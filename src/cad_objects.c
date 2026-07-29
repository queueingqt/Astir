/*
 *
 * XASTIR, Amateur Station Tracking and Information Reporting
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

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "snprintf.h"

#include "xastir.h"
#include "globals.h"
#include "main.h"
#include "xa_config.h"
#include "db_funcs.h"
#include "draw_symbols.h"
#include "maps.h"  // for draw_vector prototype

#include "xa_draw.h"

#include "xa_ui.h"

// Must be last include file
#include "leak_detection.h"

// The dialogs live in cad_objects_gui.c.  What is left here is the model:
// allocate, delete, measure, save, restore, draw.  It needs no widget, no
// fontlist and no dialog position -- when it has changed what should be on
// screen it says so with xa_ui_redraw().

int polygon_last_x = -1;        // Draw CAD Objects functions
int polygon_last_y = -1;        // Draw CAD Objects functions

// Set by the CAD drawing-mode toggle in cad_objects_gui.c, read by main.c to
// route mouse clicks.  A plain flag, so it stays with the model.
int draw_CAD_objects_flag = 0;

void Draw_All_CAD_Objects(Widget w);
void Save_CAD_Objects_to_file(void);
void CAD_object_set_raw_probability(CADRow *object_ptr, float probability, int as_percent);
void Format_area_for_output(double *area_km2, char *area_description, int sizeof_area_description);

int CAD_draw_objects = TRUE;
int CAD_show_label = TRUE;
int CAD_show_raw_probability = TRUE;
int CAD_show_comment = TRUE;
int CAD_show_area = TRUE;

//////////////////// Draw CAD Objects Functions ////////////////////



//#define CAD_DEBUG

// Allocate a new vertice along the polygon.  If the vertice is very
// close to the first vertice, ask the operator if they wish to
// close the polygon.  If closing, ask for a raw probability?
//
// As each vertice is allocated, write it out to file?  We'd then
// need to edit the file and comment vertices out if we're deleting
// vertices in memory.  We could also write out an entire object
// when we select "Close Polygon".
//
void CAD_vertice_allocate(long latitude, long longitude)
{

#ifdef CAD_DEBUG
  fprintf(stderr,"Allocating a new vertice\n");
#endif

  // Check whether a line segment will cross another?

  // We use the CAD_list_head variable, as it will be pointing to
  // the top of the list, where the current object we're working
  // on will be placed.  Check whether that pointer is NULL
  // though, just in case.
  if (CAD_list_head)     // We have at least one object defined
  {
    VerticeRow *p_new;

    // Allocate area to hold the vertice
    p_new = (VerticeRow *)malloc(sizeof(VerticeRow));

    if (!p_new)
    {
      fprintf(stderr,"Couldn't allocate memory in CAD_vertice_allocate()\n");
      return;
    }

    p_new->latitude = latitude;
    p_new->longitude = longitude;

    // Link it in at the top of the vertice chain.
    p_new->next = CAD_list_head->start;
    CAD_list_head->start = p_new;
  }

  // Call redraw_symbols outside this function, as
  // verticies may be allocated both when loading lots of them from a file
  // and when the user is drawing objects in the user interface
  // Reload symbols/tracks/CAD objects
  //redraw_symbols(da);
}





// Allocate a struct for a new object and add one vertice to it.
// When do we name it and place the label?  Assign probability to
// it?  We should keep a pointer to the current polygon we're
// working on, so that we can modify it easily as we draw.
// Actually, it'll be pointed to by CAD_list_head, so we already
// have it!
//
// As each object is allocated, write it out to file?
//
// Compute a default label of date/time?
//
void CAD_object_allocate(long latitude, long longitude)
{
  CADRow *p_new;

#ifdef CAD_DEBUG
  fprintf(stderr,"Allocating a new CAD object\n");
#endif

  // Allocate memory and link it to the top of the singly-linked
  // list of CADRow objects.
  p_new = (CADRow *)malloc(sizeof(CADRow));

  if (!p_new)
  {
    fprintf(stderr,"Couldn't allocate memory in CAD_object_allocate()\n");
    return;
  }

  // Fill in default values
  p_new->creation_time = sec_now();
  p_new->start = NULL;
  p_new->line_color = colors[0x27];
  p_new->line_type = 2;  // LineOnOffDash;
  p_new->line_width = 4;
  p_new->computed_area = 0;
  CAD_object_set_raw_probability(p_new,0.0,FALSE);
  p_new->label_latitude = 0l;
  p_new->label_longitude = 0l;
  p_new->label[0] = '\0';
  p_new->comment[0] = '\0';

  // Allocate area to hold the first vertice

#ifdef CAD_DEBUG
  fprintf(stderr,"Allocating a new vertice\n");
#endif

  p_new->start = (VerticeRow *)malloc(sizeof(VerticeRow));
  if (!p_new->start)
  {
    fprintf(stderr,"Couldn't allocate memory in CAD_object_allocate(2)\n");
    free(p_new);
    return;
  }

  p_new->start->next = NULL;
  p_new->start->latitude = latitude;
  p_new->start->longitude = longitude;

  // Hook it into the linked list of objects
  p_new->next = CAD_list_head;
  CAD_list_head = p_new;

  /*
  //
  // Note:  It was too confusing to have these two dialogs close and
  // get redrawn when we click on the first vertice.  The net result
  // is that we may have two dialogs move on top of the drawing area
  // to the spot we're trying to draw.  Commented out this section due
  // to that.  We'll get the two dialogs updated when we click on
  // either the DONE or CANCEL button on the Close Polygon dialog.
  //
      // Here we update the erase cad objects dialog if it is up on
      // the screen.  We get rid of it and re-establish it, which will
      // usually make the dialog move, but this is better than having
      // it be out-of-date.
      //
      if (cad_erase_dialog != NULL) {
          Draw_CAD_Objects_erase_dialog_close(da, NULL, NULL);
          Draw_CAD_Objects_erase_dialog(da, NULL, NULL);
      }

      // Here we update the edit cad objects dialog by getting rid of
      // it and then re-establishing it if it is active when we start.
      // This will usually make the dialog move, but it's better than
      // having it be out-of-date.
      //
      if (cad_list_dialog!=NULL) {
          // Update the Edit CAD Objects list
          Draw_CAD_Objects_list_dialog_close(da, NULL, NULL);
          Draw_CAD_Objects_list_dialog(da, NULL, NULL);
      }
  */
}





// Delete all vertices associated with a CAD object and free the
// memory.  We really should pass a pointer to the object here
// instead of a vertice, and set the start pointer to NULL when
// done.
//
void CAD_vertice_delete_all(VerticeRow *v)
{
  VerticeRow *tmp;

  // Call CAD_vertice_delete() for each vertice, then unlink this
  // CAD object from the linked list and free its memory.

  // Iterate through each vertice, deleting as we go
  while (v != NULL)
  {
    tmp = v;
    v = v->next;
    free(tmp);

#ifdef CAD_DEBUG
    fprintf(stderr,"Free'ing a vertice\n");
#endif

  }
}





// Delete _all_ CAD objects and all associated vertices.  Loop
// through the entire list of CAD objects, calling
// CAD_vertice_delete_all() and then free'ing the CAD object.  When
// done, set the start pointer to NULL.
//
// We also need to wipe the persistent CAD object file.
//
void CAD_object_delete_all(void)
{
  CADRow *p = CAD_list_head;
  CADRow *tmp;

  while (p != NULL)
  {
    VerticeRow *v = p->start;

    // Remove all of the vertices
    if (v != NULL)
    {

      // Delete/free the vertices
      CAD_vertice_delete_all(v);
    }

    // Remove the object and free its memory
    tmp = p;
    p = p->next;
    free(tmp);

#ifdef CAD_DEBUG
    fprintf(stderr,"Free'ing an object\n");
#endif

  }

  // Zero the CAD linked list head
  CAD_list_head = NULL;
}





// Remove a vertice, thereby joining two segments into one?
//
// Recompute the raw probability if need be, or make it an invalid
// value so that we know we need to recompute it.
//
//void CAD_vertice_delete(CADrow *object) {
//    VerticeRow *v = object->start;

// Unlink the vertice from the linked list and free its memory.
// Allow removing a vertice in the middle or end of a chain.  If
// removing the vertice turns the polygon into an open polygon,
// alert the user of that fact and ask if they wish to close it.
//}





/* Test to see if a CAD object of the name (label) provided exists.
   Parameter: label, the label text to be checked.
   Returns 0 if no CAD object with a name matching the
   provided name is found.
   Returns 1 if a CAD object with a name matching the
   provided name is found.  */
int exists_CAD_object_by_label(char *label)
{
  CADRow *object_pointer = CAD_list_head;
  int result = 0;  // function return value
  int done = 0;    // flag to stop loop when a match is found
  while (object_pointer != NULL && done==0)
  {
    if (strcmp(object_pointer->label,label)==0)
    {
      // a matching name was found
      result = 1;
      done = 1;
    }
    object_pointer = object_pointer->next;
  }
  return result;
}





/* Counts to see how many CAD objects of the name (label) provided exist.
   Parameter: label, the label text to be checked.
   Returns 0 if no CAD object with a name matching the
   provided name is found.
   Returns count of the number of CAD objects with a matching label if
   one or more is found.  */
int count_CAD_object_with_matching_label(char *label)
{
  CADRow *object_pointer = CAD_list_head;
  int result = 0;
  while (object_pointer != NULL)
  {
    // iterate through all CAD objects
    if (strcmp(object_pointer->label,label)==0)
    {
      // a matching name was found
      result++;
      object_pointer = object_pointer->next;
    }
  }
  return result;
}





/* Delete one CAD object and all of its vertices. */
void CAD_object_delete(CADRow *object)
{
  CADRow *all_objects_ptr = CAD_list_head;
  CADRow *previous_object_ptr = CAD_list_head;
  VerticeRow *v = object->start;
  int done = 0;

#ifdef CAD_DEBUG
  fprintf(stderr,"Deleting CAD object %s\n",object->label);
#endif

  if (object == CAD_list_head
      && polygon_last_x != -1
      && polygon_last_y != -1)
  {
    polygon_last_x = -1;
    polygon_last_y = -1;
  }

  // check to see if the object we were given was the first object
  if (object==all_objects_ptr)
  {
#ifdef CAD_DEBUG
    fprintf(stderr,"Deleting first CAD object %s\n",object->label);
#endif
    CAD_vertice_delete_all(v); // Frees the memory also

    // Unlink the object from the chain and free the memory.
    CAD_list_head = object->next;  // Unlink
    free(object);   // Free the object memory
  }
  else
  {
#ifdef CAD_DEBUG
    fprintf(stderr,"Deleting other than first CAD object %s\n",object->label);
#endif
    // walk through the list and delete the object when found
    while (all_objects_ptr != NULL && done==0)
    {
      if (object==all_objects_ptr)
      {
        v = object->start;
        CAD_vertice_delete_all(v);
        previous_object_ptr->next = object->next;
        free(object);
        done = 1;
      }
      else
      {
        all_objects_ptr = all_objects_ptr->next;
      }
    }
  }
}





// Split an existing CAD object into two objects.  Can we trigger
// this by drawing a line across a closed polygon?
void CAD_object_split_existing(void)
{
}

// Join two existing polygons into one larger polygon.
void CAD_object_join_two(void)
{
}

// Move an entire CAD object, with all it's vertices, somewhere
// else.  Move the label along with it as well.
void CAD_object_move(void)
{
}





// Determine if a CAD object is a closed polygon.
//
// Takes a pointer to a CAD object as an argument.
// Returns 1 if the object is closed.
// Returns 0 if the object is not closed.
//
int is_CAD_object_open(CADRow *cad_object)
{
  VerticeRow *vertex_pointer;
  int vertex_count = 0;
  int result = 1;
  int atleast_one_different = 0;
  long start_lat, start_long;
  long stop_lat, stop_long;

  vertex_pointer = cad_object->start;
  if (vertex_pointer!=NULL)
  {
    // greater than zero points, get first point.
    start_lat = vertex_pointer->latitude;
    start_long = vertex_pointer->longitude;
    stop_lat = vertex_pointer->latitude;
    stop_long = vertex_pointer->longitude;
    vertex_pointer = vertex_pointer->next;
    while (vertex_pointer != NULL)
    {
      //greater than one point, get current point.
      stop_lat = vertex_pointer->latitude;
      stop_long = vertex_pointer->longitude;
      if (stop_lat!=start_lat || stop_long!=start_long)
      {
        atleast_one_different = 1;
      }
      vertex_pointer = vertex_pointer->next;
      vertex_count++;
    }
    if (vertex_count>2 && start_lat==stop_lat && start_long==stop_long && atleast_one_different > 0)
    {
      // more than two points, and they aren't in the same place
      result = 0;
    }
  }
  return result;
}





// Compute the area enclosed by a CAD object.  Check that it is a
// closed, non-intersecting polygon first.
//
double CAD_object_compute_area(CADRow *CAD_list_head)
{
  VerticeRow *tmp;
  double area;
  char temp_course[20];
  // Walk the linked list, computing the area of the
  // polygon.  Greene's Theorem is how we can compute the area of
  // a polygon using the vertices.  We could also compute whether
  // we're going clockwise or counter-clockwise around the polygon
  // using Greene's Theorem.  In fact I think we do that for
  // Shapefile hole polygons.  Remember that here we're walking
  // around the vertices backwards due to the ordering of the
  // list.  Shouldn't matter for our purposes though.
  //
  area = 0.0;
  tmp = CAD_list_head->start;
  if (is_CAD_object_open(CAD_list_head)==0)
  {
    // Only compute the area if CAD object is a closed polygon,
    // that is, not an open polygon.
    while (tmp->next != NULL)
    {
      double dx0, dy0, dx1, dy1;

      // Because lat/long units can vary drastically w.r.t. real
      // units, we need to multiply the terms by the real units in
      // order to get real area.

      // Compute real distances from a fixed point.  Convert to
      // the current measurement units.  We'll use the starting
      // vertice as our fixed point.
      //
      dx0 = calc_distance_course(
              CAD_list_head->start->latitude,
              CAD_list_head->start->longitude,
              CAD_list_head->start->latitude,
              tmp->longitude,
              temp_course,
              sizeof(temp_course));

      if (tmp->longitude < CAD_list_head->start->longitude)
      {
        dx0 = -dx0;
      }

      dy0 = calc_distance_course(
              CAD_list_head->start->latitude,
              CAD_list_head->start->longitude,
              tmp->latitude,
              CAD_list_head->start->longitude,
              temp_course,
              sizeof(temp_course));

      if (tmp->latitude < CAD_list_head->start->latitude)
      {
        dx0 = -dx0;
      }

      dx1 = calc_distance_course(
              CAD_list_head->start->latitude,
              CAD_list_head->start->longitude,
              CAD_list_head->start->latitude,
              tmp->next->longitude,
              temp_course,
              sizeof(temp_course));

      if (tmp->next->longitude < CAD_list_head->start->longitude)
      {
        dx0 = -dx0;
      }

      dy1 = calc_distance_course(
              CAD_list_head->start->latitude,
              CAD_list_head->start->longitude,
              tmp->next->latitude,
              CAD_list_head->start->longitude,
              temp_course,
              sizeof(temp_course));

      // Add the minus signs back in, if any
      if (tmp->longitude < CAD_list_head->start->longitude)
      {
        dx0 = -dx0;
      }
      if (tmp->latitude < CAD_list_head->start->latitude)
      {
        dy0 = -dy0;
      }
      if (tmp->next->longitude < CAD_list_head->start->longitude)
      {
        dx1 = -dx1;
      }
      if (tmp->next->latitude < CAD_list_head->start->latitude)
      {
        dy1 = -dy1;
      }

      // Greene's Theorem:  Summation of the following, then
      // divide by two:
      //
      // A = X Y    - X   Y
      //  i   i i+1    i+1 i
      //
      area += (dx0 * dy1) - (dx1 * dy0);

      tmp = tmp->next;
    }
    area = 0.5 * area;
  }

  if (area < 0.0)
  {
    area = -area;
  }

//fprintf(stderr,"Square nautical miles: %f\n", area);

  return area;

}





// Allocate a label for an object, and place it according to the
// user's requests.  Keep track of where from the origin to place
// the label, font to use, color, etc.
void CAD_object_allocate_label(void)
{
}





// Set the probability for an object.  We should probably allocate
// the raw probability to small "buckets" within the closed polygon.
// This will allow us to split/join polygons later without messing
// up the probablity assigned to each area originally.  Check that
// it is a closed polygon first.
// if as_percent==TRUE, then probability is treated as a percent
// (expected to be a value between 0 and 100).
// otherwise, then probability is treated as a probability
// (expected to be a value between 0 and 1).
//
void CAD_object_set_raw_probability(CADRow *object_ptr, float probability, int as_percent)
{
  // initial implementation just assigns a single raw probability to the whole polygon.
  // internal storage is as a probability between 0 and 1
  // users will usually want to manipulate this as a percent (between 0 and 100)
  // thus the get and set functions are aware of both internal storage and
  // the user's request and return an appropriately scaled value.
  if (as_percent==TRUE)
  {
    // convert from a percent to a probability between 0 and 1
    object_ptr->raw_probability = (probability/100.00);
  }
  else
  {
    // treat as in internal storage form
    object_ptr->raw_probability = probability;
  }
}





// Get the raw probability for an object.  Sum up the raw
// probability "buckets" contained within the closed polygon.  Check
// that it _is_ a closed polygon first.
//
float CAD_object_get_raw_probability(CADRow *object_ptr, int as_percent)
{
  float result = 0.0;
  // not checking yet for closure
  if (object_ptr != NULL)
  {
    // initial implementation returns just the single raw probability
    result = object_ptr->raw_probability;
    if (as_percent > 0)
    {
      // raw probability is a probability between 0 an 1,
      // this may be desired as a percent.
      result = result * 100;
    }
  }
#ifdef CAD_DEBUG
  fprintf(stderr,"Getting Probability: %01.5f\n",result);
#endif
  return result;
}





void CAD_object_set_line_width(void)
{
}





void CAD_object_set_color(void)
{
}





void CAD_object_set_linetype(void)
{
}





// Used to break a line segment into two.  Can then move the vertice
// if needed.  Recompute the raw probability if need be, or make it
// an invalid value so that we know we need to recompute it.
void CAD_vertice_insert_new(void)
{
  // Check whether a line segment will cross another?
}





// Move an existing vertice.  Recompute the raw probability if need
// be, or make it an invalid value so that we know we need to
// recompute it.
void CAD_vertice_move(void)
{
  // Check whether a line segment will cross another?
}





// Set the location for drawing the label of an area to the center
// of the area.  Takes a pointer to a CAD object as a parameter.
// Sets the label_latitude and label_longitude attributes of the CAD
// object to the center of the region described by the vertices of
// the object.
//
void CAD_object_set_label_at_centroid(CADRow *CAD_object)
{
  // *** current implementation approximates the center as the
  // average of the largest and smallest of each of latitude
  // and longitude rather than correctly computing the centroid,
  // that is, it places the label at the centroid of a bounding
  // box for the area.  ***
  // We can't use a simple x=sum(x)/n, y=sum(y)/n as the
  // points on the outline shouldn't be weighted equally.
  // Ideal would be to place the label at the central point within
  // the area itslef, apparently this is a hard prbolem.
  // alternative would be to use the centroid, which like the
  // average of maximum and minimum values may lie outside of
  // the area.
  VerticeRow *vertex_pointer;
  long min_lat, min_long;
  long max_lat, max_long;
  // Walk the linked list and compute the centroid of the bounding box.
  vertex_pointer = CAD_object->start;
  min_lat = 0.0;
  min_long = 0.0;

  // Set the latitude and longitude of the label to the
  // centroid of the bounding box.
  // Start by setting lat and long of label to first point.
  CAD_object->label_latitude = vertex_pointer->latitude;
  CAD_object->label_longitude = vertex_pointer->longitude;
  if (vertex_pointer != NULL)
  {
    // Iterate through the vertices and calculate the center x and y position
    // based on an average of the largest and smallest latitudes and longitudes.
    min_lat = vertex_pointer->latitude;
    min_long = vertex_pointer->longitude;
    max_lat = vertex_pointer->latitude;
    max_long = vertex_pointer->longitude;
    while (vertex_pointer != NULL)
    {
      if (vertex_pointer->next != NULL)
      {
        if (vertex_pointer->longitude < min_long )
        {
          min_long = vertex_pointer->longitude;
        }
        if (vertex_pointer->latitude < min_lat )
        {
          min_lat = vertex_pointer->latitude;
        }
        if (vertex_pointer->longitude > max_long )
        {
          max_long = vertex_pointer->longitude;
        }
        if (vertex_pointer->latitude > max_lat )
        {
          max_lat = vertex_pointer->latitude;
        }
      }
      vertex_pointer = vertex_pointer->next;
    }
    CAD_object->label_latitude = (max_lat + min_lat)/2.0;
    CAD_object->label_longitude = (max_long + min_long)/2.0;
  }
}





// Called when we complete a new CAD object.  Save the object to
// disk so that we can recover in the case of a crash or power
// failure.  Save any old file to a backup file.  Perhaps write them
// to numbered backup files so that we keep several on-hand?
//
void Save_CAD_Objects_to_file(void)
{
  FILE *f;
  char *file;
  CADRow *object_ptr = CAD_list_head;
  char temp_file_path[MAX_VALUE];

  fprintf(stderr,"Saving CAD objects to file\n");

  // Save in ~/.xastir/config/CAD_object.log
  file = get_user_base_dir("config/CAD_object.log", temp_file_path, sizeof(temp_file_path));
  f = fopen(file,"w+");

  if (f == NULL)
  {
    fprintf(stderr,
            "Couldn't open config/CAD_object.log file for writing!\n");
    return;
  }

  while (object_ptr != NULL)
  {
    VerticeRow *vertice = object_ptr->start;

    // Write out the main object info:
    fprintf(f,"\nCAD_Object\n");
    fprintf(f,"creation_time:   %lu\n",(unsigned long)object_ptr->creation_time);
    fprintf(f,"line_color:      %d\n",object_ptr->line_color);
    fprintf(f,"line_type:       %d\n",object_ptr->line_type);
    fprintf(f,"line_width:      %d\n",object_ptr->line_width);
    fprintf(f,"computed_area:   %f\n",object_ptr->computed_area);
    fprintf(f,"raw_probability: %f\n",CAD_object_get_raw_probability(object_ptr,TRUE));
    fprintf(f,"label_latitude:  %lu\n",object_ptr->label_latitude);
    fprintf(f,"label_longitude: %lu\n",object_ptr->label_longitude);
    fprintf(f,"label: %s\n",object_ptr->label);
    if (strlen(object_ptr->comment)>1)
    {
      fprintf(f,"comment: %s\n",object_ptr->comment);
    }
    else
    {
      fprintf(f,"comment: NULL\n");
    }

    // Iterate through the vertices:
    while (vertice != NULL)
    {

      fprintf(f,"Vertice: %lu %lu\n",
              vertice->latitude,
              vertice->longitude);

      vertice = vertice->next;
    }
    object_ptr = object_ptr->next;
  }
  (void)fclose(f);
}





// Called by main() when we start Xastir.  Restores CAD objects
// created in earlier Xastir sessions.
//
void Restore_CAD_Objects_from_file(void)
{
  FILE *f;
  char *file;
  char line[MAX_FILENAME];
  char temp_file_path[MAX_VALUE];
#ifdef CAD_DEBUG
  fprintf(stderr,"Restoring CAD objects from file\n");
#endif

  // Restore from ~/.xastir/config/CAD_object.log
  file = get_user_base_dir("config/CAD_object.log", temp_file_path, sizeof(temp_file_path));
  f = fopen(file,"r");

  if (f == NULL)
  {
#ifdef CAD_DEBUG
    fprintf(stderr,
            "Couldn't open config/CAD_object.log file for reading!\n");
#endif
    return;
  }

  while (!feof (f))
  {
    (void)get_line(f, line, MAX_FILENAME);
    if (strncasecmp(line,"CAD_Object",10) == 0)
    {
      // Found a new CAD Object declaration!

      //fprintf(stderr,"Found CAD_Object\n");

      // Malloc a new object, add it to the linked list, start
      // filling in the fields.
      //
      // This gives us a default object with one vertice.  We
      // can replace all of the fields in it as we parse them.
      CAD_object_allocate(0l, 0l);

      // Remove the one vertice from the newly allocated
      // object so that we don't end up with one too many
      // vertices when all done.
      CAD_vertice_delete_all(CAD_list_head->start);
      CAD_list_head->start = NULL;

    }
    else if (strncasecmp(line,"creation_time:",14) == 0)
    {
      //fprintf(stderr,"Found creation_time:\n");
      unsigned long temp_time;
      if (1 != sscanf(line+15, "%lu",&temp_time))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [creation_time]\n");
      }
      CAD_list_head->creation_time=(time_t)temp_time;
    }
    else if (strncasecmp(line,"line_color:",11) == 0)
    {
      //fprintf(stderr,"Found line_color:\n");
      if (1 != sscanf(line+12,"%d",
                      &CAD_list_head->line_color))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [line_color]\n");
      }
    }
    else if (strncasecmp(line,"line_type:",10) == 0)
    {
      //fprintf(stderr,"Found line_type:\n");
      if (1 != sscanf(line+11,"%d",
                      &CAD_list_head->line_type))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [line_type]\n");
      }
    }
    else if (strncasecmp(line,"line_width:",11) == 0)
    {
      //fprintf(stderr,"Found line_width:\n");
      if (1 != sscanf(line+12,"%d",
                      &CAD_list_head->line_width))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [line_width]\n");
      }
    }
    else if (strncasecmp(line,"computed_area:",14) == 0)
    {
      //fprintf(stderr,"Found computed_area:\n");
      if (1 != sscanf(line+15,"%f",
                      &CAD_list_head->computed_area))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [computed_area]\n");
      }
    }
    else if (strncasecmp(line,"raw_probability:",16) == 0)
    {
      //fprintf(stderr,"Found raw_probability:\n");
      if (1 != sscanf(line+17,"%f",
                      &CAD_list_head->raw_probability))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [raw_probability]\n");
      }
      else
      {
        // External storage is as percent, need to make sure that this
        // fits the expected internal storage format.
        // Thus take given value and store using method that knows
        // how to handle percents
        CAD_object_set_raw_probability(CAD_list_head,CAD_list_head->raw_probability,TRUE);
      }
    }
    else if (strncasecmp(line,"label_latitude:",15) == 0)
    {
      //fprintf(stderr,"Found label_latitude:\n");
      if (1 != sscanf(line+16,"%lu",
                      (unsigned long *)&CAD_list_head->label_latitude))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [label_latitude]\n");
      }
    }
    else if (strncasecmp(line,"label_longitude:",16) == 0)
    {
      //fprintf(stderr,"Found label_longitude:\n");
      if (1 != sscanf(line+17,"%lu",
                      (unsigned long *)&CAD_list_head->label_longitude))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [label_longitude]\n");
      }
    }
    else if (strncasecmp(line,"label:",6) == 0)
    {
      //fprintf(stderr,"Found label:\n");
      xastir_snprintf(CAD_list_head->label,
                      sizeof(CAD_list_head->label),
                      "%s",
                      line+7);
    }
    else if (strncasecmp(line,"comment:",8) == 0)
    {
      //fprintf(stderr,"Found comment:\n");
      xastir_snprintf(CAD_list_head->comment,
                      sizeof(CAD_list_head->comment),
                      "%s",
                      line+9);

      if (strcmp(CAD_list_head->comment,"NULL")==0)
      {
        xastir_snprintf(CAD_list_head->comment,
                        sizeof(CAD_list_head->comment),
                        "%c",
                        '\0'
                       );
      }
    }
    else if (strncasecmp(line,"Vertice:",8) == 0)
    {
      long latitude, longitude;

      //fprintf(stderr,"Found Vertice:\n");
      if (2 != sscanf(line+9,"%lu %lu",
                      (unsigned long *)&latitude,
                      (unsigned long *)&longitude))
      {
        fprintf(stderr,"Restore_CAD_Objects_from_file:sscanf parsing error [vertex]\n");
      }
      CAD_vertice_allocate(latitude,longitude);
    }
    else
    {
      // Else not recognized, do nothing with it!
      //fprintf(stderr,"Found unrecognized line\n");
    }
  }
  (void)fclose(f);
  // Reload symbols/tracks/CAD objects to draw the loaded objects
  xa_ui_redraw();
}





// Function called by UpdateTime when doing screen refresh.  Draws
// all CAD objects onto the screen again.
//
void Draw_All_CAD_Objects(Widget w)
{
  CADRow *object_ptr = CAD_list_head;
  long x_long, y_lat;
  long x_offset, y_offset;
  float probability;
  char probability_string[8];
  VerticeRow *vertice;
  double area;
  int actual_line_type = LineOnOffDash;
  static int sizeof_area_description = 50; // define here as local static to limit size of display on map
  // independent of size as shown on form
  char area_description[sizeof_area_description];
  char dash[2];

  // Start at CAD_list_head, iterate through entire linked list,
  // drawing as we go.  Respect the line
  // width/line_color/line_type variables for each object.

//fprintf(stderr,"Drawing CAD objects\n");
  if (CAD_draw_objects==TRUE)
  {
    while (object_ptr != NULL)
    {
      probability = CAD_object_get_raw_probability(object_ptr,1);
      xastir_snprintf(probability_string,
                      sizeof(probability_string),
                      "%01.1f%%",
                      probability);

      // find point at which to draw label and other descriptive text
      x_long = object_ptr->label_longitude;
      y_lat = object_ptr->label_latitude;
#ifdef CAD_DEBUG
      fprintf(stderr,"Drawing object %s\n", (object_ptr->label) ? object_ptr->label : "NULL" );
#endif
      //fprintf(stderr,"Lat: %d\n", y_lat);
      //fprintf(stderr,"Long: %d\n", x_long);
//            if ((x_long+10>=0) && (x_long-10<=129600000l)) {      // 360 deg

//                if ((y_lat+10>=0) && (y_lat-10<=64800000l)) {     // 180 deg

      if ((x_long>NW_corner_longitude) && (x_long<SE_corner_longitude))
      {

        if ((y_lat>NW_corner_latitude) && (y_lat<SE_corner_latitude))
        {

          // ok to draw label and assocated data, point is on screen
          x_offset=((x_long-NW_corner_longitude)/scale_x)-(10);
          y_offset=((y_lat -NW_corner_latitude) /scale_y)-(10);
          // ****** ?? use -10 or point ??
          x_offset=((x_long-NW_corner_longitude)/scale_x);
          y_offset=((y_lat -NW_corner_latitude) /scale_y);

          if (((int)strlen(object_ptr->label)>0) & (CAD_show_label==TRUE))
          {
            // Draw Label
            // 0x08 is background color
            // 0x40 is foreground color (yellow)
            draw_nice_string(w,pixmap_final,letter_style,x_offset,y_offset,object_ptr->label,0x08,0x40,strlen(object_ptr->label));

            x_offset=x_offset+12;
            y_offset=y_offset+15;
          }

          if (CAD_show_raw_probability==TRUE)
          {
            // draw probability
            draw_nice_string(w,pixmap_final,letter_style,x_offset,y_offset,probability_string,0x08,0x40,strlen(probability_string));
            y_offset=y_offset+15;
          }

          if ((CAD_show_comment==TRUE) & ((int)strlen(object_ptr->comment)>0))
          {
            // draw comment
            draw_nice_string(w,pixmap_final,letter_style,x_offset,y_offset,object_ptr->comment,0x08,0x40,strlen(object_ptr->comment));
            y_offset=y_offset+15;
          }


          if (CAD_show_area==TRUE)
          {
            area = object_ptr->computed_area;
            Format_area_for_output(&area, area_description, sizeof_area_description);
            draw_nice_string(w,pixmap_final,letter_style,x_offset,y_offset,area_description,0x08,0x40,strlen(area_description));
            y_offset=y_offset+15;
          }

        }
      }
//                }
//            }

      // Iterate through the vertices and draw the lines
      vertice = object_ptr->start;

      switch (object_ptr->line_type)
      {

        case 1:
          actual_line_type = LineSolid;
          break;

        case 2:
          actual_line_type = LineOnOffDash;
          dash[0] = dash[1]  = 8;
          break;

        case 3:
          actual_line_type = LineDoubleDash;
          dash[0] = dash[1]  = 16;
          break;

        default:
          actual_line_type = LineOnOffDash;
          dash[0] = dash[1]  = 8;
          break;
      }

      // Set up line color/width/type here
      xa_pen_line(gc_tint, object_ptr->line_width, actual_line_type, XA_CAP_BUTT, XA_JOIN_MITER);

      if (object_ptr->line_type  != 1)
      {
        xa_pen_dashes(gc_tint, 0, dash, 2);      // elements in dash lista
      }

      xa_pen_color(gc_tint, object_ptr->line_color);

      xa_pen_function(gc_tint, XA_FUNC_XOR);

      while (vertice != NULL)
      {
        if (vertice->next != NULL)
        {
          // Use the draw_vector function from maps.c
          draw_vector(w,
                      vertice->longitude,
                      vertice->latitude,
                      vertice->next->longitude,
                      vertice->next->latitude,
                      gc_tint,
                      pixmap_final,
                      0);
        }
        vertice = vertice->next;
      }
      object_ptr = object_ptr->next;
    }
  }
}




// Formats an area as a string in english (square miles) or metric units
// (square kilometers). Switches to square feet or square meters if the
// area is less than 0.1 of the units.
//
// Parameters:
//     area: an area in square kilometers.
//     area_description: area reformatted as a localized text string.
//     sizeof_area_description: array length of area_description.
//
void Format_area_for_output(double *area_km2, char *area_description, int sizeof_area_description)
{
  double area;
  // Format it for output and dump it out.  We're using square terms, so
  // apply the conversion factor twice to convert from square kilometers
  // to the units of interest. The result here is squared meters or
  // squared feet.
//fprintf(stderr,"Square km: %f\n", *area_km2);

  area = *area_km2 * 1000.0 * 1000.0 * cvt_m2len * cvt_m2len;

  // We could be measuring a very small or a very large object.
  // In the case of very small, convert it to square feet or
  // square meters.

  if (english_units)   // Square feet
  {
//fprintf(stderr,"Square feet: %f\n", area);

    if (area < 2787840.0)   // Switch at 0.5 miles squared
    {
      // Smaller area: Output in feet squared
      xastir_snprintf(area_description,
                      sizeof_area_description,
                      "A:%0.2f %s %s",
                      area,
                      langcode("POPUPMA052"),     // sq
                      langcode("POPUPMA053") );   // ft
      //popup_message_always(langcode("POPUPMA020"),area_description);
    }
    else
    {
      // Larger area: Output in miles squared
      area = area / 27878400.0;
      xastir_snprintf(area_description,
                      sizeof_area_description,
                      "A:%0.2f %s %s",
                      area,
                      langcode("POPUPMA052"),     // sq
                      langcode("POPUPMA055") );   // mi
      //popup_message_always(langcode("POPUPMA020"),area_description);
    }
  }
  else    // Square meters
  {
//fprintf(stderr,"Square meters: %f\n", area);

    if (area < 100000.0)   // Switch at 0.1 km squared
    {
      // Smaller area: Output in meters squared
      xastir_snprintf(area_description,
                      sizeof_area_description,
                      "A:%0.2f %s %s",
                      area,
                      langcode("POPUPMA052"),     // sq
                      langcode("POPUPMA054") );   // meters
      //popup_message_always(langcode("POPUPMA020"),area_description);
    }
    else
    {
      // Larger ara: Output in kilometers squared
      area = area / 1000000.0;
      xastir_snprintf(area_description,
                      sizeof_area_description,
                      "A:%0.2f %s %s",
                      area,
                      langcode("POPUPMA052"),     // sq
                      langcode("UNIOP00005") );   // km
      //popup_message_always(langcode("POPUPMA020"),area_description);
    }
  }
}








