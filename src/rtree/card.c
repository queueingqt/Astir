/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
 *
 * ASTIR, Amateur Station Tracking and Information Reporting
 * Copyright (C) 1999,2000  Frank Giannandrea
 * Copyright (C) 2000-2026 The Xastir Group
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
/****************************************************************************
 * MODULE:       R-Tree library
 *
 * AUTHOR(S):    Antonin Guttman - original code
 *               Melinda Green (melinda@superliminal.com) - major clean-up
 *                               and implementation of bounding spheres
 *
 * PURPOSE:      Multidimensional index
 *
 */

#include "index.h"
#include "card.h"

int Astir_NODECARD = MAXCARD;
int Astir_LEAFCARD = MAXCARD;

static int set_max(int *which, int new_max)
{
  if(2 > new_max || new_max > MAXCARD)
  {
    return 0;
  }
  *which = new_max;
  return 1;
}

int Astir_RTreeSetNodeMax(int new_max)
{
  return set_max(&Astir_NODECARD, new_max);
}
int Astir_RTreeSetLeafMax(int new_max)
{
  return set_max(&Astir_LEAFCARD, new_max);
}
int Astir_RTreeGetNodeMax(void)
{
  return Astir_NODECARD;
}
int Astir_RTreeGetLeafMax(void)
{
  return Astir_LEAFCARD;
}
