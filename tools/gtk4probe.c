/*
 * gtk4probe.c -- does GTK4 give us a Wayland-native window on THIS machine,
 * and which GSK renderer does it actually choose?
 *
 * The hardware is Ironlake: OpenGL 2.1 / GLES 2.0, no Vulkan.  GTK4's GPU
 * renderers may not be usable, in which case GSK falls back to software.  That
 * is a measurement, not something to assume either way.
 */
#include <gtk/gtk.h>

static gboolean report(gpointer data)
{
  GtkWindow *win = GTK_WINDOW(data);
  GdkDisplay *dpy = gtk_widget_get_display(GTK_WIDGET(win));
  GdkSurface *surf = gtk_native_get_surface(GTK_NATIVE(win));
  GskRenderer *r = gtk_native_get_renderer(GTK_NATIVE(win));

  g_print("GDK display class : %s\n", G_OBJECT_TYPE_NAME(dpy));
  g_print("GDK surface class : %s\n", surf ? G_OBJECT_TYPE_NAME(surf) : "(none)");
  g_print("GSK renderer      : %s\n", r ? G_OBJECT_TYPE_NAME(r) : "(none)");
  g_print("backend is wayland: %s\n",
          strstr(G_OBJECT_TYPE_NAME(dpy), "Wayland") ? "YES" : "NO");

  gtk_window_close(win);
  return G_SOURCE_REMOVE;
}

static void activate(GtkApplication *app, gpointer u)
{
  (void)u;
  GtkWidget *win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "gtk4 probe");
  gtk_window_set_default_size(GTK_WINDOW(win), 320, 200);
  gtk_widget_set_visible(win, TRUE);
  // The renderer is created at realize/map time, so report after the loop turns.
  g_timeout_add(700, report, win);
}

int main(int argc, char **argv)
{
  GtkApplication *app = gtk_application_new("org.astir.gtk4probe",
                                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
