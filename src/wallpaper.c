#include "wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_running(const char *proc_name) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "pgrep -x %s > /dev/null 2>&1", proc_name);
  return system(cmd) == 0;
}

// Extracts trailing path after the last ": " or "= " on a line.
static char *extract_path_from_line(const char *line) {
  const char *marker = strrchr(line, ' ');
  if (!marker)
    return NULL;
  // walk back to find "image: " or "= " prefix patterns
  const char *img = strstr(line, "image: ");
  const char *path_start = img ? img + 7 : marker + 1;

  char *path = strdup(path_start);
  // strip trailing newline
  size_t len = strlen(path);
  while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
    path[--len] = '\0';
  }
  return path;
}

static char *query_swww(void) {
  FILE *fp = popen("swww query", "r");
  if (!fp)
    return NULL;

  char line[1024];
  char *result = NULL;
  if (fgets(line, sizeof(line), fp)) {
    result = extract_path_from_line(line);
  }
  pclose(fp);
  return result;
}

static char *query_hyprpaper(void) {
  FILE *fp = popen("hyprctl hyprpaper listactive", "r");
  if (!fp)
    return NULL;

  char line[1024];
  char *result = NULL;
  if (fgets(line, sizeof(line), fp)) {
    result = extract_path_from_line(line);
  }
  pclose(fp);
  return result;
}

char *wallpaper_get_current_path(void) {
  if (is_running("swww-daemon")) {
    char *path = query_swww();
    if (path)
      return path;
  }
  if (is_running("hyprpaper")) {
    char *path = query_hyprpaper();
    if (path)
      return path;
  }
  return NULL; // caller should fall back to manual color picker
}
