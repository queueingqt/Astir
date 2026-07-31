/*
 * The dbfawk styling engine, as an interface any feature source can use.
 *
 * dbfawk decides what a map feature looks like -- colour, width, fill, and the
 * zoom range it appears in -- from its attributes.  It was written for
 * shapefiles and its state lives in map_shp.c, because dbfawk's symbol table is
 * built once and recycled and the variables the rules write into therefore have
 * to be shared.
 *
 * A second feature source needs the same engine and the same values.  Reaching
 * into another translation unit's file statics is not a way to get them, and
 * neither is including map_shp_fwd.h, which is full of shapefile internals and
 * names X11 types.  This is the interface, and it depends on nothing.
 */
#ifndef ASTIR_MAP_STYLE_H
#define ASTIR_MAP_STYLE_H

/*
 * What a rule decided, snapshotted after it ran.
 *
 * `name` points at the engine's shared label buffer and is only valid until
 * the next feature is styled.
 */
typedef struct
{
  int color, lanes, filled, pattern;
  int display_level, min_display_level, label_level;
  int fill_style, fill_color, fill_stipple;
  int label_color, font_size;
  const char *name;
} map_style;

/*
 * The loaded rule set, as a dbfawk_sig_info *.
 *
 * void * so that a caller who only wants to style features does not have to
 * pull in dbfawk.h and shapelib behind it; the one caller that needs the real
 * type casts it.
 */
void *map_dbfawk_sigs(void);

// Build the shared symbol table if no map has been drawn yet.
void map_dbfawk_init_symtab(void);

/*
 * Compile a rule set against the shared symbol table.
 *
 * Must happen before the rule is executed: an uncompiled program has no symbol
 * list to look names up in, and running one crashes rather than complaining.
 * Returns negative on failure.
 */
int map_dbfawk_compile(void *sig_info);

// The style values left by the last rule that ran.
map_style map_dbfawk_style(void);

#endif /* ASTIR_MAP_STYLE_H */
