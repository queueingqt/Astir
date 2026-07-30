/*
 * xa_gtk4_palette.c -- Xastir's colour table, by name.
 *
 * Generated from the 148 GetPixelByName() calls in main.c, because the values
 * are not cosmetic: core drawing code indexes colors[] by number -- colors[0xfd]
 * is the map background, the map drivers pick specific slots -- so a map drawn
 * with a different palette is a different map.  The mechanism is modern (one
 * table, resolved through xa_color_by_name), the values are the old ones exactly.
 */

#include "xa_draw.h"

typedef struct { int slot; const char *name; } xa_palette_entry;

static const xa_palette_entry xa_palette[] =
{
  { 0x00, "DarkGreen" },
  { 0x01, "purple" },
  { 0x02, "DarkGreen" },
  { 0x03, "cyan" },
  { 0x04, "brown" },
  { 0x05, "plum" },
  { 0x06, "orange" },
  { 0x07, "darkgray" },
  { 0x08, "black" },
  { 0x09, "blue" },
  { 0x0a, "green" },
  { 0x0b, "mediumorchid" },
  { 0x0c, "red" },
  { 0x0d, "magenta" },
  { 0x0e, "yellow" },
  { 0x0f, "white" },
  { 0x10, "black" },
  { 0x11, "black" },
  { 0x12, "black" },
  { 0x13, "black" },
  { 0x14, "lightgray" },
  { 0x15, "magenta" },
  { 0x16, "mediumorchid" },
  { 0x17, "lightblue" },
  { 0x18, "purple" },
  { 0x19, "orange2" },
  { 0x1a, "SteelBlue" },
  { 0x20, "white" },
  { 0x21, "black" },
  { 0x22, "blue" },
  { 0x23, "green" },
  { 0x24, "cyan3" },
  { 0x25, "red" },
  { 0x26, "magenta" },
  { 0x27, "yellow" },
  { 0x28, "gray35" },
  { 0x29, "gray27" },
  { 0x2a, "blue4" },
  { 0x2b, "green4" },
  { 0x2c, "cyan4" },
  { 0x2d, "red4" },
  { 0x2e, "magenta4" },
  { 0x2f, "yellow4" },
  { 0x30, "gray53" },
  { 0x40, "yellow" },
  { 0x41, "DarkOrange3" },
  { 0x42, "purple" },
  { 0x43, "gray80" },
  { 0x44, "red3" },
  { 0x45, "brown1" },
  { 0x46, "brown3" },
  { 0x47, "blue4" },
  { 0x48, "DeepSkyBlue" },
  { 0x49, "DarkGreen" },
  { 0x4a, "red2" },
  { 0x4b, "green3" },
  { 0x4c, "MediumBlue" },
  { 0x4d, "white" },
  { 0x4e, "gray53" },
  { 0x4f, "gray35" },
  { 0x50, "gray27" },
  { 0x51, "black" },
  { 0x52, "LimeGreen" },
  { 0x60, "HotPink" },
  { 0x61, "RoyalBlue" },
  { 0x62, "orange3" },
  { 0x63, "yellow3" },
  { 0x64, "ForestGreen" },
  { 0x65, "DodgerBlue" },
  { 0x66, "cyan2" },
  { 0x67, "plum2" },
  { 0x68, "MediumBlue" },
  { 0x69, "gray86" },
  { 0x6a, "tgr_prird_1" },
  { 0x6b, "tgr_secrd_1" },
  { 0x70, "RosyBrown2" },
  { 0x71, "gray81" },
  { 0x72, "tgr_park_1" },
  { 0x73, "tgr_city_1" },
  { 0x74, "tgr_forest_1" },
  { 0x75, "tgr_water_1" },
  { 0x76, "cividis_1" },
  { 0x77, "cividis_2" },
  { 0x78, "cividis_3" },
  { 0x79, "cividis_4" },
  { 0x7a, "cividis_5" },
  { 0x7b, "cividis_6" },
  { 0x7c, "cividis_7" },
  { 0x7d, "cividis_8" },
  { 0x7e, "cividis_9" },
  { 0x7f, "set1_1" },
  { 0x80, "set1_2" },
  { 0x81, "set1_3" },
  { 0x82, "set1_4" },
  { 0x83, "set1_5" },
  { 0x84, "set1_6" },
  { 0x85, "set1_7" },
  { 0x86, "set1_8" },
  { 0x87, "set1_9" },
  { 0xfd, "gray73" },
  { 0xfe, "pink" },
  { 0xff, "gray73" },
};

static const xa_palette_entry xa_trail_palette[] =
{
  { 0x00, "yellow" },
  { 0x01, "blue" },
  { 0x02, "green" },
  { 0x03, "red" },
  { 0x04, "magenta" },
  { 0x05, "black" },
  { 0x06, "white" },
  { 0x07, "DarkOrchid" },
  { 0x08, "purple" },
  { 0x09, "OrangeRed" },
  { 0x0a, "brown" },
  { 0x0b, "DarkGreen" },
  { 0x0c, "MediumBlue" },
  { 0x0d, "ForestGreen" },
  { 0x0e, "chartreuse" },
  { 0x0f, "cornsilk" },
  { 0x10, "LightCyan" },
  { 0x11, "cyan" },
  { 0x12, "DarkSlateGray" },
  { 0x13, "NavyBlue" },
  { 0x14, "DarkOrange3" },
  { 0x15, "gray27" },
  { 0x16, "RoyalBlue" },
  { 0x17, "yellow2" },
  { 0x18, "DodgerBlue" },
  { 0x19, "cyan2" },
  { 0x1a, "MediumBlue" },
  { 0x1b, "gray86" },
  { 0x1c, "SteelBlue" },
  { 0x1d, "PaleGreen" },
  { 0x1e, "RosyBrown" },
  { 0x1f, "DeepSkyBlue" },
};

void xa_gtk4_load_palette(void)
{
  unsigned i;

  for (i = 0; i < sizeof(xa_palette) / sizeof(xa_palette[0]); i++)
  {
    colors[xa_palette[i].slot] = xa_color_by_name(xa_palette[i].name);
  }
  for (i = 0; i < sizeof(xa_trail_palette) / sizeof(xa_trail_palette[0]); i++)
  {
    trail_colors[xa_trail_palette[i].slot] =
      xa_color_by_name(xa_trail_palette[i].name);
  }
}
