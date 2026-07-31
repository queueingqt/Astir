/*
 *
 * ASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2026 The Xastir Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "core/util/snprintf.h"

#include <Xm/XmAll.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>

//Needed for Solaris 2.5
#include <netinet/in.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <setjmp.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <errno.h>

#include "core/astir.h"
#include "core/main.h"
#include "core/util/lang.h"

#include "core/xa_ui.h"

// Must be last include file
#include "core/util/leak_detection.h"
#include "core/io/forked_getaddrinfo.h"

#ifndef HAVE_SIGHANDLER_T
  #ifdef HAVE_SIG_T
    typedef sig_t sighandler_t;
  #else
    typedef void (*sighandler_t)(int);
  #endif
#endif


#ifndef __LCLINT__
  #ifndef HAVE_SIGJMP_BUF
    jmp_buf ret_place;
  #else // HAVE_SIGJMP_BUF
    static  sigjmp_buf ret_place;       /* Jump address if alarm */
  #endif    // HAVE_SIGJMP_BUF
#endif // __LCLINT__





/*************************************************************************/
/* Time out on connect                                                   */
/* In case there is a problem in getting the hostname or connecting      */
/* (see  setjmp below).                                                  */
/*************************************************************************/

static void host_time_out( int UNUSED(sig) )
{
#ifndef __LCLINT__
  siglongjmp(ret_place,0);
#endif // __LCLINT__
}





/*************************************************************************/
/* do a nice host lookup (don't thread!!)                                */
/*                                                                       */
/* host: name to lookup                                                  */
/* ip: buffer for ip's must be 400 bytes at least                        */
/* time: time in seconds to wait                                         */
/*                                                                       */
/* return the ip or ip's of the host name                                */
/* or these strings:                                                     */
/* NOHOST  for no host by that name found                                */
/* NOIP    for host found but no ip address available                    */
/* TIMEOUT for time exceeded                                             */
/*************************************************************************/

#define RETSIGTYPE void

int forked_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **resout, int time)
{

  RETSIGTYPE * previous_loc;


  pid_t host_pid;
  int status;
  int fp[2];
  int tm;
  int i;
  int rc = EAI_FAIL;
  char ttemp[60];
  int wait_host;
  struct addrinfo *res0;
  struct addrinfo *res;

  if (debug_level & 1024)
  {
    fprintf(stderr,"Start Host lookup\n");
  }

  xa_ui_busy();

  if (debug_level & 1024)
  {
    fprintf(stderr,"Creating pipe\n");
  }

  // Create a pipe for communication
  if (pipe(fp)!=0)
  {
    fprintf(stderr, "Error creating pipe for hostname lookup: %s\n",
            strerror(errno));
    return rc;
  }

  host_pid = fork();      // Fork off a child process

  if (debug_level & 1024)
  {
    fprintf(stderr,"Host fork\n");
  }

  if (host_pid!=-1)       // If the fork was successful
  {

//---------------------------------------------------------------------------------------
    if (host_pid==0)    // We're in the child process
    {


      // Go back to default signal handler instead of
      // calling restart() on SIGHUP
      (void) signal(SIGHUP,SIG_DFL);

      /*
       * THE CHILD DOES NOT RENAME ITSELF.
       *
       * It used to, so that a process listing showed "hostname lookup (astir)"
       * rather than a second copy of Astir.  That is worth very little, and it
       * cost the whole program: init_set_proc_title() malloc()s in a loop and
       * strdup()s twice, and this is the first thing after a fork() in a
       * process with nine threads in it.
       *
       * fork() copies the calling thread and nothing else.  A lock another
       * thread was holding at that instant stays locked in the child with
       * nobody left to release it, so the first malloc() can block forever --
       * and did, leaving a child parked in futex_wait and the parent spinning
       * beside it waiting for a child that would never exit.  The application
       * froze.
       *
       * POSIX is blunt about this: between fork() and exec() a child in a
       * threaded program may call only async-signal-safe functions.  malloc()
       * is not one, and neither is getaddrinfo() below -- which is why the
       * wait in the parent now has a deadline, rather than trusting this.
       */


      // Close the end of the pipe we don't need here

      if (debug_level & 1024)
      {
        fprintf(stderr,"Child closing read end of pipe\n");
      }

      close(fp[0]);   // Read end of the pipe

      if (debug_level & 1024)
      {
        fprintf(stderr,"Set alarm \n");
      }

      previous_loc = (RETSIGTYPE *)signal(SIGALRM, host_time_out);

      // Set up to jump here if we time out on SIGALRM
      if (sigsetjmp(ret_place,-1)!=0)
      {

        // Turn off the alarm
        (void)alarm(0);

        // Reset the SIGALRM handler to its previous value
        (void)signal(SIGALRM, (sighandler_t)previous_loc);

        // Return net connection time out
        rc = FAI_TIMEOUT;

        if (write(fp[1],&rc, sizeof(int)) == -1)
        {
          // Write error. Do nothing as we did prior
          // to checking the return value.
        }

        if (debug_level & 1024)
        {
          fprintf(stderr,"Child closing write end of pipe\n");
        }

        close(fp[1]);   // All done writing to the pipe
        exit(EXIT_FAILURE); // Exit from child process
      }
      (void)alarm(time);  // Start the timer

      // Make the call that may time out if no response from DNS
      /*hostinfo = gethostbyname2(host,AF_INET); some systems don't have this*/
      rc = getaddrinfo(hostname, servname, hints, &res0);

      // If we get to here, we haven't timed out
      // and we have an answer to process.

      // Turn off the alarm
      (void)alarm(0);
      // Reset the SIGALRM handler to its previous value
      (void)signal(SIGALRM, (sighandler_t)previous_loc);

      if (write(fp[1], &rc, sizeof(int)) == -1)    // Send status
      {
        // Write error. Do nothing as we did prior
        // to checking the return value.
      }

      for (res = res0; res; res = res->ai_next)
      {
        // Signal that an entry is coming.
        rc = 1;

        if (write(fp[1], &rc, sizeof(int)) == -1)
        {
          // Write error. Do nothing as we did prior
          // to checking the return value.
        }

        // Send  entry
        if (write(fp[1], res, sizeof(struct addrinfo)) == -1)
        {
          // Write error. Do nothing as we did prior
          // to checking the return value.
        }

        if (write(fp[1], res->ai_addr, res->ai_addrlen) == -1)
        {
          // Write error. Do nothing as we did prior
          // to checking the return value.
        }

      }
      // Signal that there is nothing left
      rc = 0;

      if (write(fp[1], &rc, sizeof(int)) == -1)
      {
        // Write error. Do nothing as we did prior
        // to checking the return value.
      }

      if (debug_level & 1024)
      {
        fprintf(stderr,"Child closing write end of pipe\n");
      }

      freeaddrinfo(res0);
      close(fp[1]);   // All done writing to the pipe
      exit(EXIT_FAILURE); // Exit from child process

    }   // End of child process
//---------------------------------------------------------------------------------------
    else
    {
      // We're in the parent process at this point

      // Close the end of the pipe we don't need here

      if (debug_level & 1024)
      {
        fprintf(stderr,"Parent closing write end of pipe\n");
      }

      close(fp[1]);   // Write end of the pipe

      /*
       * Wait for the child, with a sleep and a deadline.
       *
       * This was a busy-wait -- waitpid(WNOHANG) and sched_yield() round a
       * loop, with the usleep commented out -- and it left only when waitpid
       * returned -1.  A child that never exits returns 0 forever, so the loop
       * span at one whole core until the program was killed.  From outside
       * that is not a slow lookup; it is a frozen application.
       *
       * And a child that never exits is not hypothetical.  fork() in a
       * threaded program copies the calling thread alone: any lock another
       * thread held at that instant stays locked in the child, with nothing
       * left running to release it.  The child here goes on to allocate -- in
       * set_proc_title, in getaddrinfo, in the resolver's own module loading --
       * and any one of those can block on the allocator's lock forever.  That
       * is what happened: a child parked in futex_wait, and a parent burning a
       * core beside it.
       *
       * So: sleep between checks, and give up after a deadline.  A name that
       * cannot be resolved in this long is not going to be, and the caller
       * already knows how to report a lookup that failed.
       */
      {
        const int poll_ms = 20;                   /* between checks */
        const int deadline_ms = 20000;            /* 20 s, then give up */
        int waited_ms = 0;

        tm = 1;
        wait_host = 0;
        while ((wait_host = waitpid(host_pid, &status, WNOHANG)) == 0)
        {
          if (waited_ms >= deadline_ms)
          {
            fprintf(stderr,
                    "hostname lookup for '%s' did not finish in %d seconds; "
                    "abandoning it\n",
                    hostname ? hostname : "(null)", deadline_ms / 1000);

            // SIGKILL, not SIGTERM: a child deadlocked on a lock it can never
            // acquire will not run a handler.
            (void)kill(host_pid, SIGKILL);
            (void)waitpid(host_pid, &status, 0);
            wait_host = -1;
            rc = FAI_TIMEOUT;
            break;
          }

          // Once a second, say that something is still happening -- and only
          // once a second, because this used to redraw a status line as fast
          // as the loop could turn.
          if (waited_ms % 1000 == 0)
          {
            astir_snprintf(ttemp, sizeof(ttemp), langcode("BBARSTA031"), tm++);
            xa_ui_status(ttemp);      // Looking up hostname...
          }

          usleep(poll_ms * 1000);
          waited_ms += poll_ms;
        }
        (void)i;
      }

      if (rc == FAI_TIMEOUT)
      {
        close(fp[0]);
        return rc;                    // nothing was written to the pipe
      }
      // Get the return code
      if (read(fp[0],&rc,sizeof(int)) == -1)
      {
        // Read error. Do nothing as we did prior
        // to checking the return value.
      }

      if(rc!=0)
      {
        close(fp[0]);
        return rc;
      }

      if (read(fp[0], &status, sizeof(int)) == -1)
      {
        // Read error. Do nothing as we did prior
        // to checking the return value.
      }

      while(status)
      {
        *resout = (struct addrinfo*) malloc(sizeof(struct addrinfo));
        res = *resout;

        if (read(fp[0], res, sizeof(struct addrinfo)) == -1)
        {
          // Read error. Do nothing as we did prior
          // to checking the return value.
        }

        res->ai_addr = (struct sockaddr*) malloc(res->ai_addrlen);

        if (read(fp[0], res->ai_addr, res->ai_addrlen) == -1)
        {
          // Read error. Do nothing as we did prior
          // to checking the return value.
        }

        res->ai_canonname = NULL;
        resout = &res->ai_next;

        // See if there is another
        if (read(fp[0], &status, sizeof(int)) == -1)
        {
          // Read error. Do nothing as we did prior
          // to checking the return value.
        }
      }
      *resout = NULL;

      if (debug_level & 1024)
      {
        fprintf(stderr,"Parent closing read end of pipe\n");
      }

      close(fp[0]);   // Close the read end of the pipe
    }
  }
  else    // We didn't fork
  {
    // Close both ends of the pipe to make
    // sure we've cleaned up properly
    close(fp[0]);
    close(fp[1]);
  }
  return rc;
}

void forked_freeaddrinfo(struct addrinfo *ai)
{
  struct addrinfo *next = ai;
  struct addrinfo *current = ai;

  while(next)
  {
    current = next;
    next = current->ai_next;

    free(current->ai_addr);
    free(current);
  }
}


