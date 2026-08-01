/*
 * Draining the incoming queue, and the once-a-second housekeeping.
 * See incoming.h for why this is in the core rather than in a front end.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "core/astir.h"
#include "core/aprs/alert.h"
#include "core/aprs/db_funcs.h"
#include "core/io/gps.h"
#include "core/io/incoming.h"
#include "core/io/interface.h"
#include "core/state/xa_config.h"      // get_user_base_dir, MAX_VALUE
#include "core/state/xa_settings.h"
#include "core/util/log_utils.h"
#include "core/util/snprintf.h"

#ifdef HAVE_LIBSHP
  #include "core/map/shp_hash.h"
#endif

// Enough that a normal channel never hits it, low enough that a firehose
// cannot hold the caller's event loop for a visible length of time.
#define PUMP_DEFAULT_BUDGET 250


/* ---- waking a front end when a packet arrives --------------------------- */

/*
 * A self-pipe: read end for the main loop, write end for the read threads.
 *
 * A pipe rather than an eventfd because it is portable, and one byte rather
 * than a count because the reader drains the whole queue anyway -- the byte
 * says "something is there", not how much.
 *
 * Non-blocking on both ends deliberately.  A write that would block means the
 * pipe is already full of wakeups nobody has collected yet, which means the
 * main loop is already going to wake and drain; dropping that byte loses
 * nothing.  A blocking write there would stall a read thread on the UI.
 */
static int wakeup_fd[2] = { -1, -1 };


int xa_incoming_wakeup_fd(void)
{
  if (wakeup_fd[0] == -1)
  {
    if (pipe(wakeup_fd) != 0)
    {
      wakeup_fd[0] = wakeup_fd[1] = -1;
      return -1;
    }
    (void)fcntl(wakeup_fd[0], F_SETFL, O_NONBLOCK);
    (void)fcntl(wakeup_fd[1], F_SETFL, O_NONBLOCK);
  }
  return wakeup_fd[0];
}


void xa_incoming_wake(void)
{
  static const char one = 'x';

  if (wakeup_fd[1] != -1)
  {
    // Deliberately unchecked: see above, a full pipe is a wakeup already
    // pending and losing this byte costs nothing.
    (void)!write(wakeup_fd[1], &one, 1);
  }
}


void xa_incoming_drain_wakeup(void)
{
  char buf[256];

  if (wakeup_fd[0] == -1)
  {
    return;
  }
  while (read(wakeup_fd[0], buf, sizeof(buf)) > 0)
  {
    ;                            // however many bytes, one drain
  }
}


/*
 * Write a received line to the log, if that log is enabled.
 *
 * Two logs, not one: a packet off the radio and a packet off the internet are
 * different evidence, and someone reading a log back wants to know which they
 * are looking at without having to infer it.
 */
static void log_if_enabled(int which, const char *line)
{
  char path[MAX_VALUE];

  if (which < 0 || which >= XA_LOG_COUNT || !xa_log[which].enabled)
  {
    return;
  }
  log_data(get_user_base_dir(xa_log[which].file, path, sizeof(path)),
           (char *)line);
}


int xa_incoming_pump(int budget)
{
  unsigned char data_string[MAX_LINE_SIZE + 1];
  int done = 0;

  if (budget <= 0)
  {
    budget = PUMP_DEFAULT_BUDGET;
  }

  while (done < budget)
  {
    int data_port = 0;
    int data_length = pop_incoming_data(data_string, &data_port);

    if (data_length == 0)
    {
      break;                     // queue empty; the normal way out
    }
    data_string[data_length] = '\0';

    if (data_port < 0 || data_port >= MAX_IFACE_DEVICES)
    {
      continue;                  // a port that went away under us
    }

    /*
     * Which decoder, and what to call the source.
     *
     * The 'I' and 'T' passed to decode_ax25_line are not decoration: they say
     * whether the packet arrived over the internet or over the air, and that
     * decides whether it may be gated back out.  Getting it wrong is how a
     * station ends up transmitting the internet onto the radio.
     */
    switch (port_data[data_port].device_type)
    {
      case DEVICE_NET_STREAM:
        log_if_enabled(XA_LOG_NET, (char *)data_string);
        packet_data_add(langcode("WPUPDPD006"), (char *)data_string, data_port);
        decode_ax25_line((char *)data_string, 'I', data_port, 1);
        break;

      case DEVICE_SERIAL_KISS_TNC:
      case DEVICE_SERIAL_MKISS_TNC:
        /*
         * A KISS frame carries a real AX.25 header, which has to come off
         * before there is anything a text decoder can read.  This can make the
         * string LONGER, which is why the length goes in by address.
         */
        if (!decode_ax25_header(data_string, &data_length))
        {
          break;                 // bad header or checksum; drop it
        }
        data_string[data_length] = '\0';
        log_if_enabled(XA_LOG_TNC, (char *)data_string);
        packet_data_add(langcode("WPUPDPD005"), (char *)data_string, data_port);
        decode_ax25_line((char *)data_string, 'T', data_port, 1);
        break;

      case DEVICE_SERIAL_TNC:
      case DEVICE_SERIAL_TNC_HSP_GPS:
      case DEVICE_SERIAL_TNC_AUX_GPS:
        // A TNC in converse mode hands over printable text with its own
        // decoration; strip that first.
        tnc_data_clean((char *)data_string);
        log_if_enabled(XA_LOG_TNC, (char *)data_string);
        packet_data_add(langcode("WPUPDPD005"), (char *)data_string, data_port);
        decode_ax25_line((char *)data_string, 'T', data_port, 1);
        break;

      /*
       * AGWPE and kernel AX.25 both hand over a packet already in the printable
       * form the decoder reads, so neither needs unwrapping.  AGWPE is the path
       * a software TNC such as Direwolf arrives on.
       */
      case DEVICE_AX25_TNC:
      case DEVICE_NET_AGWPE:
        log_if_enabled(XA_LOG_TNC, (char *)data_string);
        packet_data_add(langcode("WPUPDPD005"), (char *)data_string, data_port);
        decode_ax25_line((char *)data_string, 'T', data_port, 1);
        break;

      default:
        /*
         * GPS and weather ports are not decoded here.  Their read threads keep
         * only the most recent sentence rather than queueing every one, because
         * a position from four seconds ago is not worth the queue slot, and
         * they are sampled on their own schedule.
         */
        break;
    }
    done++;
  }

  return done;
}


/*
 * Parse whatever the GPS last said, on this thread.
 *
 * The read thread deliberately does not queue GPS sentences -- it keeps only
 * the newest GPRMC and GPGGA in globals, because a position from four seconds
 * ago is not worth a queue slot.  Something then has to come along and read
 * them, and in the Motif build that was UpdateTime() on a timer.  Nothing did
 * after the front end changed, so gps_data_find() ended up with no callers at
 * all and the whole GPS path was dead: no position on the map, and no
 * SmartBeaconing, which is what the beacon schedule is built on.
 *
 * Here rather than in the read thread because gps_data_find() ends in
 * my_station_gps_change(), which touches the station database and the beacon
 * queue.  Those belong to the main loop; the read thread's job is to get the
 * bytes off the socket and say so.
 *
 * Returns non-zero if a sentence was consumed, so the caller can redraw.
 */
int xa_gps_pump(void)
{
  char line[MAX_LINE_SIZE + 1];
  int port = gps_port_save;
  int did = 0;

  if (port < 0 || port >= MAX_IFACE_DEVICES)
  {
    return 0;
  }

  // Copied, and the global cleared first: gps_data_find() is destructive to
  // what it is given, and the read thread may write another sentence in at any
  // moment.
  if (gprmc_save_string[0] != '\0')
  {
    astir_snprintf(line, sizeof(line), "%s", gprmc_save_string);
    gprmc_save_string[0] = '\0';
    (void)gps_data_find(line, port);
    did = 1;
  }

  if (gpgga_save_string[0] != '\0')
  {
    astir_snprintf(line, sizeof(line), "%s", gpgga_save_string);
    gpgga_save_string[0] = '\0';
    (void)gps_data_find(line, port);
    did = 1;
  }

  return did;
}


int xa_housekeeping(time_t now)
{
  static time_t last = 0;
  static time_t last_port_check = 0;

  if (now == last)
  {
    return 0;                    // called from a faster timer; nothing due
  }
  last = now;

  // Not on the first call: at startup every port has just been brought up (or
  // deliberately not), and a reconnect sweep a second later would fight it.
  if (last_port_check == 0)
  {
    last_port_check = now;
  }

  check_station_remove(now);     // stations older than the expiry setting
  check_message_remove(now);     // messages likewise

#ifdef HAVE_LIBSHP
  purge_shp_hash(now);           // shapefile indexes for maps no longer shown
#endif

  // Always, even when the circle is not drawn: it is computed from a rolling
  // window, so a circle switched on after an hour of running should be right
  // immediately rather than starting to gather history at that moment.
  calc_aloha((int)now);

  (void)alert_expire((int)now);  // sets the redraw flags itself

  /*
   * Reconnecting a dropped port is NOT done here any more.
   *
   * This function is now called from packet arrival and from drawing a frame,
   * and a port that is down produces neither -- so a reconnect driven from here
   * would never happen in exactly the situation that needs it.  The front end
   * arms a retry when a port reports failure instead, which is the moment the
   * decision can actually be made.
   */

  return 1;
}
