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







// The SIGALRM handler and its jump buffer went with the fork: the timeout
// they implemented is the resolver's own job now.





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

/*
 * Resolve a hostname.
 *
 * THIS NO LONGER FORKS, and the name is now a lie kept for its callers.
 *
 * It used to fork a child, have the child call getaddrinfo() under a SIGALRM,
 * and pass the answer back down a pipe a field at a time.  The fork bought one
 * thing -- a hard timeout on a resolver that historically had none -- and cost
 * far more than it bought.
 *
 * fork() in a threaded program copies the calling thread and nothing else.
 * Every lock another thread held at that instant stays locked in the child,
 * with nothing left running to release it, and getaddrinfo() allocates and
 * loads NSS modules and takes locks of its own.  POSIX says only
 * async-signal-safe functions may be called between fork() and exec() for
 * exactly this reason; getaddrinfo() is not one of them.
 *
 * The consequences were not subtle.  A child that deadlocked never exited, and
 * the parent waited for it in a busy loop, so the whole application froze at
 * one hundred percent of a core.  Once that was bounded with a deadline, the
 * same deadlock turned into interfaces that simply refused to connect after
 * twenty seconds of nothing -- and it got MORE likely, not less, the more the
 * program had going on, because more threads means more locks held at any
 * instant.  Starting an interface at boot usually worked; starting the same one
 * from a button, with the whole GUI running, usually did not.
 *
 * So it calls getaddrinfo() directly.  Modern resolvers have their own
 * timeouts, which is what the fork was there to provide, and a call that takes a
 * few seconds is worth far more than one that occasionally takes the program
 * with it.  The `time` argument is accepted and ignored.
 */
int forked_getaddrinfo(const char *hostname, const char *servname,
                       const struct addrinfo *hints, struct addrinfo **resout,
                       int time)
{
  int rc;

  (void)time;                    // the resolver imposes its own

  if (resout == NULL)
  {
    return EAI_FAIL;
  }
  *resout = NULL;

  xa_ui_busy();

  if (debug_level & 1024)
  {
    fprintf(stderr, "Looking up %s\n", hostname ? hostname : "(null)");
  }

  rc = getaddrinfo(hostname, servname, hints, resout);

  if (rc != 0 && debug_level & 1024)
  {
    fprintf(stderr, "Lookup of %s failed: %s\n",
            hostname ? hostname : "(null)", gai_strerror(rc));
  }
  return rc;
}


/*
 * Release what forked_getaddrinfo() returned.
 *
 * The list is the system's now, not one rebuilt field by field out of a pipe,
 * so it goes back to the system.  These two have to change together: freeing a
 * getaddrinfo() list with free() corrupts the heap.
 */
void forked_freeaddrinfo(struct addrinfo *ai)
{
  if (ai != NULL)
  {
    freeaddrinfo(ai);
  }
}
