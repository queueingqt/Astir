/*
 * Finding the devices, so the operator does not have to name them.
 * See device_scan.h for why this is in the core and what it is for.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/io/device_scan.h"
#include "core/io/interface.h"
#include "core/util/snprintf.h"

#ifdef HAVE_LIBAX25
  #include <netax25/ax25.h>
  #include <netax25/axlib.h>
  #include <netax25/axconfig.h>
#endif  // HAVE_LIBAX25


/* ---- serial ports -------------------------------------------------------- */

/*
 * /dev/serial/by-id holds one symlink per USB serial port, named after what the
 * device said it was.  It is the only place on a Linux system that knows the
 * difference between a radio and a 3D printer, so it is looked at first and its
 * path -- not the ttyUSB number it points at -- is what gets stored.
 *
 * Storing the by-id path rather than /dev/ttyACM0 is the whole reason to prefer
 * it: the number is assigned in the order things were plugged in and silently
 * becomes another device's the next time they are plugged in the other order.
 * A configuration that survives a reboot has to name the hardware, not the
 * enumeration order.
 */
#define BY_ID_DIR "/dev/serial/by-id"


/*
 * Turn "usb-Kenwood_Corp._TH-D75_A1B2C3-if00-port0" into "Kenwood Corp. TH-D75
 * A1B2C3".
 *
 * The bus prefix and the interface suffix are how the kernel disambiguates two
 * identical adaptors; neither means anything to a person reading a list, and
 * the underscores are only there because the name has to be a filename.
 */
static void pretty_by_id_name(const char *raw, char *out, size_t n)
{
  const char *start = raw;
  char *cut;
  size_t i;

  if (strncmp(start, "usb-", 4) == 0)
  {
    start += 4;
  }
  else if (strncmp(start, "pci-", 4) == 0)
  {
    start += 4;
  }
  else if (strncmp(start, "platform-", 9) == 0)
  {
    start += 9;
  }

  astir_snprintf(out, n, "%s", start);

  // Drop the "-if00", and anything after it, wherever it appears.  Searched
  // from the right because a product name is allowed to contain "-if" and the
  // kernel's suffix is always last.
  for (cut = out + strlen(out); cut > out; cut--)
  {
    if (strncmp(cut, "-if", 3) == 0)
    {
      *cut = '\0';
      break;
    }
  }

  for (i = 0; out[i] != '\0'; i++)
  {
    if (out[i] == '_')
    {
      out[i] = ' ';
    }
  }
}


/*
 * Whether a legacy /dev/ttyS node is a port that physically exists.
 *
 * The 8250 driver registers thirty-two of them on every PC whether or not the
 * hardware is there, and a list of thirty-two identical-looking ports of which
 * twenty-seven are imaginary is worse than no list: the real device gets pushed
 * off the end and the operator is back to guessing.  An unpopulated port
 * reports UART type 0 in sysfs, which is the cheapest way to tell without
 * opening it -- and opening a port to find out whether it is real is not free,
 * since something else may be using it.
 */
static int serial8250_is_populated(const char *name)
{
  char path[PATH_MAX];
  char buf[32];
  FILE *f;
  int type = 0;

  astir_snprintf(path, sizeof(path), "/sys/class/tty/%s/type", name);
  f = fopen(path, "r");
  if (f == NULL)
  {
    return 1;                      // no sysfs to ask: keep it rather than lie
  }
  if (fgets(buf, sizeof(buf), f) != NULL)
  {
    type = atoi(buf);
  }
  fclose(f);
  return type != 0;
}


// Whether a bare /dev entry is the kind of node a TNC or GPS turns up on.
static int is_serial_node(const char *name)
{
  if (strncmp(name, "ttyUSB", 6) == 0
      || strncmp(name, "ttyACM", 6) == 0
      || strncmp(name, "rfcomm", 6) == 0)
  {
    return 1;
  }
  if (strncmp(name, "ttyS", 4) == 0)
  {
    return serial8250_is_populated(name);
  }
  return 0;
}


/*
 * The node a by-id symlink resolves to, as a bare name ("ttyACM0").
 *
 * Wanted for two things: showing it, because that is what every other tool on
 * the system will call it, and dedup -- the same port must not appear twice
 * because it happens to have two names.
 */
static int resolve_by_id(const char *link_name, char *node, size_t n)
{
  char path[PATH_MAX];
  char target[PATH_MAX];
  ssize_t len;
  const char *base;

  astir_snprintf(path, sizeof(path), "%s/%s", BY_ID_DIR, link_name);
  len = readlink(path, target, sizeof(target) - 1);
  if (len < 0)
  {
    return 0;
  }
  target[len] = '\0';

  base = strrchr(target, '/');
  base = (base != NULL) ? base + 1 : target;
  astir_snprintf(node, n, "%s", base);
  return 1;
}


static int already_have_node(const char *node, char seen[][64], int count)
{
  int i;

  for (i = 0; i < count; i++)
  {
    if (strcmp(seen[i], node) == 0)
    {
      return 1;
    }
  }
  return 0;
}


static int scan_serial(device_candidate *out, int max)
{
  char seen[DEVICE_SCAN_MAX][64];
  struct dirent **list = NULL;
  int found = 0;
  int seen_count = 0;
  int n;
  int i;

  // Named ports first.  A list that opens with "Kenwood TH-D75" and only then
  // offers "ttyS0" is one an operator can use without knowing what a tty is.
  n = scandir(BY_ID_DIR, &list, NULL, alphasort);
  for (i = 0; i < n; i++)
  {
    char node[64];

    if (list[i]->d_name[0] != '.' && found < max
        && resolve_by_id(list[i]->d_name, node, sizeof(node)))
    {
      char pretty[128];

      pretty_by_id_name(list[i]->d_name, pretty, sizeof(pretty));
      astir_snprintf(out[found].value, sizeof(out[found].value),
                     "%s/%s", BY_ID_DIR, list[i]->d_name);
      astir_snprintf(out[found].label, sizeof(out[found].label),
                     "%s  (/dev/%s)", pretty, node);
      out[found].attached = 1;
      found++;

      if (seen_count < DEVICE_SCAN_MAX)
      {
        astir_snprintf(seen[seen_count], sizeof(seen[seen_count]), "%s", node);
        seen_count++;
      }
    }
    free(list[i]);
  }
  free(list);

  // Then the bare nodes, for everything with no by-id entry: built-in serial
  // ports, and the rfcomm nodes a Bluetooth radio is bound to, which are
  // created by hand and so are not USB devices the kernel can name.
  list = NULL;
  n = scandir("/dev", &list, NULL, alphasort);
  for (i = 0; i < n; i++)
  {
    if (is_serial_node(list[i]->d_name) && found < max
        && !already_have_node(list[i]->d_name, seen, seen_count))
    {
      astir_snprintf(out[found].value, sizeof(out[found].value),
                     "/dev/%s", list[i]->d_name);
      if (strncmp(list[i]->d_name, "rfcomm", 6) == 0)
      {
        astir_snprintf(out[found].label, sizeof(out[found].label),
                       "/dev/%s  (Bluetooth serial)", list[i]->d_name);
      }
      else
      {
        astir_snprintf(out[found].label, sizeof(out[found].label),
                       "/dev/%s", list[i]->d_name);
      }
      out[found].attached = 1;
      found++;
    }
    free(list[i]);
  }
  free(list);

  return found;
}


/* ---- kernel AX.25 ports -------------------------------------------------- */

/*
 * The axports table, which is a different question from "what is plugged in".
 *
 * A port is listed there because somebody wrote it down, not because it works.
 * It becomes real when kissattach binds it to a tty and the kernel gives it a
 * network device; until then ax25_config_get_dev() returns NULL for it and the
 * port will accept an interface, come up, and receive nothing.  That state is
 * reported rather than hidden, because it is the single most common way an
 * AX.25 station is silently deaf.
 */
#define AXPORTS_FILE "/etc/ax25/axports"

static int scan_ax25(device_candidate *out, int max)
{
  FILE *f;
  char line[512];
  int found = 0;
  int have_active = 0;

  f = fopen(AXPORTS_FILE, "r");
  if (f == NULL)
  {
    return 0;                      // no AX.25 configured on this machine
  }

  /*
   * libax25 is asked which ports are live, but the list itself is read from the
   * file rather than from libax25.
   *
   * ax25_config_load_ports() enumerates only ports the kernel has *attached*,
   * and returns zero when none are -- so asking it for the list means the
   * dropdown goes empty at precisely the moment the operator most needs to see
   * an entry, namely when the port is configured and kissattach has not run.
   * An empty list reads as "you have no AX.25 ports", which is a different and
   * much less actionable statement than "d75 is there and is not attached".
   */
#ifdef HAVE_LIBAX25
  have_active = (ax25_config_load_ports() > 0);
#endif

  while (fgets(line, sizeof(line), f) != NULL && found < max)
  {
    char name[64] = "";
    char call[32] = "";
    char desc[160] = "";
    char *p = line;
    int n;

    while (*p == ' ' || *p == '\t')
    {
      p++;
    }
    if (*p == '#' || *p == '\n' || *p == '\0')
    {
      continue;
    }

    // name callsign speed paclen window description-to-end-of-line
    n = sscanf(p, "%63s %31s %*s %*s %*s %159[^\n]", name, call, desc);
    if (n < 2 || name[0] == '\0')
    {
      continue;
    }

    astir_snprintf(out[found].value, sizeof(out[found].value), "%s", name);

    out[found].attached = 0;
#ifdef HAVE_LIBAX25
    if (have_active && ax25_config_get_dev(name) != NULL)
    {
      out[found].attached = 1;
    }
#endif

    if (out[found].attached)
    {
#ifdef HAVE_LIBAX25
      astir_snprintf(out[found].label, sizeof(out[found].label),
                     "%s  %s on %s%s%s", name, call,
                     ax25_config_get_dev(name),
                     (desc[0] != '\0') ? "  \xe2\x80\x94 " : "", desc);
#endif
    }
    else
    {
      // Deliberately blunt.  "not attached" is something an operator can act
      // on; an entry that looks identical to a working one is not, and this is
      // the single most common way an AX.25 station ends up silently deaf.
      astir_snprintf(out[found].label, sizeof(out[found].label),
                     "%s  %s \xe2\x80\x94 not attached (kissattach has not run)",
                     name, call);
    }
    found++;
  }

  fclose(f);
  (void)have_active;
  return found;
}


/* ---- what the caller asks for -------------------------------------------- */

int device_scan(int device_type, device_candidate *out, int max)
{
  if (out == NULL || max <= 0)
  {
    return 0;
  }

  switch (device_type)
  {
    case DEVICE_SERIAL_TNC:
    case DEVICE_SERIAL_TNC_HSP_GPS:
    case DEVICE_SERIAL_TNC_AUX_GPS:
    case DEVICE_SERIAL_KISS_TNC:
    case DEVICE_SERIAL_MKISS_TNC:
    case DEVICE_SERIAL_GPS:
    case DEVICE_SERIAL_WX:
      return scan_serial(out, max);

    case DEVICE_AX25_TNC:
      return scan_ax25(out, max);

    // Everything else is reached over the network, where there is nothing
    // attached to enumerate and a hostname is the only sensible answer.
    default:
      return 0;
  }
}
