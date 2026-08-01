/*
 *
 * ASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 2025-2026 The Xastir Group
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Look at the README for more information on the program.
 */

/*
 * Test program for object_utils.c functions
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

#include "tests/test_framework.h"

#include "util.h"
#include "globals.h"

extern long scale_x, scale_y;
extern long center_longitude, center_latitude;
extern long NW_corner_longitude, NW_corner_latitude;
extern long SE_corner_longitude, SE_corner_latitude;
extern long screen_height, screen_width;

int test_convert_lat_l2s_basic(void)
{
  long lat;
  char lat_str[20];
  // Compute Astir coordinates for 35d01.631'N
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lat = 90*60*60*100-(35*60+1.631)*60*100;
  convert_lat_l2s(lat,lat_str, sizeof(lat_str),CONVERT_HP_NOSP);
  TEST_ASSERT_STR_EQ("3501.631N",lat_str,"astir y value correctly converted to string");
  TEST_PASS("convert_lat_l2s: correct");
}
int test_convert_lon_l2s_basic(void)
{
  long lon;
  char lon_str[20];
  // Compute Astir coordinates for 106d12.385'W
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lon = 180*60*60*100-(106*60+12.385)*60*100;
  convert_lon_l2s(lon,lon_str, sizeof(lon_str),CONVERT_HP_NOSP);
  TEST_ASSERT_STR_EQ("10612.385W",lon_str,"astir x value correctly converted to string");
  TEST_PASS("convert_lon_l2s: correct");
}
int test_convert_lat_l2s_basic_s(void)
{
  long lat;
  char lat_str[20];
  // Compute Astir coordinates for 35d01.631'S
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lat = 90*60*60*100+(35*60+1.631)*60*100;
  convert_lat_l2s(lat,lat_str, sizeof(lat_str),CONVERT_HP_NOSP);
  TEST_ASSERT_STR_EQ("3501.631S",lat_str,"astir y value correctly converted to string");
  TEST_PASS("convert_lat_l2s: correct");
}
int test_convert_lon_l2s_basic_e(void)
{
  long lon;
  char lon_str[20];
  // Compute Astir coordinates for 106d12.385'E
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lon = 180*60*60*100+(106*60+12.385)*60*100;
  convert_lon_l2s(lon,lon_str, sizeof(lon_str),CONVERT_HP_NOSP);
  TEST_ASSERT_STR_EQ("10612.385E",lon_str,"astir x value correctly converted to string");
  TEST_PASS("convert_lon_l2s: correct");
}
int test_convert_lat_l2s_lp(void)
{
  long lat;
  char lat_str[20];
  // Compute Astir coordinates for 35d01.631'N
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lat = 90*60*60*100-(35*60+1.631)*60*100;
  convert_lat_l2s(lat,lat_str, sizeof(lat_str),CONVERT_LP_NOSP);
  TEST_ASSERT_STR_EQ("3501.63N",lat_str,"astir y value correctly converted to string");
  TEST_PASS("convert_lat_l2s: correct");
}
int test_convert_lon_l2s_lp(void)
{
  long lon;
  char lon_str[20];
  // Compute Astir coordinates for 106d12.384'W
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  // we're using ".384" here because ".385" would get rounded up, not truncated.
  lon = 180*60*60*100-(106*60+12.384)*60*100;
  convert_lon_l2s(lon,lon_str, sizeof(lon_str),CONVERT_LP_NOSP);
  TEST_ASSERT_STR_EQ("10612.38W",lon_str,"astir x value correctly converted to string");
  TEST_PASS("convert_lon_l2s: correct");
}
int test_convert_lat_s2l_basic(void)
{
  long lat;
  long lat_expect;
  // Compute Astir coordinates for 35d01.631'N
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lat_expect = 90*60*60*100-(35*60+1.631)*60*100;
  lat = convert_lat_s2l("3501.631N");
  TEST_ASSERT(lat==lat_expect,"astir y value correctly converted from string");
  TEST_PASS("convert_lat_s2l: correct");
}
int test_convert_lon_s2l_basic(void)
{
  long lon;
  long lon_expect;
  // Compute Astir coordinates for 106d12.385'W
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lon_expect = 180*60*60*100-(106*60+12.385)*60*100;
  lon = convert_lon_s2l("10612.385W");
  TEST_ASSERT(lon==lon_expect,"astir x value correctly converted from string");
  TEST_PASS("convert_lon_s2l: correct");
}
int test_convert_lat_s2l_basic_s(void)
{
  long lat;
  long lat_expect;
  // Compute Astir coordinates for 35d01.631'S
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lat_expect = 90*60*60*100+(35*60+1.631)*60*100;
  lat = convert_lat_s2l("3501.631S");
  TEST_ASSERT(lat==lat_expect,"astir y value correctly converted from string");
  TEST_PASS("convert_lat_s2l: correct");
}
int test_convert_lon_s2l_basic_e(void)
{
  long lon;
  long lon_expect;
  // Compute Astir coordinates for 106d12.385'E
  // Astir coordinates are in hundredths of seconds, with 0 being 90d N
  lon_expect = 180*60*60*100+(106*60+12.385)*60*100;
  lon = convert_lon_s2l("10612.385E");
  TEST_ASSERT(lon==lon_expect,"astir x value correctly converted from string");
  TEST_PASS("convert_lon_s2l: correct");
}

// Check that s2l->l2s gives back what we started with.
int test_s2l_l2s_consistency(void)
{
  long lon;
  long lat;
  char lon_s[10+1];
  char lat_s[9+1];
  lon=convert_lon_s2l("10612.385W");
  lat=convert_lat_s2l("3501.631N");
  convert_lon_l2s(lon,lon_s,sizeof(lon_s),CONVERT_HP_NOSP);
  convert_lat_l2s(lat,lat_s,sizeof(lat_s),CONVERT_HP_NOSP);

  TEST_ASSERT_STR_EQ("3501.631N",lat_s,"Round-trip latitude consistent");
  TEST_ASSERT_STR_EQ("10612.385W",lon_s,"Round-trip longitude consistent");
  TEST_PASS("convert_lon_s2l and back: correct");
}
// Check that l2s->s2l gives back what we started with.
int test_l2s_s2l_consistency(void)
{
  long lon;
  long lat;
  long lon_return;
  long lat_return;
  char lon_s[10+1];
  char lat_s[9+1];
  lon = 180*60*60*100-(106*60+12.385)*60*100;
  lat = 90*60*60*100-(35*60+1.631)*60*100;
  convert_lon_l2s(lon,lon_s,sizeof(lon_s),CONVERT_HP_NOSP);
  convert_lat_l2s(lat,lat_s,sizeof(lat_s),CONVERT_HP_NOSP);
  lat_return = convert_lat_s2l(lat_s);
  lon_return = convert_lon_s2l(lon_s);
  TEST_ASSERT(lat==lat_return,"Round-trip latitude consistent");
  TEST_ASSERT(lon==lon_return,"Round-trip longitude consistent");
  TEST_PASS("convert_lon_s2l and back: correct");
}

// test screen/astir/other converters
int test_convert_screen_to_astir_coordinates(void)
{
  // presume screen to be 1900x712 pixels
  // NW corner 3515.704N 10706.340W
  // SW corner 3454.727N 10548.923W
  // the "long" coords are in centi-seconds (1/100 second)
  // scale_x and scale_y are centi-seconds per pixel
  long lon_xa, lat_xa;
  long screen_x, screen_y;

  screen_width=1900;
  screen_height=712;
  NW_corner_longitude = convert_lon_s2l("10706.340W");
  NW_corner_latitude  = convert_lat_s2l("3515.704N");
  SE_corner_longitude = convert_lon_s2l("10548.923W");
  SE_corner_latitude  = convert_lat_s2l("3454.727N");
  // Remember that Astir coords are 0,0 at 90N 180W and increase as we
  // go east and south.
  scale_x = (SE_corner_longitude - NW_corner_longitude)/screen_width;
  scale_y = (SE_corner_latitude - NW_corner_latitude)/screen_height;
  center_latitude = (NW_corner_latitude + SE_corner_latitude)/2;
  center_longitude = (NW_corner_longitude + SE_corner_longitude)/2;

  // Now, Astir itself actually makes the center lat/lon the primary
  // variable, and recomputes NW and SW based on that and the scale.  Let's
  // do that ourselves now.  Otherwise we get rounding problems later.

  NW_corner_longitude = center_longitude - (screen_width*scale_x)/2;
  NW_corner_latitude  = center_latitude  - (screen_height*scale_y)/2;
  SE_corner_longitude = center_longitude + (screen_width*scale_x)/2;
  SE_corner_latitude  = center_latitude  + (screen_height*scale_y)/2;

  convert_screen_to_astir_coordinates(screen_width/2, screen_height/2,
                                       &lat_xa, &lon_xa);
  TEST_ASSERT(lon_xa == center_longitude, "Center pixel mapped correctly to center longitude");
  TEST_ASSERT(lat_xa == center_latitude, "Center pixel mapped correctly to center latitude");

  // Now the NW corner
  convert_screen_to_astir_coordinates(0,0,
                                       &lat_xa, &lon_xa);

  TEST_ASSERT(lon_xa == NW_corner_longitude, "Top left pixel mapped correctly to NW corner longitude");
  TEST_ASSERT(lat_xa == NW_corner_latitude, "top left pixel mapped correctly to NW corner latitude");

  // now the SE corner
  convert_screen_to_astir_coordinates(screen_width, screen_height,
                                       &lat_xa, &lon_xa);
  TEST_ASSERT(lon_xa == SE_corner_longitude, "Bottom right pixel mapped correctly to SE corner longitude");
  TEST_ASSERT(lat_xa == SE_corner_latitude, "Bottom right pixel mapped correctly to SE corner latitude");

  TEST_PASS("convert_screen_to_astir_coordinates: works as expected");
}

// Note that this is *identical* to the previous function except it
// calls convert_astir_to_screen_coordinates instead of having hard-coded
// junk in it.
int test_convert_astir_to_screen_coordinates(void)
{
  // presume screen to be 1900x712 pixels
  // NW corner 3515.704N 10706.340W
  // SW corner 3454.727N 10548.923W
  // the "long" coords are in centi-seconds (1/100 second)
  // scale_x and scale_y are centi-seconds per pixel
  long screen_x, screen_y;

  screen_width=1900;
  screen_height=712;
  NW_corner_longitude = convert_lon_s2l("10706.340W");
  NW_corner_latitude  = convert_lat_s2l("3515.704N");
  SE_corner_longitude = convert_lon_s2l("10548.923W");
  SE_corner_latitude  = convert_lat_s2l("3454.727N");
  // Remember that Astir coords are 0,0 at 90N 180W and increase as we
  // go east and south.
  scale_x = (SE_corner_longitude - NW_corner_longitude)/screen_width;
  scale_y = (SE_corner_latitude - NW_corner_latitude)/screen_height;
  center_latitude = (NW_corner_latitude + SE_corner_latitude)/2;
  center_longitude = (NW_corner_longitude + SE_corner_longitude)/2;

  // Now, Astir itself actually makes the center lat/lon the primary
  // variable, and recomputes NW and SW based on that and the scale.  Let's
  // do that ourselves now.  Otherwise we get rounding problems later.

  NW_corner_longitude = center_longitude - (screen_width*scale_x)/2;
  NW_corner_latitude  = center_latitude  - (screen_height*scale_y)/2;
  SE_corner_longitude = center_longitude + (screen_width*scale_x)/2;
  SE_corner_latitude  = center_latitude  + (screen_height*scale_y)/2;

  // Now try to convert this back to screen coords using the
  // utility function
  convert_astir_to_screen_coordinates(center_longitude, center_latitude, &screen_x, &screen_y);

  // We expect these screen coords to be screen_width/2, screen_height/2
  TEST_ASSERT(screen_x == screen_width/2, "center lon maps onto center pixel");
  TEST_ASSERT(screen_y == screen_height/2, "center lat maps onto center pixel.");

  // Now what about NW and SE corners?

  convert_astir_to_screen_coordinates(NW_corner_longitude, NW_corner_latitude, &screen_x, &screen_y);
  // We expect these screen coords to be 0,0
  TEST_ASSERT(screen_x == 0, "NW corner lon maps onto top left pixel");
  TEST_ASSERT(screen_y == 0, "NW corner lat maps onto top left pixel.");

  // now the SE corner

  convert_astir_to_screen_coordinates(SE_corner_longitude, SE_corner_latitude, &screen_x, &screen_y);

  // We expect these screen coords to be screen_width, screen_height
  TEST_ASSERT(screen_x == screen_width, "SE corner lon maps onto bot right pixel");
  TEST_ASSERT(screen_y == screen_height, "SE corner lat maps onto bot right pixel.");

  TEST_PASS("convert_astir_to_screen_coordinates: works as expected");
}

int test_short_filename_for_status_notrunc(void)
{
  char filename[MAX_FILENAME]="this_is_short.shp";
  char short_filename[MAX_FILENAME];

  short_filename_for_status(filename, short_filename, sizeof(short_filename));

  TEST_ASSERT_STR_EQ("this_is_short.shp", short_filename, "Name not truncated if already short enough");
  TEST_PASS("short_filename_for_status");
}
int test_short_filename_for_status_trunc(void)
{
  char filename[MAX_FILENAME]="/a/long/path/with/lots/of/components/basename.shp";
  char short_filename[MAX_FILENAME];

  short_filename_for_status(filename, short_filename, sizeof(short_filename));

  TEST_ASSERT_STR_EQ("..ots/of/components/basename.shp", short_filename, "Name truncated if long");
  TEST_PASS("short_filename_for_status");
}

int test_copy_token_plain(void)
{
  char dest[16];
  int len;

  len = copy_token(dest, sizeof(dest), "png");

  TEST_ASSERT_STR_EQ("png", dest, "Plain token copied unchanged");
  TEST_ASSERT(len == 3, "Return value is the token length");
  TEST_PASS("copy_token");
}

int test_copy_token_leading_space(void)
{
  char dest[16];

  copy_token(dest, sizeof(dest), "    png");

  TEST_ASSERT_STR_EQ("png", dest, "Leading whitespace is skipped");
  TEST_PASS("copy_token");
}

int test_copy_token_stops_at_space(void)
{
  char dest[16];
  int len;

  len = copy_token(dest, sizeof(dest), "  png   and more");

  TEST_ASSERT_STR_EQ("png", dest, "Copy stops at the first trailing whitespace");
  TEST_ASSERT(len == 3, "Return value covers only the first token");
  TEST_PASS("copy_token");
}

int test_copy_token_exact_fit(void)
{
  char dest[4];
  int len;

  len = copy_token(dest, sizeof(dest), "png");

  TEST_ASSERT_STR_EQ("png", dest, "Token that exactly fills the buffer is not truncated");
  TEST_ASSERT(len < (int)sizeof(dest), "Exact fit is not reported as truncation");
  TEST_PASS("copy_token");
}

int test_copy_token_truncates(void)
{
  char dest[4];
  char canary[8];
  int len;

  memset(canary, 'x', sizeof(canary));

  len = copy_token(dest, sizeof(dest), "abcdefghij");

  TEST_ASSERT_STR_EQ("abc", dest, "Oversized token is truncated to fit the buffer");
  TEST_ASSERT(len == 10, "Return value is the full length the token would have needed");
  TEST_ASSERT(len >= (int)sizeof(dest), "Truncation is detectable from the return value");
  TEST_ASSERT(canary[0] == 'x', "Neighbouring storage is untouched");
  TEST_PASS("copy_token");
}

int test_copy_token_empty(void)
{
  char dest[16];
  int len;

  len = copy_token(dest, sizeof(dest), "");

  TEST_ASSERT_STR_EQ("", dest, "Empty input yields an empty string");
  TEST_ASSERT(len == 0, "Empty input reports no token");
  TEST_PASS("copy_token");
}

int test_copy_token_whitespace_only(void)
{
  char dest[16];
  int len;

  len = copy_token(dest, sizeof(dest), "     ");

  TEST_ASSERT_STR_EQ("", dest, "Whitespace-only input yields an empty string");
  TEST_ASSERT(len == 0, "Whitespace-only input reports no token");
  TEST_PASS("copy_token");
}

int test_copy_token_null_source(void)
{
  char dest[16];
  int len;

  len = copy_token(dest, sizeof(dest), NULL);

  TEST_ASSERT_STR_EQ("", dest, "NULL source yields an empty string");
  TEST_ASSERT(len == 0, "NULL source reports no token");
  TEST_ASSERT(copy_token(NULL, sizeof(dest), "png") == 0, "NULL destination is rejected");
  TEST_ASSERT(copy_token(dest, 0, "png") == 0, "Zero destination size is rejected");
  TEST_PASS("copy_token");
}

/*
 * loc_to_sec(): Maidenhead locator to coordinates.
 *
 * Checked against places whose grid squares are independently known, not only
 * by round-tripping through sec_to_loc().  A round trip proves the two agree
 * with each other; it cannot notice that both are wrong the same way -- and
 * they share a sign convention and an off-by-one that would survive it.
 */
int test_loc_to_sec_known_places(void)
{
  long lon, lat;

  // CN87 is Seattle: 124W-122W, 47N-48N.  Centre of the square is 123W 47.5N.
  TEST_ASSERT(loc_to_sec("CN87", &lon, &lat) == 1, "CN87 is accepted");
  // Astir longitude counts 1/100 second east from 180W: 123W is 57 deg east.
  TEST_ASSERT(lon == 57L * 3600L * 100L, "CN87 centres on 123W");
  // Astir latitude counts 1/100 second south from 90N: 47.5N is 42.5 south.
  TEST_ASSERT(lat == (2L * 90L * 3600L - 1L - (long)(137.5 * 3600.0)) * 100L,
              "CN87 centres on 47.5N");

  // JO62 is Berlin: 12E-14E, 52N-53N.  Centre 13E 52.5N.
  TEST_ASSERT(loc_to_sec("JO62", &lon, &lat) == 1, "JO62 is accepted");
  TEST_ASSERT(lon == 193L * 3600L * 100L, "JO62 centres on 13E");
  TEST_ASSERT(lat == (2L * 90L * 3600L - 1L - (long)(142.5 * 3600.0)) * 100L,
              "JO62 centres on 52.5N");

  // The south-west corner of the world, where a flipped sign or an off-by-one
  // shows up most clearly.
  TEST_ASSERT(loc_to_sec("AA00", &lon, &lat) == 1, "AA00 is accepted");
  TEST_ASSERT(lon == 3600L * 100L, "AA00 centres one degree east of 180W");
  TEST_ASSERT(lat == (2L * 90L * 3600L - 1L - 1800L) * 100L,
              "AA00 centres half a degree north of 90S");

  TEST_PASS("loc_to_sec");
}


int test_loc_to_sec_six_character(void)
{
  long lon4, lat4, lon6, lat6, lonU, latU;

  // A six-character locator must land inside the four-character square it
  // extends.
  TEST_ASSERT(loc_to_sec("CN87", &lon4, &lat4) == 1, "CN87 is accepted");
  TEST_ASSERT(loc_to_sec("CN87us", &lon6, &lat6) == 1, "CN87us is accepted");

  TEST_ASSERT(lon6 > lon4, "CN87us is east of the centre of CN87");
  TEST_ASSERT(lat6 < lat4, "CN87us is north of the centre of CN87");
  // Still inside: half of two degrees of longitude, half of one of latitude.
  TEST_ASSERT(lon6 - lon4 < 3600L * 100L, "CN87us stays inside CN87");
  TEST_ASSERT(lat4 - lat6 < 1800L * 100L, "CN87us stays inside CN87");

  // Case must not matter, in either direction.
  TEST_ASSERT(loc_to_sec("cn87US", &lonU, &latU) == 1, "mixed case accepted");
  TEST_ASSERT(lonU == lon6 && latU == lat6, "case makes no difference");

  TEST_PASS("loc_to_sec");
}


int test_loc_to_sec_round_trip(void)
{
  static const char *locs[] =
  {
    "CN87us", "JO62qm", "FN20xr", "AA00aa", "RR99xx", "IO91wm", NULL
  };
  long lon, lat;
  int  i;

  for (i = 0; locs[i] != NULL; i++)
  {
    TEST_ASSERT(loc_to_sec(locs[i], &lon, &lat) == 1, "locator is accepted");
    TEST_ASSERT_STR_EQ(locs[i], sec_to_loc(lon, lat),
                       "sec_to_loc returns the locator it came from");
  }
  TEST_PASS("loc_to_sec");
}


int test_loc_to_sec_rejects_rubbish(void)
{
  long lon = 12345, lat = 67890;

  TEST_ASSERT(loc_to_sec(NULL, &lon, &lat) == 0, "NULL is rejected");
  TEST_ASSERT(loc_to_sec("", &lon, &lat) == 0, "empty is rejected");
  TEST_ASSERT(loc_to_sec("CN", &lon, &lat) == 0, "two characters rejected");
  TEST_ASSERT(loc_to_sec("CN8", &lon, &lat) == 0, "three characters rejected");
  TEST_ASSERT(loc_to_sec("CN87u", &lon, &lat) == 0, "five characters rejected");
  TEST_ASSERT(loc_to_sec("CN87usx", &lon, &lat) == 0, "seven rejected");
  TEST_ASSERT(loc_to_sec("SN87", &lon, &lat) == 0, "field past R rejected");
  TEST_ASSERT(loc_to_sec("CNX7", &lon, &lat) == 0, "non-digit square rejected");
  TEST_ASSERT(loc_to_sec("CN87yz", &lon, &lat) == 0, "sub-square past x rejected");
  TEST_ASSERT(loc_to_sec("N0CALL", &lon, &lat) == 0, "a callsign is rejected");

  // Nothing above may have written through the pointers.
  TEST_ASSERT(lon == 12345 && lat == 67890,
              "a rejected locator leaves the coordinates alone");
  TEST_PASS("loc_to_sec");
}


/* Test runner */
typedef struct {
    const char *name;
    int (*func)(void);
} test_case_t;

int main(int argc, char *argv[])
{
  test_case_t tests[] = {
    {"convert_lat_l2s_basic",test_convert_lat_l2s_basic},
    {"convert_lon_l2s_basic",test_convert_lon_l2s_basic},
    {"convert_lat_l2s_basic_s",test_convert_lat_l2s_basic_s},
    {"convert_lon_l2s_basic_e",test_convert_lon_l2s_basic_e},
    {"convert_lat_l2s_lp",test_convert_lat_l2s_lp},
    {"convert_lon_l2s_lp",test_convert_lon_l2s_lp},
    {"convert_lat_s2l_basic",test_convert_lat_s2l_basic},
    {"convert_lon_s2l_basic",test_convert_lon_s2l_basic},
    {"convert_lat_s2l_basic_s",test_convert_lat_s2l_basic_s},
    {"convert_lon_s2l_basic_e",test_convert_lon_s2l_basic_e},
    {"s2l_l2s_consistency",test_s2l_l2s_consistency},
    {"l2s_s2l_consistency",test_l2s_s2l_consistency},
    {"convert_screen_to_astir_coordinates", test_convert_screen_to_astir_coordinates},
    {"convert_astir_to_screen_coordinates", test_convert_astir_to_screen_coordinates},
    {"short_filename_for_status_notrunc",test_short_filename_for_status_notrunc},
    {"short_filename_for_status_trunc",test_short_filename_for_status_trunc},
    {"copy_token_plain",test_copy_token_plain},
    {"copy_token_leading_space",test_copy_token_leading_space},
    {"copy_token_stops_at_space",test_copy_token_stops_at_space},
    {"copy_token_exact_fit",test_copy_token_exact_fit},
    {"copy_token_truncates",test_copy_token_truncates},
    {"copy_token_empty",test_copy_token_empty},
    {"copy_token_whitespace_only",test_copy_token_whitespace_only},
    {"copy_token_null_source",test_copy_token_null_source},
    {"loc_to_sec_known_places",test_loc_to_sec_known_places},
    {"loc_to_sec_six_character",test_loc_to_sec_six_character},
    {"loc_to_sec_round_trip",test_loc_to_sec_round_trip},
    {"loc_to_sec_rejects_rubbish",test_loc_to_sec_rejects_rubbish},
    {NULL,NULL}
  };


  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s <test name>\n", argv[0]);
    fprintf(stderr, "Available tests: \n");
    for (int i = 0; tests[i].name != NULL; i++)
    {
      fprintf(stderr, "  %s\n", tests[i].name);
    }
    return 1;
  }

  const char *test_name = argv[1];

  /* Run the requested test */
  for (int i = 0; tests[i].name != NULL; i++)
  {
    if (strcmp(test_name, tests[i].name) == 0)
    {
      return tests[i].func();
    }
  }

  fprintf(stderr, "Unknown test: %s\n", test_name);
  return 1;
}


