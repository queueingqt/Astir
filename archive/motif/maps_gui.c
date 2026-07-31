/*
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

/*
 * maps_gui.c -- printing, print preview, and map snapshots.
 *
 * Split out of maps.c.  Thirteen functions and about 37k of Motif -- the print
 * properties dialog, the Postscript dialog, their toggle callbacks, and the
 * snapshot writer -- against 62 functions of map loading, indexing, projection
 * and grid drawing that stay behind and link without the toolkit.
 *
 * This split waited on the font abstraction.  Before it, every grid function in
 * maps.c reached Motif through text measurement, so cutting here would have put
 * draw_grid, both UTM grid functions and the lat/lon grid on this side along
 * with the print dialog -- the largest core functions in the file following the
 * smallest GUI ones.  Text goes through xa_draw.h now.
 *
 * snapshot_thread() came across with Snapshot() even though it has no Motif in
 * it: it is the second half of one operation, and leaving it behind would have
 * meant the core calling into here.
 *
 * Moved verbatim.  Only the file-scope declarations changed.
 */

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif  // HAVE_CONFIG_H

#include "core/util/snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>

#include <X11/Intrinsic.h>
#include <Xm/XmAll.h>

#ifdef HAVE_X11_XPM_H
  #include <X11/xpm.h>
  #ifdef HAVE_LIBXPM // if we have both, prefer the extra library
    #undef HAVE_XM_XPMI_H
  #endif // HAVE_LIBXPM
#endif // HAVE_X11_XPM_H

#ifdef HAVE_XM_XPMI_H
  #include <Xm/XpmI.h>
#endif // HAVE_XM_XPMI_H

#include "core/astir.h"
#include "ui/motif/astir_gui.h"
#include "ui/motif/wx_gui.h"
#include "ui/motif/main_gui.h"
#include "ui/motif/draw_symbols_gui.h"
#include "ui/motif/cad_objects_gui.h"
#include "core/globals.h"
#include "core/main.h"
#include "core/map/maps.h"
#include "core/aprs/alert.h"
#include "core/util/util.h"
#include "draw/x11/color.h"
#include "core/state/xa_config.h"
#include "core/state/xa_settings.h"
#include "core/geo/datum.h"
#include "core/render/draw_symbols.h"
#include "draw/x11/rotated.h"

#include "draw/xa_draw.h"

#include "core/xa_ui.h"

// Must be last include file
#include "core/util/leak_detection.h"

extern XmFontList fontlist1;    // Menu/System fontlist
extern void pos_dialog(Widget w);
extern void resize_dialog(Widget form, Widget dialog);

// The two dialogs and their widgets.  Private to this file.
Widget print_properties_dialog = (Widget)NULL;
Widget print_postscript_dialog = (Widget)NULL;
Widget printer_data;
Widget previewer_data;
Widget rotate_90 = (Widget)NULL;
Widget auto_rotate = (Widget)NULL;

//char  print_paper_size[20] = "Letter";  // Displayed in dialog, but not used yet.
//float print_scale = 1.0;                // Not used yet.
//int   print_blank_background_color = 0; // Not used yet.
int   print_resolution = 150;           // 72 dpi is normal for Postscript.
// 100 or 150 dpi work well with HP printer

// Snapshot bookkeeping.  Both halves of the snapshot are on this side, so this
// is private too.
time_t last_snapshot = 0;               // Used to determine when to take next snapshot
int doing_snapshot = 0;

// The locks stay defined in maps.c because maps_init() initialises them and does
// several other things besides, so it did not come across.  They are plain
// mutexes, not a toolkit type, so maps.c holding them costs nothing.
extern astir_mutex print_properties_dialog_lock;
extern astir_mutex print_postscript_dialog_lock;







static void Print_postscript_destroy_shell(Widget UNUSED(widget), XtPointer clientData, XtPointer UNUSED(callData) )
{
  Widget shell = (Widget) clientData;
  char *temp_ptr;


  XtPopdown(shell);

  begin_critical_section(&print_postscript_dialog_lock, "maps.c:Print_postscript_destroy_shell" );

  if (print_postscript_dialog)
  {
    // Snag the path to the printer program from the print dialog
    temp_ptr = XmTextFieldGetString(printer_data);
    astir_snprintf(printer_program,
                    sizeof(printer_program),
                    "%s",
                    temp_ptr);
    XtFree(temp_ptr);
    (void)remove_trailing_spaces(printer_program);

    // Check for empty variable
    if (printer_program[0] == '\0')
    {

#ifdef LPR_PATH
      // Path to LPR if defined
      astir_snprintf(printer_program,
                      sizeof(printer_program),
                      "%s",
                      LPR_PATH);
#else // LPR_PATH
      // Empty path
      printer_program[0]='\0';
#endif // LPR_PATH
    }

//fprintf(stderr,"%s\n", printer_program);

    // Snag the path to the previewer program from the print dialog
    temp_ptr = XmTextFieldGetString(previewer_data);
    astir_snprintf(previewer_program,
                    sizeof(previewer_program),
                    "%s",
                    temp_ptr);
    XtFree(temp_ptr);
    (void)remove_trailing_spaces(previewer_program);

    // Check for empty variable
    if (previewer_program[0] == '\0')
    {

#ifdef GV_PATH
      // Path to GV if defined
      astir_snprintf(previewer_program,
                      sizeof(previewer_program),
                      "%s",
                      GV_PATH);
#else // GV_PATH
      // Empty string
      previewer_program[0] = '\0';
#endif // GV_PATH
    }
//fprintf(stderr,"%s\n", previewer_program);
  }

  XtDestroyWidget(shell);
  print_postscript_dialog = (Widget)NULL;

  end_critical_section(&print_postscript_dialog_lock, "maps.c:Print_postscript_destroy_shell" );

}





static void Print_properties_destroy_shell(Widget UNUSED(widget), XtPointer clientData, XtPointer UNUSED(callData) )
{
  Widget shell = (Widget) clientData;

  if (!shell)
  {
    return;
  }

  XtPopdown(shell);

  begin_critical_section(&print_properties_dialog_lock, "maps.c:Print_properties_destroy_shell" );

  XtDestroyWidget(shell);
  print_properties_dialog = (Widget)NULL;

  end_critical_section(&print_properties_dialog_lock, "maps.c:Print_properties_destroy_shell" );

}





// Print_window:  Prints the drawing area to a Postscript file and
// then sends it to the printer program (usually "lpr).
//
static void Print_window( Widget widget, XtPointer UNUSED(clientData), XtPointer UNUSED(callData) )
{

#ifdef NO_XPM
//    fprintf(stderr,"XPM or ImageMagick support not compiled into Astir!\n");
  xa_ui_popup_always(langcode("POPEM00035"),
                       "XPM or ImageMagick support not compiled into Astir! Cannot Print!");
#else   // NO_XPM

  char xpm_filename[MAX_FILENAME];
  char ps_filename[MAX_FILENAME];
  char command[MAX_FILENAME*2];
  char temp[MAX_FILENAME];
  int xpmretval;
  char temp_base_dir[MAX_VALUE];

  get_user_base_dir("tmp", temp_base_dir, sizeof(temp_base_dir));

  astir_snprintf(xpm_filename,
                  sizeof(xpm_filename),
                  "%s/print.xpm",
                  temp_base_dir);

  astir_snprintf(ps_filename,
                  sizeof(ps_filename),
                  "%s/print.ps",
                  temp_base_dir);

  xa_ui_busy();           // Show a busy cursor while we're doing all of this

  // Get rid of the Print dialog
  Print_postscript_destroy_shell(widget, print_postscript_dialog, NULL );

  if ( debug_level & 512 )
  {
    fprintf(stderr,"Creating %s\n", xpm_filename );
  }

  astir_snprintf(temp, sizeof(temp), "%s", langcode("PRINT0012") );
  xa_ui_status(temp);       // Dumping image to file...

  if (chdir(temp_base_dir) != 0)
  {
    fprintf(stderr,"Couldn't chdir to %s directory for print_window\n", temp_base_dir);
    return;
  }

  xpmretval=XpmWriteFileFromPixmap(XtDisplay(appshell),// Display *display
                                   "print.xpm",                                 // char *filename
                                   pixmap_final,                                // Pixmap pixmap
                                   (Pixmap)NULL,                                // Pixmap shapemask
                                   NULL );

  if (xpmretval != XpmSuccess)
  {
    fprintf(stderr,"ERROR writing %s: %s\n", xpm_filename,
            XpmGetErrorString(xpmretval));
    xa_ui_popup_always(langcode("POPEM00035"),
                         "Error writing xpm image file! Cannot Print!");
    return;
  }
  else            // We now have the xpm file created on disk
  {

    chmod( xpm_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

    if ( debug_level & 512 )
    {
      fprintf(stderr,"Convert %s ==> %s\n", xpm_filename, ps_filename );
    }


    // Convert it to a postscript file for printing.  This depends
    // on the ImageMagick command "convert".
    //

    if (debug_level & 512)
    {
      fprintf(stderr,"Width: %ld\tHeight: %ld\n", screen_width, screen_height);
    }

    astir_snprintf(temp, sizeof(temp), "%s", langcode("PRINT0013") );
    xa_ui_status(temp);       // Converting to Postscript...


#ifdef HAVE_CONVERT
    strcpy(command, CONVERT_PATH);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " -filter Point ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, xpm_filename);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, ps_filename);
    command[sizeof(command)-1] = '\0';  // Terminate string

    if ( debug_level & 512 )
    {
      fprintf(stderr,"%s\n", command );
    }

    if ( system( command ) != 0 )
    {
//            fprintf(stderr,"\n\nPrint: Couldn't convert from XPM to PS!\n\n\n");
      xa_ui_popup_always(langcode("POPEM00035"),
                           "Couldn't convert from XPM to PS!");
      return;
    }
#endif  // HAVE_CONVERT

    chmod( ps_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

    // Delete temporary xpm file
    if ( !(debug_level & 512) )
    {
      unlink( xpm_filename );
    }

    if ( debug_level & 512 )
    {
      fprintf(stderr,"Printing postscript file %s\n", ps_filename);
    }

// Note: This needs to be changed to "lp" for Solaris.
// Also need to have a field to configure the printer name.  One
// fill-in field could do both.
//
// Since we could be running SUID root, we don't want to be
// calling "system" anyway.  Several problems with it.

    strcpy(command, printer_program);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, ps_filename);
    command[sizeof(command)-1] = '\0';  // Terminate string

    if ( debug_level & 512 )
    {
      fprintf(stderr,"%s\n", command);
    }

    if (printer_program[0] == '\0')
    {
//            fprintf(stderr,"\n\nPrint: No print program defined!\n\n\n");
      xa_ui_popup_always(langcode("POPEM00035"),
                           "No print program defined!");
      return;
    }

    if ( system( command ) != 0 )
    {
//            fprintf(stderr,"\n\nPrint: Couldn't send to the printer!\n\n\n");
      xa_ui_popup_always(langcode("POPEM00035"),
                           "Couldn't send to the printer!");
      return;
    }

    /*
            if ( !(debug_level & 512) )
                unlink( ps_filename );
    */

    if ( debug_level & 512 )
    {
      fprintf(stderr,"  Done printing.\n");
    }
  }

  astir_snprintf(temp, sizeof(temp), "%s", langcode("PRINT0014") );
  xa_ui_status(temp);       // Finished creating print file.

  //popup_message( langcode("PRINT0015"), langcode("PRINT0014") );

#endif // NO_XPM

}





// Print_preview:  Prints the drawing area to a Postscript file.  If
// previewer_program has "gv" in it, then use the various options
// selected by the user.  If not, skip those options.
//
static void Print_preview( Widget widget, XtPointer UNUSED(clientData), XtPointer UNUSED(callData) )
{

#ifdef NO_XPM
//    fprintf(stderr,"XPM or ImageMagick support not compiled into Astir!\n");
  xa_ui_popup_always(langcode("POPEM00035"),
                       "XPM or ImageMagick support not compiled into Astir! Cannot Print!");
#else   // NO_GRAPHICS

  char xpm_filename[MAX_FILENAME];
  char ps_filename[MAX_FILENAME];
  char mono[50] = "";
  char invert[50] = "";
  char rotate[50] = "";
  char scale[50] = "";
  char density[50] = "";
  char command[MAX_FILENAME*2];
  char temp[MAX_FILENAME];
  char format[100] = " ";
  int xpmretval;
  char temp_base_dir[MAX_VALUE];

  get_user_base_dir("tmp", temp_base_dir, sizeof(temp_base_dir));


  astir_snprintf(xpm_filename,
                  sizeof(xpm_filename),
                  "%s/print.xpm",
                  temp_base_dir);

  astir_snprintf(ps_filename,
                  sizeof(ps_filename),
                  "%s/print.ps",
                  temp_base_dir);

  xa_ui_busy();           // Show a busy cursor while we're doing all of this

  // Get rid of the Print Properties dialog if it exists
  Print_properties_destroy_shell(widget, print_properties_dialog, NULL );

  if ( debug_level & 512 )
  {
    fprintf(stderr,"Creating %s\n", xpm_filename );
  }

  astir_snprintf(temp, sizeof(temp), "%s", langcode("PRINT0012") );
  xa_ui_status(temp);       // Dumping image to file...

  if (chdir(temp_base_dir) != 0)
  {
    fprintf(stderr,"Couldn't chdir to %s directory for print_preview\n", temp_base_dir);
    return;
  }

  xpmretval=XpmWriteFileFromPixmap(XtDisplay(appshell),// Display *display
                                   "print.xpm",                                 // char *filename
                                   pixmap_final,                                // Pixmap pixmap
                                   (Pixmap)NULL,                                // Pixmap shapemask
                                   NULL );

  if (xpmretval != XpmSuccess)
  {
    fprintf(stderr,"ERROR writing %s: %s\n", xpm_filename,
            XpmGetErrorString(xpmretval));
    xa_ui_popup_always(langcode("POPEM00035"),
                         "Error writing XPM file!");
    return;
  }
  else            // We now have the xpm file created on disk
  {

    chmod( xpm_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

    if ( debug_level & 512 )
    {
      fprintf(stderr,"Convert %s ==> %s\n", xpm_filename, ps_filename );
    }


    // If we're not using "gv", skip most of the code below and
    // go straight to the previewer program portion of the code.
    //
    if ( strstr(previewer_program,"gv") )
    {

      // Convert it to a postscript file for printing.  This
      // depends on the ImageMagick command "convert".
      //
      // Other options to try in the future:
      // -label
      //
      if ( print_auto_scale )
      {
//                sprintf(scale, "-geometry 612x792 -page 612x792 ");   // "Letter" size at 72 dpi
//                sprintf(scale, "-sample 612x792 -page 612x792 ");     // "Letter" size at 72 dpi
        astir_snprintf(scale, sizeof(scale), "-page 1275x1650+0+0 "); // "Letter" size at 150 dpi
      }
      else
      {
        scale[0] = '\0';  // Empty string
      }


      if ( print_in_monochrome )
      {
        astir_snprintf(mono, sizeof(mono), "-monochrome +dither " );  // Monochrome
      }
      else
      {
        astir_snprintf(mono, sizeof(mono), "+dither ");  // Color
      }


      if ( print_invert )
      {
        astir_snprintf(invert, sizeof(invert), "-negate " );  // Reverse Colors
      }
      else
      {
        invert[0] = '\0';  // Empty string
      }


      if (debug_level & 512)
      {
        fprintf(stderr,"Width: %ld\tHeight: %ld\n", screen_width, screen_height);
      }


      if ( print_rotated )
      {
        astir_snprintf(rotate, sizeof(rotate), "-rotate -90 " );

#ifdef HAVE_OLD_GV
        astir_snprintf(format, sizeof(format), "-landscape " );
#else   // HAVE_OLD_GV
        astir_snprintf(format, sizeof(format), "--orientation=landscape " );
#endif  // HAVE_OLD_GV

      }
      else if ( print_auto_rotation )
      {
        // Check whether the width or the height of the
        // pixmap is greater.  If width is greater than
        // height, rotate the image by 270 degrees.
        if (screen_width > screen_height)
        {
          astir_snprintf(rotate, sizeof(rotate), "-rotate -90 " );

#ifdef HAVE_OLD_GV
          astir_snprintf(format, sizeof(format), "-landscape " );
#else   // HAVE_OLD_GV
          astir_snprintf(format, sizeof(format), "--orientation=landscape " );
#endif  // HAVE_OLD_GV

          if (debug_level & 512)
          {
            fprintf(stderr,"Rotating\n");
          }
        }
        else
        {
          rotate[0] = '\0';   // Empty string
          if (debug_level & 512)
          {
            fprintf(stderr,"Not Rotating\n");
          }
        }
      }
      else
      {
        rotate[0] = '\0';   // Empty string
        if (debug_level & 512)
        {
          fprintf(stderr,"Not Rotating\n");
        }
      }


      // Higher print densities require more memory and time
      // to process
      astir_snprintf(density, sizeof(density), "-density %dx%d", print_resolution,
                      print_resolution );

      astir_snprintf(temp, sizeof(temp), "%s", langcode("PRINT0013") );
      xa_ui_status(temp);       // Converting to Postscript...


      // Filters:
      // Point (ok at higher dpi's)
      // Box  (not too bad)
      // Triangle (no)
      // Hermite (no)
      // Hanning (no)
      // Hamming (no)
      // Blackman (better but still not good)
      // Gaussian (no)
      // Quadratic (no)
      // Cubic (no)
      // Catrom (not too bad)
      // Mitchell (no)
      // Lanczos (no)
      // Bessel (no)
      // Sinc (not too bad)

    }

#ifdef HAVE_CONVERT
    strcpy(command, CONVERT_PATH);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " -filter Point ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, mono);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, invert);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, rotate);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, scale);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, density);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, xpm_filename);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, ps_filename);
    command[sizeof(command)-1] = '\0';  // Terminate string

    if ( debug_level & 512 )
    {
      fprintf(stderr,"%s\n", command );
    }

    if ( system( command ) != 0 )
    {
//            fprintf(stderr,"\n\nPrint: Couldn't convert from XPM to PS!\n\n\n");
      xa_ui_popup_always(langcode("POPEM00035"),
                           "Couldn't convert from XPM to PS!");
      return;
    }
#endif  // HAVE_CONVERT

    chmod( ps_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

    // Delete temporary xpm file
    if ( !(debug_level & 512) )
    {
      unlink( xpm_filename );
    }

    if ( debug_level & 512 )
    {
      fprintf(stderr,"Printing postscript file %s\n", ps_filename);
    }

// Since we could be running SUID root, we don't want to be
// calling "system" anyway.  Several problems with it.

    // Bring up the postscript viewer
    strcpy(command, previewer_program);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, format);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " ");
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, ps_filename);
    command[sizeof(command)-1] = '\0';  // Terminate string
    strcat(command, " &");
    command[sizeof(command)-1] = '\0';  // Terminate string

    if ( debug_level & 512 )
    {
      fprintf(stderr,"%s\n", command);
    }

    if (previewer_program[0] == '\0')
    {
//            fprintf(stderr,"\n\nPrint: No print previewer defined!\n\n\n");
      xa_ui_popup_always(langcode("POPEM00035"),
                           "No print previewer defined!");
      return;
    }

    if ( system( command ) != 0 )
    {
//            fprintf(stderr,"\n\nPrint: Couldn't bring up the postscript viewer!\n\n\n");
      xa_ui_popup_always(langcode("POPEM00035"),
                           "Couldn't bring up the viewer!");
      return;
    }

    /*
            if ( !(debug_level & 512) )
                unlink( ps_filename );
    */

    if ( debug_level & 512 )
    {
      fprintf(stderr,"  Done printing.\n");
    }
  }

  astir_snprintf(temp, sizeof(temp), "%s", langcode("PRINT0014") );
  xa_ui_status(temp);       // Finished creating print file.

  //popup_message( langcode("PRINT0015"), langcode("PRINT0014") );

#endif // NO_XPM

}





/*
 *  Auto_rotate
 *
 */
static void  Auto_rotate( Widget UNUSED(widget), XtPointer clientData, XtPointer callData)
{
  char *which = (char *)clientData;
  XmToggleButtonCallbackStruct *state = (XmToggleButtonCallbackStruct *)callData;

  if(state->set)
  {
    print_auto_rotation = atoi(which);
    print_rotated = 0;
    XmToggleButtonSetState(rotate_90, FALSE, FALSE);
  }
  else
  {
    print_auto_rotation = 0;
  }
}





/*
 *  Rotate_90
 *
 */
static void  Rotate_90( Widget UNUSED(widget), XtPointer clientData, XtPointer callData)
{
  char *which = (char *)clientData;
  XmToggleButtonCallbackStruct *state = (XmToggleButtonCallbackStruct *)callData;

  if(state->set)
  {
    print_rotated = atoi(which);
    print_auto_rotation = 0;
    XmToggleButtonSetState(auto_rotate, FALSE, FALSE);
  }
  else
  {
    print_rotated = 0;
  }
}





/*
 *  Auto_scale
 *
 */
static void  Auto_scale( Widget UNUSED(widget), XtPointer clientData, XtPointer callData)
{
  char *which = (char *)clientData;
  XmToggleButtonCallbackStruct *state = (XmToggleButtonCallbackStruct *)callData;

  if(state->set)
  {
    print_auto_scale = atoi(which);
  }
  else
  {
    print_auto_scale = 0;
  }
}





/*
 *  Monochrome
 *
 */
void  Monochrome( Widget UNUSED(widget), XtPointer clientData, XtPointer callData)
{
  char *which = (char *)clientData;
  XmToggleButtonCallbackStruct *state = (XmToggleButtonCallbackStruct *)callData;

  if(state->set)
  {
    print_in_monochrome = atoi(which);
  }
  else
  {
    print_in_monochrome = 0;
  }
}





/*
 *  Invert
 *
 */
static void  Invert( Widget UNUSED(widget), XtPointer clientData, XtPointer callData)
{
  char *which = (char *)clientData;
  XmToggleButtonCallbackStruct *state = (XmToggleButtonCallbackStruct *)callData;

  if(state->set)
  {
    print_invert = atoi(which);
  }
  else
  {
    print_invert = 0;
  }
}





// Print_properties:  Prints the drawing area to a PostScript file.
// Provides various togglebuttons for configuring the "gv" previewer
// only.
//
// Perhaps later:
// 1) Select an area on the screen to print
// 2) -label
//
void Print_properties( Widget w, XtPointer UNUSED(clientData), XtPointer UNUSED(callData) )
{
  static Widget pane, scrollwindow, form, button_ok, button_cancel,
         sep, auto_scale,
//            paper_size, paper_size_data, scale, scale_data, blank_background,
//            res_label1, res_label2, res_x, res_y,
         monochrome, invert;
  Atom delw;

  // Get rid of the Print dialog
  Print_postscript_destroy_shell(w, print_postscript_dialog, NULL );


  // If we're not using "gv", skip the entire dialog below and go
  // straight to the actual previewer function.
  //
  if ( !strstr(previewer_program,"gv") )
  {
    Print_preview(w, NULL, NULL);
    return;
  }


  if (!print_properties_dialog)
  {


    begin_critical_section(&print_properties_dialog_lock, "maps.c:Print_properties" );


    print_properties_dialog = XtVaCreatePopupShell(langcode("PRINT0001"),
                              xmDialogShellWidgetClass, appshell,
                              XmNdeleteResponse, XmDESTROY,
                              XmNdefaultPosition, FALSE,
                              XmNfontList, fontlist1,
                              NULL);

    pane = XtVaCreateWidget("Print_properties pane",xmPanedWindowWidgetClass, print_properties_dialog,
                            XmNbackground, colors[0xff],
                            NULL);

    scrollwindow = XtVaCreateManagedWidget("scrollwindow",
                                           xmScrolledWindowWidgetClass,
                                           pane,
                                           XmNscrollingPolicy, XmAUTOMATIC,
                                           NULL);

    form =  XtVaCreateWidget("Print_properties form",
                             xmFormWidgetClass,
                             scrollwindow,
                             XmNfractionBase, 2,
                             XmNbackground, colors[0xff],
                             XmNautoUnmanage, FALSE,
                             XmNshadowThickness, 1,
                             NULL);

    /*
            paper_size = XtVaCreateManagedWidget(langcode("PRINT0002"),xmLabelWidgetClass, form,
                                          XmNtopAttachment, XmATTACH_FORM,
                                          XmNtopOffset, 10,
                                          XmNbottomAttachment, XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_FORM,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment, XmATTACH_NONE,
                                          XmNbackground, colors[0xff],
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(paper_size,FALSE);


            paper_size_data = XtVaCreateManagedWidget("Print_properties paper_size_data", xmTextFieldWidgetClass, form,
                                          XmNeditable,   TRUE,
                                          XmNcursorPositionVisible, TRUE,
                                          XmNsensitive, TRUE,
                                          XmNshadowThickness,    1,
                                          XmNcolumns, 15,
                                          XmNwidth, ((15*7)+2),
                                          XmNmaxLength, 15,
                                          XmNbackground, colors[0x0f],
                                          XmNtopAttachment,XmATTACH_FORM,
                                          XmNtopOffset, 5,
                                          XmNbottomAttachment,XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_WIDGET,
                                          XmNleftWidget, paper_size,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment,XmATTACH_FORM,
                                          XmNrightOffset, 10,
                                          XmNnavigationType, XmTAB_GROUP,
                                          XmNtraversalOn, TRUE,
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(paper_size_data,FALSE);
    */


    auto_rotate  = XtVaCreateManagedWidget(langcode("PRINT0003"),xmToggleButtonWidgetClass,form,
//                                      XmNtopAttachment, XmATTACH_WIDGET,
//                                      XmNtopWidget, paper_size_data,
//                                      XmNtopOffset, 5,
                                           XmNtopAttachment, XmATTACH_FORM,
                                           XmNtopOffset, 10,
                                           XmNbottomAttachment, XmATTACH_NONE,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNleftOffset,10,
                                           XmNrightAttachment, XmATTACH_NONE,
                                           XmNbackground, colors[0xff],
                                           XmNnavigationType, XmTAB_GROUP,
                                           XmNtraversalOn, TRUE,
                                           XmNfontList, fontlist1,
                                           NULL);
    XtAddCallback(auto_rotate,XmNvalueChangedCallback,Auto_rotate,"1");


    rotate_90  = XtVaCreateManagedWidget(langcode("PRINT0004"),xmToggleButtonWidgetClass,form,
//                                      XmNtopAttachment, XmATTACH_WIDGET,
//                                      XmNtopWidget, paper_size_data,
//                                      XmNtopOffset, 5,
                                         XmNtopAttachment, XmATTACH_FORM,
                                         XmNtopOffset, 10,
                                         XmNbottomAttachment, XmATTACH_NONE,
                                         XmNleftAttachment, XmATTACH_WIDGET,
                                         XmNleftWidget, auto_rotate,
                                         XmNleftOffset,10,
                                         XmNrightAttachment, XmATTACH_FORM,
                                         XmNrightOffset, 10,
                                         XmNbackground, colors[0xff],
                                         XmNnavigationType, XmTAB_GROUP,
                                         XmNtraversalOn, TRUE,
                                         XmNfontList, fontlist1,
                                         NULL);
    XtAddCallback(rotate_90,XmNvalueChangedCallback,Rotate_90,"1");


    auto_scale = XtVaCreateManagedWidget(langcode("PRINT0005"),xmToggleButtonWidgetClass,form,
                                         XmNtopAttachment, XmATTACH_WIDGET,
                                         XmNtopWidget, auto_rotate,
                                         XmNtopOffset, 5,
                                         XmNbottomAttachment, XmATTACH_NONE,
                                         XmNleftAttachment, XmATTACH_FORM,
                                         XmNleftOffset,10,
                                         XmNrightAttachment, XmATTACH_NONE,
                                         XmNbackground, colors[0xff],
                                         XmNnavigationType, XmTAB_GROUP,
                                         XmNtraversalOn, TRUE,
                                         XmNfontList, fontlist1,
                                         NULL);
    XtAddCallback(auto_scale,XmNvalueChangedCallback,Auto_scale,"1");


    /*
            scale = XtVaCreateManagedWidget(langcode("PRINT0006"),xmLabelWidgetClass, form,
                                          XmNtopAttachment, XmATTACH_WIDGET,
                                          XmNtopWidget, auto_rotate,
                                          XmNtopOffset, 10,
                                          XmNbottomAttachment, XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_WIDGET,
                                          XmNleftWidget, auto_scale,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment, XmATTACH_NONE,
                                          XmNbackground, colors[0xff],
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(scale,FALSE);


            scale_data = XtVaCreateManagedWidget("Print_properties scale_data", xmTextFieldWidgetClass, form,
                                          XmNeditable,   TRUE,
                                          XmNcursorPositionVisible, TRUE,
                                          XmNsensitive, TRUE,
                                          XmNshadowThickness,    1,
                                          XmNcolumns, 15,
                                          XmNwidth, ((15*7)+2),
                                          XmNmaxLength, 15,
                                          XmNbackground, colors[0x0f],
                                          XmNtopAttachment,XmATTACH_WIDGET,
                                          XmNtopWidget, auto_rotate,
                                          XmNtopOffset, 5,
                                          XmNbottomAttachment,XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_WIDGET,
                                          XmNleftWidget, scale,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment,XmATTACH_FORM,
                                          XmNrightOffset, 10,
                                          XmNnavigationType, XmTAB_GROUP,
                                          XmNtraversalOn, TRUE,
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(scale_data,FALSE);
    */


    /*
            blank_background = XtVaCreateManagedWidget(langcode("PRINT0007"),xmToggleButtonWidgetClass,form,
                                          XmNtopAttachment, XmATTACH_WIDGET,
                                          XmNtopWidget, scale_data,
                                          XmNtopWidget, auto_rotate,
                                          XmNtopOffset, 5,
                                          XmNbottomAttachment, XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_FORM,
                                          XmNleftOffset ,10,
                                          XmNrightAttachment, XmATTACH_NONE,
                                          XmNbackground, colors[0xff],
                                          XmNnavigationType, XmTAB_GROUP,
                                          XmNtraversalOn, TRUE,
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(blank_background,FALSE);
    */


    monochrome = XtVaCreateManagedWidget(langcode("PRINT0008"),xmToggleButtonWidgetClass,form,
                                         XmNtopAttachment, XmATTACH_WIDGET,
//                                      XmNtopWidget, blank_background,
                                         XmNtopWidget, auto_scale,
                                         XmNtopOffset, 5,
                                         XmNbottomAttachment, XmATTACH_NONE,
                                         XmNleftAttachment, XmATTACH_FORM,
                                         XmNleftOffset,10,
                                         XmNrightAttachment, XmATTACH_NONE,
                                         XmNbackground, colors[0xff],
                                         XmNnavigationType, XmTAB_GROUP,
                                         XmNtraversalOn, TRUE,
                                         XmNfontList, fontlist1,
                                         NULL);
    XtAddCallback(monochrome,XmNvalueChangedCallback,Monochrome,"1");


    invert = XtVaCreateManagedWidget(langcode("PRINT0016"),xmToggleButtonWidgetClass,form,
                                     XmNtopAttachment, XmATTACH_WIDGET,
                                     XmNtopWidget, monochrome,
                                     XmNtopOffset, 5,
                                     XmNbottomAttachment, XmATTACH_NONE,
                                     XmNleftAttachment, XmATTACH_FORM,
                                     XmNleftOffset,10,
                                     XmNrightAttachment, XmATTACH_NONE,
                                     XmNbackground, colors[0xff],
                                     XmNnavigationType, XmTAB_GROUP,
                                     XmNtraversalOn, TRUE,
                                     XmNfontList, fontlist1,
                                     NULL);
    XtAddCallback(invert,XmNvalueChangedCallback,Invert,"1");


    /*
            res_label1 = XtVaCreateManagedWidget(langcode("PRINT0009"),xmLabelWidgetClass, form,
                                          XmNtopAttachment, XmATTACH_WIDGET,
                                          XmNtopWidget, invert,
                                          XmNtopOffset, 10,
                                          XmNbottomAttachment, XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_FORM,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment, XmATTACH_NONE,
                                          XmNbackground, colors[0xff],
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(res_label1,FALSE);


            res_x = XtVaCreateManagedWidget("Print_properties resx_data", xmTextFieldWidgetClass, form,
                                          XmNeditable,   TRUE,
                                          XmNcursorPositionVisible, TRUE,
                                          XmNsensitive, TRUE,
                                          XmNshadowThickness,    1,
                                          XmNcolumns, 15,
                                          XmNwidth, ((15*7)+2),
                                          XmNmaxLength, 15,
                                          XmNbackground, colors[0x0f],
                                          XmNtopAttachment,XmATTACH_WIDGET,
                                          XmNtopWidget, invert,
                                          XmNtopOffset, 5,
                                          XmNbottomAttachment,XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_WIDGET,
                                          XmNleftWidget, res_label1,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment,XmATTACH_NONE,
                                          XmNnavigationType, XmTAB_GROUP,
                                          XmNtraversalOn, TRUE,
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(res_x,FALSE);


            res_label2 = XtVaCreateManagedWidget("X",xmLabelWidgetClass, form,
                                          XmNtopAttachment, XmATTACH_WIDGET,
                                          XmNtopWidget, invert,
                                          XmNtopOffset, 10,
                                          XmNbottomAttachment, XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_WIDGET,
                                          XmNleftWidget, res_x,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment, XmATTACH_NONE,
                                          XmNbackground, colors[0xff],
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(res_label2,FALSE);


            res_y = XtVaCreateManagedWidget("Print_properties res_y_data", xmTextFieldWidgetClass, form,
                                          XmNeditable,   TRUE,
                                          XmNcursorPositionVisible, TRUE,
                                          XmNsensitive, TRUE,
                                          XmNshadowThickness,    1,
                                          XmNcolumns, 15,
                                          XmNwidth, ((15*7)+2),
                                          XmNmaxLength, 15,
                                          XmNbackground, colors[0x0f],
                                          XmNtopAttachment,XmATTACH_WIDGET,
                                          XmNtopWidget, invert,
                                          XmNtopOffset, 5,
                                          XmNbottomAttachment,XmATTACH_NONE,
                                          XmNleftAttachment, XmATTACH_WIDGET,
                                          XmNleftWidget, res_label2,
                                          XmNleftOffset, 10,
                                          XmNrightAttachment,XmATTACH_FORM,
                                          XmNrightOffset, 10,
                                          XmNnavigationType, XmTAB_GROUP,
                                          XmNtraversalOn, TRUE,
                                          XmNfontList, fontlist1,
                                          NULL);
    XtSetSensitive(res_y,FALSE);
    */


    sep = XtVaCreateManagedWidget("Print_properties sep", xmSeparatorGadgetClass,form,
                                  XmNorientation, XmHORIZONTAL,
                                  XmNtopAttachment,XmATTACH_WIDGET,
//                                      XmNtopWidget, res_y,
                                  XmNtopWidget, invert,
                                  XmNtopOffset, 10,
                                  XmNbottomAttachment,XmATTACH_NONE,
                                  XmNleftAttachment, XmATTACH_FORM,
                                  XmNrightAttachment,XmATTACH_FORM,
                                  XmNbackground, colors[0xff],
                                  XmNfontList, fontlist1,
                                  NULL);


//        button_ok = XtVaCreateManagedWidget(langcode("PRINT0011"),xmPushButtonGadgetClass, form,
    button_ok = XtVaCreateManagedWidget(langcode("PRINT0010"),xmPushButtonGadgetClass, form,
                                        XmNtopAttachment, XmATTACH_WIDGET,
                                        XmNtopWidget, sep,
                                        XmNtopOffset, 5,
                                        XmNbottomAttachment, XmATTACH_FORM,
                                        XmNbottomOffset, 5,
                                        XmNleftAttachment, XmATTACH_POSITION,
                                        XmNleftPosition, 0,
                                        XmNleftOffset, 3,
                                        XmNrightAttachment, XmATTACH_POSITION,
                                        XmNrightPosition, 1,
                                        XmNrightOffset, 2,
                                        XmNbackground, colors[0xff],
                                        XmNnavigationType, XmTAB_GROUP,
                                        XmNtraversalOn, TRUE,
                                        XmNfontList, fontlist1,
                                        NULL);


    button_cancel = XtVaCreateManagedWidget(langcode("UNIOP00002"),xmPushButtonGadgetClass, form,
                                            XmNtopAttachment, XmATTACH_WIDGET,
                                            XmNtopWidget, sep,
                                            XmNtopOffset, 5,
                                            XmNbottomAttachment, XmATTACH_FORM,
                                            XmNbottomOffset, 5,
                                            XmNleftAttachment, XmATTACH_POSITION,
                                            XmNleftPosition, 1,
                                            XmNleftOffset, 3,
                                            XmNrightAttachment, XmATTACH_POSITION,
                                            XmNrightPosition, 2,
                                            XmNrightOffset, 5,
                                            XmNbackground, colors[0xff],
                                            XmNnavigationType, XmTAB_GROUP,
                                            XmNtraversalOn, TRUE,
                                            XmNfontList, fontlist1,
                                            NULL);


    XtAddCallback(button_ok, XmNactivateCallback, Print_preview, NULL );
    XtAddCallback(button_cancel, XmNactivateCallback, Print_properties_destroy_shell, print_properties_dialog);


    XmToggleButtonSetState(rotate_90,FALSE,FALSE);
    XmToggleButtonSetState(auto_rotate,TRUE,FALSE);


    if (print_auto_rotation)
    {
      XmToggleButtonSetState(auto_rotate, TRUE, TRUE);
    }
    else
    {
      XmToggleButtonSetState(auto_rotate, FALSE, TRUE);
    }


    if (print_rotated)
    {
      XmToggleButtonSetState(rotate_90, TRUE, TRUE);
    }
    else
    {
      XmToggleButtonSetState(rotate_90, FALSE, TRUE);
    }


    if (print_in_monochrome)
    {
      XmToggleButtonSetState(monochrome, TRUE, FALSE);
    }
    else
    {
      XmToggleButtonSetState(monochrome, FALSE, FALSE);
    }


    if (print_invert)
    {
      XmToggleButtonSetState(invert, TRUE, FALSE);
    }
    else
    {
      XmToggleButtonSetState(invert, FALSE, FALSE);
    }


    if (print_auto_scale)
    {
      XmToggleButtonSetState(auto_scale, TRUE, TRUE);
    }
    else
    {
      XmToggleButtonSetState(auto_scale, FALSE, TRUE);
    }


//        XmTextFieldSetString(paper_size_data,print_paper_size);


    end_critical_section(&print_properties_dialog_lock, "maps.c:Print_properties" );


    pos_dialog(print_properties_dialog);


    delw = XmInternAtom(XtDisplay(print_properties_dialog),"WM_DELETE_WINDOW", FALSE);
    XmAddWMProtocolCallback(print_properties_dialog, delw, Print_properties_destroy_shell, (XtPointer)print_properties_dialog);


    XtManageChild(form);
    XtManageChild(pane);

    resize_dialog(form, print_properties_dialog);

    XtPopup(print_properties_dialog,XtGrabNone);


    // Move focus to the Cancel button.  This appears to highlight the
    // button fine, but we're not able to hit the <Enter> key to
    // have that default function happen.  Note:  We _can_ hit the
    // <SPACE> key, and that activates the option.
//        XmUpdateDisplay(print_properties_dialog);
    XmProcessTraversal(button_cancel, XmTRAVERSE_CURRENT);


  }
  else
  {
    (void)XRaiseWindow(XtDisplay(print_properties_dialog), XtWindow(print_properties_dialog));
  }
}





// General print dialog.  From here we can either print Postscript
// files to the device selected in this dialog, or head off to a
// print preview program that might allow us a variety of print
// options.  From here we should be able to set the print device
// and the print preview program & path.
//
void Print_Postscript( Widget UNUSED(w), XtPointer UNUSED(clientData), XtPointer UNUSED(callData) )
{
  static Widget pane, scrollwindow, form, button_print, button_cancel,
         sep, button_preview;
  Atom delw;

  if (!print_postscript_dialog)
  {


    begin_critical_section(&print_postscript_dialog_lock, "maps.c:Print_Postscript" );


    print_postscript_dialog = XtVaCreatePopupShell(langcode("PULDNFI015"),
                              xmDialogShellWidgetClass, appshell,
                              XmNdeleteResponse, XmDESTROY,
                              XmNdefaultPosition, FALSE,
                              XmNfontList, fontlist1,
                              NULL);

    pane = XtVaCreateWidget("Print_postscript pane",xmPanedWindowWidgetClass, print_postscript_dialog,
                            XmNbackground, colors[0xff],
                            NULL);

    scrollwindow = XtVaCreateManagedWidget("scrollwindow",
                                           xmScrolledWindowWidgetClass,
                                           pane,
                                           XmNscrollingPolicy, XmAUTOMATIC,
                                           NULL);

    form =  XtVaCreateWidget("Print_postscript form",
                             xmFormWidgetClass,
                             scrollwindow,
                             XmNfractionBase, 3,
                             XmNbackground, colors[0xff],
                             XmNautoUnmanage, FALSE,
                             XmNshadowThickness, 1,
                             NULL);

    // "Direct to:"
    button_print = XtVaCreateManagedWidget(langcode("PRINT1001"),xmPushButtonGadgetClass, form,
                                           XmNtopAttachment, XmATTACH_FORM,
                                           XmNtopOffset, 5,
                                           XmNbottomAttachment, XmATTACH_NONE,
                                           XmNleftAttachment, XmATTACH_FORM,
                                           XmNleftOffset, 5,
                                           XmNrightAttachment, XmATTACH_NONE,
                                           XmNbackground, colors[0xff],
                                           XmNnavigationType, XmTAB_GROUP,
                                           XmNtraversalOn, TRUE,
                                           XmNfontList, fontlist1,
                                           NULL);


    printer_data = XtVaCreateManagedWidget("Print_Postscript printer_data", xmTextFieldWidgetClass, form,
                                           XmNeditable,   TRUE,
                                           XmNcursorPositionVisible, TRUE,
                                           XmNsensitive, TRUE,
                                           XmNshadowThickness,    1,
                                           XmNcolumns, 40,
                                           XmNwidth, ((40*7)+2),
                                           XmNmaxLength, MAX_FILENAME,
                                           XmNbackground, colors[0x0f],
                                           XmNtopAttachment,XmATTACH_FORM,
                                           XmNtopOffset, 5,
                                           XmNbottomAttachment,XmATTACH_NONE,
                                           XmNleftAttachment, XmATTACH_WIDGET,
                                           XmNleftWidget, button_print,
                                           XmNleftOffset, 10,
                                           XmNrightAttachment,XmATTACH_FORM,
                                           XmNrightOffset, 5,
                                           XmNnavigationType, XmTAB_GROUP,
                                           XmNtraversalOn, TRUE,
                                           XmNfontList, fontlist1,
                                           NULL);


    // "Via Previewer:"
    button_preview = XtVaCreateManagedWidget(langcode("PRINT1002"),xmPushButtonGadgetClass, form,
                     XmNtopAttachment, XmATTACH_WIDGET,
                     XmNtopWidget, button_print,
                     XmNtopOffset, 5,
                     XmNbottomAttachment, XmATTACH_NONE,
                     XmNleftAttachment, XmATTACH_FORM,
                     XmNleftOffset, 5,
                     XmNrightAttachment, XmATTACH_NONE,
                     XmNbackground, colors[0xff],
                     XmNnavigationType, XmTAB_GROUP,
                     XmNtraversalOn, TRUE,
                     XmNfontList, fontlist1,
                     NULL);


    previewer_data = XtVaCreateManagedWidget("Print_Postscript previewer_data", xmTextFieldWidgetClass, form,
                     XmNeditable,   TRUE,
                     XmNcursorPositionVisible, TRUE,
                     XmNsensitive, TRUE,
                     XmNshadowThickness,    1,
                     XmNcolumns, 40,
                     XmNwidth, ((40*7)+2),
                     XmNmaxLength, MAX_FILENAME,
                     XmNbackground, colors[0x0f],
                     XmNtopAttachment,XmATTACH_WIDGET,
                     XmNtopWidget, button_print,
                     XmNtopOffset, 5,
                     XmNbottomAttachment,XmATTACH_NONE,
                     XmNleftAttachment, XmATTACH_WIDGET,
                     XmNleftWidget, button_preview,
                     XmNleftOffset, 10,
                     XmNrightAttachment,XmATTACH_FORM,
                     XmNrightOffset, 5,
                     XmNnavigationType, XmTAB_GROUP,
                     XmNtraversalOn, TRUE,
                     XmNfontList, fontlist1,
                     NULL);


    sep = XtVaCreateManagedWidget("Print_postscript sep", xmSeparatorGadgetClass,form,
                                  XmNorientation, XmHORIZONTAL,
                                  XmNtopAttachment,XmATTACH_WIDGET,
                                  XmNtopWidget, button_preview,
                                  XmNtopOffset, 10,
                                  XmNbottomAttachment,XmATTACH_NONE,
                                  XmNleftAttachment, XmATTACH_FORM,
                                  XmNrightAttachment,XmATTACH_FORM,
                                  XmNbackground, colors[0xff],
                                  XmNfontList, fontlist1,
                                  NULL);


    button_cancel = XtVaCreateManagedWidget(langcode("UNIOP00002"),xmPushButtonGadgetClass, form,
                                            XmNtopAttachment, XmATTACH_WIDGET,
                                            XmNtopWidget, sep,
                                            XmNtopOffset, 5,
                                            XmNbottomAttachment, XmATTACH_FORM,
                                            XmNbottomOffset, 5,
                                            XmNleftAttachment, XmATTACH_FORM,
                                            XmNleftOffset, 5,
                                            XmNrightAttachment, XmATTACH_FORM,
                                            XmNrightOffset, 5,
                                            XmNbackground, colors[0xff],
                                            XmNnavigationType, XmTAB_GROUP,
                                            XmNtraversalOn, TRUE,
                                            XmNfontList, fontlist1,
                                            NULL);


    XtAddCallback(button_preview, XmNactivateCallback, Print_properties, NULL );
    XtAddCallback(button_print, XmNactivateCallback, Print_window, NULL );
    XtAddCallback(button_cancel, XmNactivateCallback, Print_postscript_destroy_shell, print_postscript_dialog);

    // Fill in the text fields from persistent variables out of the config file.
    XmTextFieldSetString(printer_data, printer_program);
    XmTextFieldSetString(previewer_data, previewer_program);

    end_critical_section(&print_postscript_dialog_lock, "maps.c:Print_Postscript" );


    pos_dialog(print_postscript_dialog);


    delw = XmInternAtom(XtDisplay(print_postscript_dialog),"WM_DELETE_WINDOW", FALSE);
    XmAddWMProtocolCallback(print_postscript_dialog, delw, Print_postscript_destroy_shell, (XtPointer)print_postscript_dialog);


    XtManageChild(form);
    XtManageChild(pane);

    resize_dialog(form, print_postscript_dialog);

    XtPopup(print_postscript_dialog,XtGrabNone);

    // Move focus to the Cancel button.  This appears to highlight the
    // button fine, but we're not able to hit the <Enter> key to
    // have that default function happen.  Note:  We _can_ hit the
    // <SPACE> key, and that activates the option.
//        XmUpdateDisplay(print_postscript_dialog);
    XmProcessTraversal(button_cancel, XmTRAVERSE_CURRENT);


  }
  else
  {
    (void)XRaiseWindow(XtDisplay(print_postscript_dialog), XtWindow(print_postscript_dialog));
  }
}





// Create png image (for use in web browsers??).  Requires that "convert"
// from the ImageMagick package be installed on the system.  At the
// point this thread is started, the XPM file has already been
// created.  We now create a .geo file to go with the .png file.
//
#ifndef NO_XPM
static void* snapshot_thread(void * UNUSED(arg) )
{
  char xpm_filename[MAX_FILENAME];
  char png_filename[MAX_FILENAME];
  char geo_filename[MAX_FILENAME];
  char kml_filename[MAX_FILENAME];   // filename for kml file that describes the png file in keyhole markup language
  char timestring[101];  // string representation of the time heard or the current time
  FILE *f;
  FILE *fk;  // file handle for kml file
  time_t expire_time;
#ifdef HAVE_CONVERT
  char command[MAX_FILENAME*2];
#endif  // HAVE_CONVERT
  char temp_base_dir[MAX_VALUE];

  get_user_base_dir("tmp", temp_base_dir, sizeof(temp_base_dir));


  // The pthread_detach() call means we don't care about the
  // return code and won't use pthread_join() later.  Makes
  // threading more efficient.
  (void)pthread_detach(pthread_self());

  astir_snprintf(xpm_filename,
                  sizeof(xpm_filename),
                  "%s/snapshot.xpm",
                  temp_base_dir);

  astir_snprintf(png_filename,
                  sizeof(png_filename),
                  "%s/snapshot.png",
                  temp_base_dir);

  // Same for the .geo filename
  astir_snprintf(geo_filename,
                  sizeof(geo_filename),
                  "%s/snapshot.geo",
                  temp_base_dir);

  // Same for the .kml filename
  astir_snprintf(kml_filename,
                  sizeof(kml_filename),
                  "%s/snapshot.kml",
                  temp_base_dir);


  // Create a .geo file to match the new png image
  // Likewise for a matching .kml file
  f = fopen(geo_filename,"w");    // Overwrite whatever file
  // is there.
  fk = fopen(kml_filename,"w");

  if (f == NULL || fk == NULL)
  {
    if (f==NULL)
    {
      fprintf(stderr,"Couldn't open %s\n",geo_filename);
    }
    if (fk==NULL)
    {
      fprintf(stderr,"Couldn't open %s\n",kml_filename);
    }
  }
  else
  {
    float lat1, long1, lat2, long2;


    long1 = f_NW_corner_longitude;
    lat1 = f_NW_corner_latitude;
    long2 = f_SE_corner_longitude;
    lat2 = f_SE_corner_latitude;

    // FILENAME   world1.xpm
    // #          x          y        lon         lat
    // TIEPOINT   0          0        -180        90
    // TIEPOINT   639        319      180         -90
    // IMAGESIZE  640        320
    // REFRESH    250

    fprintf(f,"FILENAME     snapshot.png\n");
    fprintf(f,"#            x       y        lon           lat\n");
    fprintf(f,"TIEPOINT     0       0       %8.5f     %8.5f\n",
            long1, lat1);
    fprintf(f,"TIEPOINT     %-4d    %-4d    %8.5f     %8.5f\n",
            (int)screen_width-1, (int)screen_height-1, long2, lat2);

    fprintf(f,"IMAGESIZE    %-4d    %-4d\n",
            (int)screen_width, (int)screen_height);
    fprintf(f,"REFRESH      250\n");
    fclose(f);

    // Write a matching kml file that describes the location of the snapshot on
    // the Earth's surface.
    // Another kml file pointing to the location of this file with a networklinkcontrol element
    // and an update element loaded into a kml application should be able to reload this file
    // at regular intervals.
    // See kml documentation of:
    // <kml><NetworkLinkControl><linkName/><refreshMode/>
    //
    // <?xml version="1.0" encoding="UTF-8"?>
    // <kml xmlns="http://earth.google.com/kml/2.1">
    // <Document>
    //   <NetworkLink>
    //      <Link>
    //        <href>http://www.example.com/cgi-bin/screenshot.kml</href>
    //        <refreshMode>onExpire</refreshMode>
    //      </Link>
    //  </NetworkLink>
    // </Document>
    // </kml>
    //
    // TODO: Calculate a suitable range and tilt for viewing the snapshot draped on the
    // underlying terrain.

    fprintf(fk,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(fk,"<kml xmlns=\"http://earth.google.com/kml/2.2\">\n");
    // Add an expire time matching the time when the next snapshot should
    // be produced, so that a network link with an onExpire refresh mode
    // will check for the next snapshot.
    expire_time = sec_now() + (time_t)(snapshot_interval * 60);
    if (get_w3cdtf_datetime(expire_time, timestring, False, False))
    {
      if (strlen(timestring) > 0)
      {
        fprintf(fk,"  <NetworkLinkControl>\n");
        fprintf(fk,"     <expires>%s</expires>\n",timestring);
        fprintf(fk,"  </NetworkLinkControl>\n");
      }
    }
    fprintf(fk,"  <Document>\n");
    fprintf(fk,"    <name>ASTIR Snapshot from %s</name>\n",my_callsign);
    fprintf(fk,"    <open>1</open>\n");
    fprintf(fk,"    <GroundOverlay>\n");
    fprintf(fk,"      <name>Astir snapshot</name>\n");
    fprintf(fk,"      <visibility>1</visibility>\n");
    // timestamp the overlay with the current time
    if (get_w3cdtf_datetime(sec_now(), timestring, True, True))
    {
      if (strlen(timestring) > 0)
      {
        fprintf(fk,"      <TimeStamp><when>%s</when></TimeStamp>\n",timestring);
        fprintf(fk,"      <description>Overlay shows screen visible for %s in Astir at %s.</description>\n",my_callsign,timestring);
      }
    }
    else
    {
      fprintf(fk,"      <description>Overlay shows screen visible for %s in Astir.</description>\n",my_callsign);
    }
    fprintf(fk,"      <LookAt>\n");
    fprintf(fk,"        <longitude>%8.5f</longitude>\n",f_center_longitude);
    fprintf(fk,"        <latitude>%8.5f</latitude>\n",f_center_latitude);
    fprintf(fk,"        <altitude>0</altitude>\n");
    fprintf(fk,"        <range>30350.36838438907</range>\n");  // range in meters from viewer to lookat point
    fprintf(fk,"        <tilt>0</tilt>\n");  // 0 is looking straight down
    fprintf(fk,"        <altitudeMode>clampToGround</altitudeMode>\n");
    fprintf(fk,"        <heading>0</heading>\n");  // 0 is north at top, 90 east at top
    fprintf(fk,"      </LookAt>\n");
    fprintf(fk,"      <Icon>\n");
    fprintf(fk,"        <href>snapshot.png</href>\n");
    fprintf(fk,"      </Icon>\n");
    fprintf(fk,"      <LatLonBox>\n");
    fprintf(fk,"        <north>%8.5f</north>\n",lat1);
    fprintf(fk,"        <south>%8.5f</south>\n",lat2);
    fprintf(fk,"        <east>%8.5f</east>\n",long2);
    fprintf(fk,"        <west>%8.5f</west>\n",long1);
    fprintf(fk,"        <rotation>0</rotation>\n");
    fprintf(fk,"      </LatLonBox>\n");
    fprintf(fk,"    </GroundOverlay>\n");
    fprintf(fk,"  </Document>\n");
    fprintf(fk,"</kml>\n");

    fclose(fk);

    chmod( geo_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );
    chmod( kml_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );
  }


  if ( debug_level & 512 )
  {
    fprintf(stderr,"Convert %s ==> %s\n", xpm_filename, png_filename );
  }


#ifdef HAVE_CONVERT
  // Convert it to a png file.  This depends upon having the
  // ImageMagick command "convert" installed.
  strcpy(command, CONVERT_PATH);
  command[sizeof(command)-1] = '\0';  // Terminate string
  strcat(command, " -quality 100 -colors 256 ");
  command[sizeof(command)-1] = '\0';  // Terminate string
  strcat(command, xpm_filename);
  command[sizeof(command)-1] = '\0';  // Terminate string
  strcat(command, " ");
  command[sizeof(command)-1] = '\0';  // Terminate string
  strcat(command, png_filename);
  command[sizeof(command)-1] = '\0';  // Terminate string

  if ( system( command ) != 0 )
  {
    // We _may_ have had an error.  Check errno to make
    // sure.
    if (errno)
    {
      fprintf(stderr, "%s\n", strerror(errno));
      fprintf(stderr,
              "Failed to convert snapshot: %s -> %s\n",
              xpm_filename,
              png_filename);
    }
    else
    {
      fprintf(stderr,
              "System call return error: convert: %s -> %s\n",
              xpm_filename,
              png_filename);
    }
  }
  else
  {
    chmod( png_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );

//        // Delete temporary xpm file
//        unlink( xpm_filename );

    if ( debug_level & 512 )
    {
      fprintf(stderr,"  Done creating png.\n");
    }
  }

#endif  // HAVE_CONVERT

  // Signify that we're all done and that another snapshot can
  // occur.
  doing_snapshot = 0;

  return(NULL);
}
#endif  // NO_XPM





// Starts a separate thread that creates a png image from the
// current displayed image.
//
void Snapshot(void)
{
#ifndef NO_XPM
  pthread_t snapshot_thread_id;
  char xpm_filename[MAX_FILENAME];
  int xpmretval;
#endif  // NO_XPM
  char temp_base_dir[MAX_VALUE];

  get_user_base_dir("tmp", temp_base_dir, sizeof(temp_base_dir));


  // Check whether we're already doing a snapshot
  if (doing_snapshot)
  {
    return;
  }

  // Time to take another snapshot?
  // New snapshot interval based on slider in Configure Timing
  // dialog (in minutes)
  if (sec_now() < (last_snapshot + (snapshot_interval * 60)) )
  {
    return;
  }

  last_snapshot = sec_now(); // Set up timer for next time


#ifndef NO_XPM

  if (debug_level & 512)
  {
    fprintf(stderr,"Taking Snapshot\n");
  }

  doing_snapshot++;

  // Set up the XPM filename that we'll use
  astir_snprintf(xpm_filename,
                  sizeof(xpm_filename),
                  "%s/snapshot.xpm",
                  temp_base_dir);


  if ( debug_level & 512 )
  {
    fprintf(stderr,"Creating %s\n", xpm_filename );
  }

  // Create an XPM file from pixmap_final.
  if (chdir(temp_base_dir) != 0)
  {
    fprintf(stderr,"Couldn't chdir to %s directory for snapshot\n", temp_base_dir);
    return;
  }

  xpmretval=XpmWriteFileFromPixmap(XtDisplay(appshell),   // Display *display
                                   "snapshot.xpm",                             // char *filename
                                   pixmap_final,                               // Pixmap pixmap
                                   (Pixmap)NULL,                               // Pixmap shapemask
                                   NULL );

  if (xpmretval != XpmSuccess)
  {
    fprintf(stderr,"ERROR writing %s: %s\n", xpm_filename,
            XpmGetErrorString(xpmretval));
    return;
  }

  chmod( xpm_filename, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH );


//----- Start New Thread -----

  //
  // Here we start a new thread.  We'll communicate with the main
  // thread via global variables.  Use mutex locks if there might
  // be a conflict as to when/how we're updating those variables.
  //

  if (pthread_create(&snapshot_thread_id, NULL, snapshot_thread, NULL))
  {
    fprintf(stderr,"Error creating snapshot thread\n");
  }
  else
  {
    // We're off and running with the new thread!
  }
#endif  // NO_XPM
}