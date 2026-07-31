#!/bin/bash
# Turn an OpenStreetMap extract into shapefiles Astir already knows how to draw.
#
# Astir renders vector maps well: shapefiles, styled per feature by dbfawk
# rules, drawn as real lines and fills that stay sharp at any zoom.  What it has
# not had is a source of that data with street-level detail -- the shipped
# Natural Earth set is the 1:50,000,000 world map, which at a city zoom can
# offer a coastline and nothing else.
#
# OSM has the detail.  This converts an extract into the format the existing
# driver reads, so no new map driver, no new styling engine and no network at
# render time.  That last part is the point for a radio application: a map that
# needs the internet is a map you do not have during the emergency you brought
# the radio for.
#
# The layers are split by what they are rather than by OSM's own geometry
# tables, because dbfawk picks its rules from the DBF field signature -- the
# colon-joined field names -- so a file has to have one stable set of fields to
# be styleable at all.
#
#   ./tools/osm_import.sh <extract.osm.pbf|.osm> [outdir]
#
# outdir defaults to $ASTIR_DATA_BASE/maps/OSM, so the result is selectable in
# the map chooser as soon as the index is rebuilt.
#
# Extracts come from a provider such as Geofabrik, per region.  A US state is
# a few hundred MB and converts in minutes; a whole continent is not the place
# to start.
set -eu

SRC="${1:-}"
OUT="${2:-}"

if [ -z "$SRC" ] || [ ! -f "$SRC" ]; then
  echo "usage: $0 <extract.osm.pbf|.osm> [outdir]" >&2
  exit 2
fi

if ! command -v ogr2ogr >/dev/null; then
  echo "ogr2ogr not found.  Install GDAL (package 'gdal' or 'gdal-bin')." >&2
  exit 3
fi

if [ -z "$OUT" ]; then
  BASE="${ASTIR_DATA_BASE:-$HOME/.local/share/astir}"
  OUT="$BASE/maps/OSM"
fi
mkdir -p "$OUT"

echo "converting $SRC -> $OUT"

# Each layer selects its fields explicitly.  Letting ogr2ogr emit the driver's
# default columns would make the DBF signature depend on the GDAL version, and
# the dbfawk rule that styles the result is matched by exactly that signature.
#
# Field names stay within the 10 characters a DBF allows, so nothing is
# silently truncated into a different signature.

emit () {
  local name="$1" sql="$2"
  echo "  $name"
  rm -f "$OUT/$name".{shp,shx,dbf,prj,cpg}
  ogr2ogr -f "ESRI Shapefile" "$OUT/$name.shp" "$SRC" \
          -dialect SQLITE -sql "$sql" -skipfailures 2>/dev/null || {
    echo "    (no features, or the extract has no such layer)"
    return 0
  }
  if [ ! -f "$OUT/$name.shp" ]; then
    echo "    no geometry written -- nothing matched"
    rm -f "$OUT/$name.dbf"
    return 0
  fi
  # The DBF signature is what dbfawk matches on, so print it: a rule whose
  # dbfinfo does not match this string exactly will simply never fire, silently.
  echo -n "    signature: "
  ogrinfo -so "$OUT/$name.shp" "$name" 2>/dev/null \
    | sed -n 's/^\([A-Za-z_][A-Za-z0-9_]*\): \(String\|Integer\|Real\).*/\1/p' \
    | paste -sd: -
}

# Roads.  z_order is GDAL's own importance ranking, which is exactly what a
# display-level rule wants and saves classifying highway= values twice.
emit osm_roads \
  "SELECT geometry, highway AS highway, name AS name, CAST(z_order AS integer) AS z_order
     FROM lines WHERE highway IS NOT NULL"

# Water bodies, as areas.
emit osm_water \
  "SELECT geometry, COALESCE(\"natural\", waterway, landuse) AS water, name AS name
     FROM multipolygons
    WHERE \"natural\" IN ('water','bay','strait')
       OR waterway IS NOT NULL
       OR landuse = 'reservoir'"

# Waterways as lines: rivers and streams are not areas at most scales.
emit osm_rivers \
  "SELECT geometry, waterway AS waterway, name AS name
     FROM lines WHERE waterway IS NOT NULL"

# Land use and green space, which is what stops a city being a blank field.
emit osm_landuse \
  "SELECT geometry, COALESCE(landuse, leisure) AS landuse, name AS name
     FROM multipolygons
    WHERE landuse IS NOT NULL OR leisure IN ('park','nature_reserve','golf_course')"

# Place names, for labels.
emit osm_places \
  "SELECT geometry, place AS place, name AS name,
          CAST(COALESCE(population,'0') AS integer) AS population
     FROM points WHERE place IS NOT NULL AND name IS NOT NULL"

echo
echo "done.  In Astir: Maps -> reload the index, then select the OSM/ entries."
echo "Styling rules live in config/osm_*.dbfawk and are matched by DBF signature."
