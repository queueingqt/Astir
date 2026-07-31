/*
 * Draining the incoming queue, and the once-a-second housekeeping.
 * See incoming.h for why this is in the core rather than in a front end.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <string.h>

#include "core/astir.h"
#include "core/aprs/alert.h"
#include "core/aprs/db_funcs.h"
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


int xa_housekeeping(time_t now)
{
  static time_t last = 0;

  if (now == last)
  {
    return 0;                    // called from a faster timer; nothing due
  }
  last = now;

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

  return 1;
}
