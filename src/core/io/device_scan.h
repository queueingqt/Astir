/*
 * What is actually plugged into this machine, for the interface editor to
 * offer instead of asking the operator to type a device path from memory.
 *
 * In the core rather than in a front end because it is not a drawing question:
 * it reads /dev and the AX.25 port table and answers with plain structs, so a
 * second front end -- or a test -- gets the same answer without a toolkit.
 *
 * The motivating failure is worth recording, because it is invisible rather
 * than loud.  A TH-D75 presents two entirely different things that a person
 * would call "the radio": a USB CDC-ACM tty, and a kernel AX.25 port fed from
 * the radio's internal TNC over Bluetooth.  On one machine both were reachable
 * under the name "d75" -- a udev symlink /dev/d75 for the first, an axports
 * entry d75 for the second.  Typing the wrong one into the Device box produced
 * an interface that opened without error, reported itself up, and delivered no
 * bytes ever.  Nothing in the program could have said so, because nothing in
 * the program had ever looked at what was attached.
 *
 * So the editor should not be a free-text box with a guess in it.  It should
 * be a list of what is really there.
 */
#ifndef ASTIR_DEVICE_SCAN_H
#define ASTIR_DEVICE_SCAN_H

#include "core/io/interface.h"    /* MAX_DEVICE_NAME */

/*
 * More candidates than any real station has, and small enough to be a caller's
 * local array.  A machine with more serial ports than this is not one whose
 * operator needs a dropdown.
 */
#define DEVICE_SCAN_MAX 32

typedef struct
{
  /*
   * What to store in devices[].device_name -- a path for a serial port, a bare
   * port name for AX.25.  Exactly the string the interface code expects, so the
   * caller never has to know which kind it is holding.
   */
  char value[MAX_DEVICE_NAME + 1];

  /*
   * What to show a person.  The product name where the system knows it, and the
   * node it resolves to, because those are the two things an operator needs to
   * recognise the thing on the desk.
   */
  char label[192];

  /*
   * Whether this looks usable right now, as opposed to merely configured.
   *
   * An axports entry with no kissattach behind it is the exact trap this whole
   * file exists for: it is in the table, it looks like a port, and it passes no
   * packets.  Saying so in the list is cheaper than finding out on the air.
   */
  int attached;
} device_candidate;


/*
 * Fill out[] with what could serve as the device for an interface of this type,
 * and return how many were found.
 *
 * Serial types get the tty nodes; DEVICE_AX25_TNC gets the axports table.  A
 * type with nothing to enumerate -- anything reached over the network -- yields
 * zero, which is not an error and should leave the caller's free-text box alone.
 *
 * The ordering is deliberate: things that are attached come first, and within
 * that, things the system could name come before bare node numbers.  A list
 * whose first entry is usually the right one is the point of the exercise.
 */
int device_scan(int device_type, device_candidate *out, int max);

#endif /* ASTIR_DEVICE_SCAN_H */
