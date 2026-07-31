/*
 * xa_trace.h -- a deterministic log of operations, for verifying changes the
 * pixel harness cannot see.
 *
 * tools/snapshot_ab.sh compares `pixmap_final`, the finished map canvas.  That
 * covers the drawing path and nothing else: the Send Message windows are
 * separate Motif dialogs and never appear in the snapshot at all.  So the whole
 * of the remaining Motif-in-core work -- update_messages() in db.c, the window
 * management in messages.c -- is invisible to the only harness that existed,
 * and "verified" would have meant "compiled".
 *
 * This is the cheaper and stricter alternative: have the code under test say
 * what it is doing, run the same input through both builds, and diff.  It does
 * not depend on the change being visible, which is the point.
 *
 * The output must be byte-identical between two runs of the *same* build, or it
 * cannot tell A from B.  That rules out anything that varies run to run:
 *
 *   - no wall-clock time, no elapsed time, no sequence numbers tied to timing
 *   - no pointers, no addresses, no anything derived from an allocation
 *   - no iteration over a container whose order is not defined
 *
 * A record is one line.  Text arguments must be passed through xa_trace_quote()
 * first, because message bodies contain newlines and a record that spans lines
 * makes the diff unreadable.
 *
 * Entirely inert unless ASTIR_TRACE names a file, so it can stay in the tree.
 * This header must never include an X11, Xt or Motif header.
 */

#ifndef XA_TRACE_H
#define XA_TRACE_H

#include <stddef.h>

// Nonzero when tracing is active.  Cheap; safe to call anywhere, including
// before main() has done anything.  Guard the call sites with it when building
// the arguments costs something.
int xa_trace_enabled(void);

// Write one record.  A newline is appended; do not include one.  No-op when
// tracing is off.
void xa_trace(const char *fmt, ...)
#ifdef __GNUC__
__attribute__((format(printf, 1, 2)))
#endif
;

// Escape `s` into `out` as a double-quoted C-style string, so that a record
// stays on one line whatever the text contains.  Always NUL-terminates, and
// truncates rather than overflowing.  Returns `out` so it can be used inline.
//
// Takes a caller-supplied buffer rather than returning a static one on purpose:
// a record can quote two strings, and a static buffer would silently return the
// same text for both.
const char *xa_trace_quote(const char *s, char *out, size_t n);

#endif // XA_TRACE_H
