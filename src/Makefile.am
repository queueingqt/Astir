#
# Copyright (C) 2000-2026 The Xastir Group
#
# The layout mirrors the architecture:
#
#   core/    links with NO front end at all.  Built once, as a library, and
#            that is only possible because the core has no toolkit dependency
#            -- so the build now demonstrates the boundary that
#            tools/link_core.py measures, instead of merely not contradicting
#            it.
#   draw/    the xa_draw.h contract and the backends implementing it.  Exactly
#            one backend is linked into any binary.
#   ui/      the front end.  GTK4.
#   apps/    standalone binaries that are not Astir.
#
# Includes are written as full paths from src/ ("core/util/util.h"), so a line
# says which layer it reaches into.

bin_PROGRAMS = astir astir_udp_client astir_testdbfawk

SUBDIRS = rtree
DIST_SUBDIRS = rtree

# The null backend ships but is never linked here: it defines the same symbols
# as the Cairo one.  tools/link_null.py compiles it itself, to ask whether the
# core can link against a backend that draws nothing.
EXTRA_DIST = icon.xbm \
    core/io/io-common.c \
    core/io/macspeech.c \
    draw/null/xa_draw_null.c

noinst_LIBRARIES = libastircore.a

libastircore_a_SOURCES = \
    core/aprs/alert.c \
    core/aprs/alert.h \
    core/aprs/cad_objects.c \
    core/aprs/cad_objects.h \
    core/aprs/database.h \
    core/aprs/db.c \
    core/aprs/db_funcs.h \
    core/aprs/db_gis.c \
    core/aprs/db_gis.h \
    core/aprs/dr_utils.c \
    core/aprs/dr_utils.h \
    core/aprs/fcc_data.c \
    core/aprs/fcc_data.h \
    core/aprs/igate.c \
    core/aprs/igate.h \
    core/aprs/location.c \
    core/aprs/messages.c \
    core/aprs/messages.h \
    core/aprs/object_utils.c \
    core/aprs/object_utils.h \
    core/aprs/objects.c \
    core/aprs/objects.h \
    core/aprs/rac_data.c \
    core/aprs/rac_data.h \
    core/aprs/station_draw.c \
    core/aprs/station_draw.h \
    core/aprs/tactical_call_utils.c \
    core/aprs/tactical_call_utils.h \
    core/aprs/wx.c \
    core/aprs/wx.h \
    core/astir.h \
    core/geo/ambiguity_utils.c \
    core/geo/ambiguity_utils.h \
    core/geo/datum.c \
    core/geo/datum.h \
    core/geo/mgrs_utils.c \
    core/geo/mgrs_utils.h \
    core/globals.h \
    core/io/dlm.c \
    core/io/dlm.h \
    core/io/festival.c \
    core/io/festival.h \
    core/io/fetch_remote.c \
    core/io/fetch_remote.h \
    core/io/forked_getaddrinfo.c \
    core/io/forked_getaddrinfo.h \
    core/io/gps.c \
    core/io/gps.h \
    core/io/interface.c \
    core/io/interface.h \
    core/io/sound.c \
    core/io/sound.h \
    core/io/x_spider.c \
    core/io/x_spider.h \
    core/main.h \
    core/map/awk.c \
    core/map/awk.h \
    core/map/dbfawk.c \
    core/map/dbfawk.h \
    core/map/geocoder.c \
    core/map/geocoder.h \
    core/map/map_OSM.c \
    core/map/map_OSM.h \
    core/map/map_WMS.c \
    core/map/map_cache.c \
    core/map/map_cache.h \
    core/map/map_dos.c \
    core/map/map_geo.c \
    core/map/map_gnis.c \
    core/map/map_pop.c \
    core/map/map_shp.c \
    core/map/map_shp_fwd.h \
    core/map/map_tif.c \
    core/map/maps.c \
    core/map/mvt.c core/map/mvt.h \
    core/map/maps.h \
    core/map/shp_hash.c \
    core/map/shp_hash.h \
    core/map/tile_mgmnt.c \
    core/map/tile_mgmnt.h \
    core/render/draw_symbols.c \
    core/render/symbols_vector.c core/render/symbols_vector.h \
    core/render/symbol_draw_vector.c \
    core/render/draw_symbols.h \
    core/render/symbols.h \
    core/state/xa_config.c \
    core/state/xa_config.h \
    core/state/xa_settings.c \
    core/state/xa_settings.h \
    core/state/xa_state.c \
    core/state/xa_state.h \
    core/util/debug_utils.c \
    core/util/debug_utils.h \
    core/util/encoding.c \
    core/util/encoding.h \
    core/util/hashtable.c \
    core/util/hashtable.h \
    core/util/hashtable_itr.c \
    core/util/hashtable_itr.h \
    core/util/hashtable_private.h \
    core/util/lang.c \
    core/util/lang.h \
    core/util/leak_detection.h \
    core/util/log_utils.c \
    core/util/log_utils.h \
    core/util/mutex_utils.c \
    core/util/mutex_utils.h \
    core/util/rpl_malloc.c \
    core/util/rpl_malloc.h \
    core/util/snprintf.c \
    core/util/snprintf.h \
    core/util/timer_utils.c \
    core/util/timer_utils.h \
    core/util/util.c \
    core/util/util.h \
    core/util/xa_perf.c \
    core/util/xa_perf.h \
    core/util/xa_trace.c \
    core/util/xa_trace.h \
    core/xa_ui.c \
    core/xa_ui.h

# Conditionally compiled sources
if HAVE_NOMINATIM
NOMINATIM_SRC = core/map/nominatim.c core/map/nominatim.h
else
NOMINATIM_SRC =
endif

libastircore_a_LIBADD =
nodist_libastircore_a_SOURCES = core/util/compiledate.c
libastircore_a_SOURCES += $(NOMINATIM_SRC)

# Astir itself: the GTK4 front end, the Cairo backend, and the core.
astir_SOURCES = \
    draw/xa_draw.h \
    draw/gtk4/xa_draw_gtk4.c \
    ui/gtk4/xa_gtk4_main.c \
    ui/gtk4/xa_gtk4_palette.c

astir_CPPFLAGS = $(AM_CPPFLAGS) $(GTK4_CFLAGS)
astir_LDADD = libastircore.a -Lrtree -lrtree $(GTK4_LIBS)

astir_udp_client_SOURCES = \
    apps/astir_udp_client.c

astir_testdbfawk_SOURCES = \
    apps/testdbfawk.c \
    core/map/awk.c \
    core/map/dbfawk.c \
    core/util/rpl_malloc.c core/util/rpl_malloc.h

.PHONY: core/util/compiledate.c

core/util/compiledate.c: $(libastircore_a_SOURCES)
	rm -f core/util/compiledate.c core/util/compiledate.$(OBJEXT)
	echo 'char gitstring[] = "'"`$(top_srcdir)/scripts/AstirGitStamp.sh $(top_srcdir)`"'";' >> core/util/compiledate.c

remove_compiledate:
	rm -f core/util/compiledate.c core/util/compiledate.$(OBJEXT)

astir_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

astir_udp_client_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

astir_testdbfawk_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@
