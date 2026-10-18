#include "theme.h"
#include "gdk/gdk.h"
#include "gtk/gtk.h"
#include "gtk/gtkcssprovider.h"
#include "palette.h"
#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Blends a color partway toward neutral gray to soften anything too
   saturated or too extreme (very dark/very bright) straight from the
   wallpaper palette, so the theme is easier to look at. */
static Color soften(Color c, double amount) {
  Color out;
  out.r = (unsigned char)(c.r + (128 - c.r) * amount);
  out.g = (unsigned char)(c.g + (128 - c.g) * amount);
  out.b = (unsigned char)(c.b + (128 - c.b) * amount);
  return out;
}

/* Formats a color as a CSS rgba() string with the given alpha (0.0-1.0),
   for translucent backgrounds instead of solid hex. */
static void color_to_rgba(Color c, double alpha, char *out, size_t bufsize) {
  snprintf(out, bufsize, "rgba(%d, %d, %d, %.2f)", c.r, c.g, c.b, alpha);
}

void apply_dynamic_theme(GtkWidget *window) {
  (void)window;
  char *wallpaper_path = wallpaper_get_current_path();
  if (!wallpaper_path) {
    fprintf(stderr, "apply_dynamic_theme: no wallpaper detected\n");
    return;
  }

  Color colors[6];
  if (palette_extract(wallpaper_path, colors, 6) != 0) {
    fprintf(stderr, "apply_dynamic_theme: palette extraction failed\n");
    free(wallpaper_path);
    return;
  }
  free(wallpaper_path);

  palette_sort_by_brightness(colors, 6);

  Color bg_c = soften(colors[1], 0.25);
  Color fg_c = soften(colors[5], 0.20);
  Color accent_c = soften(colors[3], 0.15);

  char bg[32], bg_list[32], fg[8], accent[8];
  color_to_rgba(bg_c, 0.55, bg, sizeof(bg));
  color_to_rgba(bg_c, 0.35, bg_list, sizeof(bg_list));
  palette_color_to_hex(fg_c, fg);
  palette_color_to_hex(accent_c, accent);

  char css[768];
  snprintf(css, sizeof(css),
           "window { background-color: %s; }\n"
           "label, entry { color: %s; }\n"
           "entry { border: 1px solid %s; }\n"
           "list { background-color: %s; }\n",
           bg, fg, accent, bg_list);

  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider, css);

  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  g_object_unref(provider);
}
