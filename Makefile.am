
#
# Copyright (C) 2000-2026 The Xastir Group
#

AUTOMAKE_OPTIONS = gnu dist-bzip2
ACLOCAL_AMFLAGS = -I m4

D=      `date +%G%m%d.%H%M%S`

SUBDIRS = src config help scripts online-maps symbols callpass tests

# DISTCLEANFILES =

EXTRA_DIST = config.guess config.sub install-sh \
        missing placeholder FAQ LICENSE README README.GIT.md \
        README.CYGWIN CONTRIBUTING.md \
        README.MAPS README.OSM_maps README.sudo astir_testdbfawk.1 FORK.md \
        DEBUG_LEVELS bootstrap.sh INSTALL.md astir_callpass.1 astir.1 astir_udp_client.1 \
        worldhi.map \
        Davis LaCrosse \
        OSM_template \
        CC_OpenStreetMap_logo.png CC_OpenStreetMap_txt.png \
        COPYING COPYING.LIB.LESSTIF AUTHORS NaturalEarthVector

MAINTAINERCLEANFILES = configure aclocal.m4 Makefile.in Makefile \
        config.guess config.sub install-sh missing mkinstalldirs

countydir=${pkgdatadir}/Counties
county_DATA=placeholder

fccdir=${pkgdatadir}/fcc
fcc_DATA=placeholder

gnisdir=${pkgdatadir}/GNIS
gnis_DATA=placeholder

mapdir=${pkgdatadir}/maps
map_DATA=worldhi.map CC_OpenStreetMap_logo.png CC_OpenStreetMap_txt.png

onlinedir=${pkgdatadir}/maps/Online
online_DATA=OSM_tiled_cycle.geo OSM_tiled_mapnik.geo \
        OSM_tiled_fosm.geo

naturalearthmapdir=${pkgdatadir}/maps/NaturalEarth
naturalearthmap_DATA=NaturalEarthVector/LICENSE.md NaturalEarthVector/README NaturalEarthVector/ne_50m_admin_0_countries_lakes.README.html NaturalEarthVector/ne_50m_admin_0_countries_lakes.VERSION.txt NaturalEarthVector/ne_50m_admin_0_countries_lakes.cpg NaturalEarthVector/ne_50m_admin_0_countries_lakes.dbf NaturalEarthVector/ne_50m_admin_0_countries_lakes.dbfawk NaturalEarthVector/ne_50m_admin_0_countries_lakes.prj NaturalEarthVector/ne_50m_admin_0_countries_lakes.shp NaturalEarthVector/ne_50m_admin_0_countries_lakes.shx NaturalEarthVector/ne_50m_coastline.README.html NaturalEarthVector/ne_50m_coastline.VERSION.txt NaturalEarthVector/ne_50m_coastline.cpg NaturalEarthVector/ne_50m_coastline.dbf NaturalEarthVector/ne_50m_coastline.dbfawk NaturalEarthVector/ne_50m_coastline.prj NaturalEarthVector/ne_50m_coastline.shp NaturalEarthVector/ne_50m_coastline.shx NaturalEarthVector/ne_50m_lakes.README.html NaturalEarthVector/ne_50m_lakes.VERSION.txt NaturalEarthVector/ne_50m_lakes.cpg NaturalEarthVector/ne_50m_lakes.dbf NaturalEarthVector/ne_50m_lakes.dbfawk NaturalEarthVector/ne_50m_lakes.prj NaturalEarthVector/ne_50m_lakes.shp NaturalEarthVector/ne_50m_lakes.shx NaturalEarthVector/ne_50m_rivers_lake_centerlines.README.html NaturalEarthVector/ne_50m_rivers_lake_centerlines.VERSION.txt NaturalEarthVector/ne_50m_rivers_lake_centerlines.cpg NaturalEarthVector/ne_50m_rivers_lake_centerlines.dbf NaturalEarthVector/ne_50m_rivers_lake_centerlines.dbfawk NaturalEarthVector/ne_50m_rivers_lake_centerlines.prj NaturalEarthVector/ne_50m_rivers_lake_centerlines.shp NaturalEarthVector/ne_50m_rivers_lake_centerlines.shx

gpsdir=${pkgdatadir}/maps/GPS
gps_DATA=placeholder

sounddir=${pkgdatadir}/sounds
sound_DATA=placeholder

docdir=@docdir@
doc_DATA=AUTHORS FAQ ChangeLog LICENSE README README.GIT.md README.CYGWIN \
        README.MAPS README.sudo COPYING INSTALL.md \
        README.OSM_maps CONTRIBUTING.md FORK.md \
        COPYING.LIB.LESSTIF

install-data-hook:
	cd $(DESTDIR)$(mapdir) && \
	rm -f CC_OpenStreetMap.png && \
	$(LN_S) CC_OpenStreetMap_logo.png CC_OpenStreetMap.png
 
# Desktop integration.
#
# Upstream ships no .desktop file -- packagers add one downstream, which is why
# there has never been one in the tree and why Xastir has no menu entry unless
# your distribution made one.  This is Astir's, and it is installed by the
# build so a source install lands in the menu like any other application.
#
# The icon is installed as SVG into hicolor/scalable first.  symbols/icon.svg
# already exists and nothing was using it; a scalable icon is the correct thing
# for a menu that renders at whatever size the panel asks for, and it is the
# same argument as the rest of the vector work.  The 128px PNG goes in as well,
# for anything that cannot read SVG.
#
# StartupWMClass is "Astir", matching the application class main.c passes to
# XtVaOpenApplication.  Without it the taskbar shows a running window that it
# cannot connect to the launcher that started it.
desktopdir = $(datadir)/applications
dist_desktop_DATA = astir.desktop

svgicondir = $(datadir)/icons/hicolor/scalable/apps
pngicondir = $(datadir)/icons/hicolor/128x128/apps

install-data-local: install-astir-icons
install-astir-icons:
	$(MKDIR_P) $(DESTDIR)$(svgicondir) $(DESTDIR)$(pngicondir)
	$(INSTALL_DATA) $(top_srcdir)/symbols/icon.svg $(DESTDIR)$(svgicondir)/astir.svg
	$(INSTALL_DATA) $(top_srcdir)/symbols/icon.png $(DESTDIR)$(pngicondir)/astir.png

uninstall-local: uninstall-astir-icons
uninstall-astir-icons:
	rm -f $(DESTDIR)$(svgicondir)/astir.svg $(DESTDIR)$(pngicondir)/astir.png

.PHONY: install-astir-icons uninstall-astir-icons

mandir=@mandir@
man_MANS=astir.1 astir_callpass.1 astir_udp_client.1 astir_testdbfawk.1

# Use tiles for these maps
OSM_tiled_cycle.geo: OSM_template
	sed -e '/THIS IS A TEMPLATE FILE/d' -e's/^#OSM_TILED_MAP/OSM_TILED_MAP/' \
	    -e's!^#URL tileURL!URL http://tile.opencyclemap.org/cycle/!' \
	    -e 's/-STYLE/-cycle/' \
	    < $(top_srcdir)/OSM_template >$@

OSM_tiled_mapnik.geo: OSM_template
	sed -e '/THIS IS A TEMPLATE FILE/d' -e's/^#OSM_TILED_MAP/OSM_TILED_MAP/' \
	    -e's!^#URL tileURL!URL https://tile.openstreetmap.org/!' \
	    -e 's/-STYLE/-mapnik/' \
	    < $(top_srcdir)/OSM_template >$@

OSM_tiled_fosm.geo: OSM_template
	sed -e '/THIS IS A TEMPLATE FILE/d' -e's/^#OSM_TILED_MAP/OSM_TILED_MAP/' \
	    -e's!^#URL tileURL!URL https://map.fosm.org/default/!' \
	    -e 's/-STYLE/-fosm/' \
	    < $(top_srcdir)/OSM_template >$@

ChangeLog::


