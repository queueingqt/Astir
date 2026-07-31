/*
 * The bridge between the interfaces and the decoder, and the housekeeping tick.
 *
 * An interface is a thread.  It reads its port, and hands whole lines to
 * channel_data(), which appends them to a queue and returns -- decoding on a
 * read thread would mean the whole station database was touched from every
 * thread at once.  So something has to drain that queue, and until it does, a
 * connected radio produces nothing at all: bytes arrive, the queue fills, and
 * the map stays empty.
 *
 * That drain was in the Motif main()'s UpdateTime() timer, and it did not come
 * across when the entry point moved to GTK4.  It is core work -- pulling a
 * packet off a queue and handing it to the AX.25 decoder has nothing to do with
 * any toolkit -- so it lives here now, and a front end only has to arrange for
 * it to be called.
 *
 * WHAT A FRONT END MUST DO
 *
 *   xa_incoming_pump()   often -- every 10-50 ms.  Drains what has arrived.
 *   xa_housekeeping()    about once a second.  Expiry and timeouts.
 *
 * Neither blocks, and both are safe to call when no interface is up.
 */
#ifndef ASTIR_INCOMING_H
#define ASTIR_INCOMING_H

#include <time.h>

/*
 * Drain the incoming queue and decode what is in it.
 *
 * Returns the number of packets processed, which is worth having: a front end
 * that gets a non-zero count knows the display is out of date without having to
 * be told separately.
 *
 * `budget` bounds one call so a busy channel cannot starve the caller's event
 * loop -- at 1200 baud this never binds, but an APRS-IS firehose or a replayed
 * log will hand over thousands of packets at once.  Pass 0 for a sane default.
 */
int xa_incoming_pump(int budget);

/*
 * Periodic expiry: old stations, old messages, timed-out popups and status
 * lines, stale map indexes, weather alerts, the Aloha circle.
 *
 * Does nothing until a second has passed since the last time it ran, so calling
 * it from a faster timer is harmless.  Returns 1 if it did the work.
 */
int xa_housekeeping(time_t now);

#endif /* ASTIR_INCOMING_H */
