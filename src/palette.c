#include "palette.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  double r, g, b;
} DColor;

static double dist2(DColor a, DColor b) {
  double dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
  return dr * dr + dg * dg + db * db;
}

static void kmeans(DColor *pixels, int n, int k, DColor *centroids,
                   int iterations) {
  srand(42);
  for (int i = 0; i < k; i++)
    centroids[i] = pixels[rand() % n];

  int *assign = malloc(n * sizeof(int));

  for (int iter = 0; iter < iterations; iter++) {
    for (int i = 0; i < n; i++) {
      int best = 0;
      double bestDist = dist2(pixels[i], centroids[0]);
      for (int c = 1; c < k; c++) {
        double d = dist2(pixels[i], centroids[c]);
        if (d < bestDist) {
          bestDist = d;
          best = c;
        }
      }
      assign[i] = best;
    }

    DColor *sums = calloc(k, sizeof(DColor));
    int *counts = calloc(k, sizeof(int));
    for (int i = 0; i < n; i++) {
      int c = assign[i];
      sums[c].r += pixels[i].r;
      sums[c].g += pixels[i].g;
      sums[c].b += pixels[i].b;
      counts[c]++;
    }
    for (int c = 0; c < k; c++) {
      if (counts[c] > 0) {
        centroids[c].r = sums[c].r / counts[c];
        centroids[c].g = sums[c].g / counts[c];
        centroids[c].b = sums[c].b / counts[c];
      }
    }
    free(sums);
    free(counts);
  }
  free(assign);
}

int palette_extract(const char *image_path, Color *out_colors, int num_colors) {
  int w, h, channels;
  unsigned char *data = stbi_load(image_path, &w, &h, &channels, 3);
  if (!data) {
    fprintf(stderr, "palette_extract: failed to load image: %s\n", image_path);
    return 1;
  }

  int total = w * h;
  int step = total > 50000 ? total / 50000 : 1;
  int n = total / step;

  DColor *pixels = malloc(n * sizeof(DColor));
  int idx = 0;
  for (int i = 0; i < total && idx < n; i += step) {
    unsigned char *p = data + i * 3;
    pixels[idx].r = p[0];
    pixels[idx].g = p[1];
    pixels[idx].b = p[2];
    idx++;
  }

  DColor *centroids = malloc(num_colors * sizeof(DColor));
  kmeans(pixels, n, num_colors, centroids, 10);

  for (int i = 0; i < num_colors; i++) {
    out_colors[i].r = (unsigned char)centroids[i].r;
    out_colors[i].g = (unsigned char)centroids[i].g;
    out_colors[i].b = (unsigned char)centroids[i].b;
  }

  free(pixels);
  free(centroids);
  stbi_image_free(data);
  return 0;
}

static double brightness(Color c) {
  return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

static int compare_brightness(const void *a, const void *b) {
  double ba = brightness(*(const Color *)a);
  double bb = brightness(*(const Color *)b);
  return (ba > bb) - (ba < bb);
}

void palette_sort_by_brightness(Color *colors, int n) {
  qsort(colors, n, sizeof(Color), compare_brightness);
}

void palette_color_to_hex(Color c, char *out_hex) {
  snprintf(out_hex, 8, "#%02X%02X%02X", c.r, c.g, c.b);
}
