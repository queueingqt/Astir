
# Copyright (C) 2000-2026 The Xastir Group


MAINTAINERCLEANFILES=Makefile.in

# astir_callpass, not callpass.  Xastir installs /usr/bin/callpass too, and a
# user may have both programs installed at once -- whichever package was
# unpacked second would silently own the name.
bin_PROGRAMS = astir_callpass

astir_callpass_SOURCES = callpass.c

astir_callpass_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

