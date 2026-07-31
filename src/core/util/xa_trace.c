/*
 * xa_trace.c -- deterministic operation log.  See xa_trace.h for why it exists
 * and what it must never record.  Inert unless ASTIR_TRACE is set.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "core/util/xa_trace.h"

static FILE *trace_fp = NULL;
static int   trace_on = -1;         // -1 = not yet checked


int xa_trace_enabled(void)
{
  if (trace_on < 0)
  {
    const char *path = getenv("ASTIR_TRACE");

    trace_on = 0;
    if (path && *path)
    {
      trace_fp = fopen(path, "w");
      if (trace_fp != NULL)
      {
        trace_on = 1;
      }
      else
      {
        // Say so.  A trace that silently produced nothing would read as "no
        // operations happened", which is the same output as a passing diff.
        fprintf(stderr, "xa_trace: cannot open %s -- tracing off\n", path);
      }
    }
  }
  return trace_on;
}


void xa_trace(const char *fmt, ...)
{
  va_list ap;

  if (!xa_trace_enabled())
  {
    return;
  }

  va_start(ap, fmt);
  vfprintf(trace_fp, fmt, ap);
  va_end(ap);
  fputc('\n', trace_fp);

  // Flushed per record on purpose.  The snapshot harness stops Astir with
  // SIGTERM and escalates to SIGKILL, so anything still buffered is lost -- and
  // a trace truncated at an arbitrary point would diff as a real difference.
  fflush(trace_fp);
}


const char *xa_trace_quote(const char *s, char *out, size_t n)
{
  size_t w = 0;

  if (out == NULL || n == 0)
  {
    return out;
  }

  // Every branch below needs room for the closing quote and the NUL, so keep
  // two bytes in hand and truncate cleanly rather than writing a record that
  // cannot be parsed.
  if (n >= 3)
  {
    out[w++] = '"';

    if (s == NULL)
    {
      // Distinct from "" on purpose: passing NULL where a string was expected
      // is exactly the kind of thing a refactor changes by accident.
      const char *nul = "(null)";
      while (*nul && w + 2 < n)
      {
        out[w++] = *nul++;
      }
    }
    else
    {
      while (*s && w + 2 < n)
      {
        unsigned char c = (unsigned char)*s++;

        switch (c)
        {
          case '\n':
          case '\t':
          case '\r':
          case '\\':
          case '"':
            if (w + 3 >= n)
            {
              // No room for the pair; stop here rather than emit half of it.
              goto done;
            }
            out[w++] = '\\';
            out[w++] = (c == '\n') ? 'n'
                       : (c == '\t') ? 't'
                       : (c == '\r') ? 'r'
                       : (char)c;
            break;

          default:
            // Anything outside printable ASCII goes out as \xHH.  Message text
            // can carry high-bit bytes (the traffic_utf8_enabled path converts
            // to latin1), and those must not depend on the locale of whatever
            // reads the trace.
            if (c < 0x20 || c >= 0x7f)
            {
              static const char hex[] = "0123456789abcdef";
              if (w + 5 >= n)
              {
                goto done;
              }
              out[w++] = '\\';
              out[w++] = 'x';
              out[w++] = hex[(c >> 4) & 0xf];
              out[w++] = hex[c & 0xf];
            }
            else
            {
              out[w++] = (char)c;
            }
            break;
        }
      }
    }

done:
    out[w++] = '"';
  }

  out[w] = '\0';
  return out;
}
