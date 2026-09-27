#ifndef PALETTE_H
#define PALETTE_H

typedef struct {
  unsigned char r, g, b;
} Color;

int palette_extract(const char *image_path, Color *out_colors, int num_colors);

void palette_color_to_hex(Color c, char *out_hex);
void palette_sort_by_brightness(Color *colors, int n);

#endif
