#
# Copyright (C) 2000-2026 The Xastir Group
#
# The layout mirrors the architecture rather than the order files were written:
#
#   core/    links with NO front end at all.  tools/link_core.py proves it by
#            linking these objects on their own; that test is the reason the
#            directory exists as a separate thing from ui/.
#   draw/    the xa_draw.h contract and the backends implementing it.  Exactly
#            one backend is linked into any binary.
#   ui/      the front ends.  motif/ is built here; gtk4/ is built by
#            tools/build_gtk4.sh, which links the same core objects against
#            draw/gtk4 instead.
#   apps/    standalone binaries that are not Astir.
#
# Includes are written as full paths from src/ ("core/util/util.h"), so a line
# says which layer it reaches into.  That is deliberate: -I per directory would
# have been a smaller change and would have made a core file including a Motif
# header look exactly like every other include.

# astir_testdbfawk, not testdbfawk: Xastir installs that name as well, and both
# programs are installable side by side.
bin_PROGRAMS = astir astir_udp_client astir_testdbfawk

SUBDIRS = rtree
DIST_SUBDIRS = rtree

# Two backends ship but must not link into astir: each defines the same
# symbols as draw/x11.  tools/link_null.py and tools/build_gtk4.sh compile them
# themselves.
EXTRA_DIST = icon.xbm \
    core/io/io-common.c \
    core/io/macspeech.c \
    draw/null/xa_draw_null.c \
    draw/gtk4/xa_draw_gtk4.c \
    ui/gtk4/xa_gtk4_main.c \
    ui/gtk4/xa_gtk4_palette.c

ASTIR_SRC = \
    core/globals.h \
    core/main.h \
    core/xa_ui.c \
    core/xa_ui.h \
    core/astir.h \
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
    core/geo/ambiguity_utils.c \
    core/geo/ambiguity_utils.h \
    core/geo/datum.c \
    core/geo/datum.h \
    core/geo/mgrs_utils.c \
    core/geo/mgrs_utils.h \
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
    core/map/maps.h \
    core/map/shp_hash.c \
    core/map/shp_hash.h \
    core/map/tile_mgmnt.c \
    core/map/tile_mgmnt.h \
    core/render/draw_symbols.c \
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
    draw/xa_draw.h \
    draw/x11/cairo_text.c \
    draw/x11/cairo_text.h \
    draw/x11/color.c \
    draw/x11/color.h \
    draw/x11/rotated.c \
    draw/x11/rotated.h \
    draw/x11/xa_draw_x11.c \
    draw/x11/xa_draw_x11.h \
    ui/motif/bulletin_gui.c \
    ui/motif/bulletin_gui.h \
    ui/motif/cad_objects_gui.c \
    ui/motif/cad_objects_gui.h \
    ui/motif/db_gui.c \
    ui/motif/db_gui.h \
    ui/motif/draw_symbols_gui.c \
    ui/motif/draw_symbols_gui.h \
    ui/motif/geocoder_gui.c \
    ui/motif/interface_gui.c \
    ui/motif/interface_gui.h \
    ui/motif/list_gui.c \
    ui/motif/list_gui.h \
    ui/motif/locate_gui.c \
    ui/motif/location_gui.c \
    ui/motif/main.c \
    ui/motif/main_gui.h \
    ui/motif/maps_gui.c \
    ui/motif/maps_gui.h \
    ui/motif/messages_gui.c \
    ui/motif/messages_gui.h \
    ui/motif/objects_gui.c \
    ui/motif/objects_gui.h \
    ui/motif/popup.h \
    ui/motif/popup_gui.c \
    ui/motif/track_gui.c \
    ui/motif/track_gui.h \
    ui/motif/view_message_gui.c \
    ui/motif/wx_gui.c \
    ui/motif/wx_gui.h \
    ui/motif/astir_gui.h

# Conditionally compiled sources
if HAVE_NOMINATIM
NOMINATIM_SRC = core/map/nominatim.c core/map/nominatim.h
else
NOMINATIM_SRC =
endif

astir_SOURCES = $(ASTIR_SRC) $(NOMINATIM_SRC) core/util/compiledate.c

astir_udp_client_SOURCES = \
    apps/astir_udp_client.c

astir_testdbfawk_SOURCES = \
    apps/testdbfawk.c \
    core/map/awk.c \
    core/map/dbfawk.c \
    core/util/rpl_malloc.c core/util/rpl_malloc.h

.PHONY: core/util/compiledate.c

core/util/compiledate.c: $(ASTIR_SRC)
	rm -f core/util/compiledate.c core/util/compiledate.$(OBJEXT)
	echo 'char gitstring[] = "'"`$(top_srcdir)/scripts/AstirGitStamp.sh $(top_srcdir)`"'";' >> core/util/compiledate.c

remove_compiledate:
	rm -f core/util/compiledate.c core/util/compiledate.$(OBJEXT)

astir_LDADD=-Lrtree -lrtree

astir_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

astir_udp_client_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@

astir_testdbfawk_LINK=$(CC) $(AM_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@
