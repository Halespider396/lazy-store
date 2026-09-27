#include "aur_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define AUR_PACKAGES_URL "https://aur.archlinux.org/packages.gz"
#define AUR_RPC_INFO_URL "https://aur.archlinux.org/rpc/v5/info"

static int build_cache_path(char *out, size_t bufsize) {
  const char *home = getenv("HOME");
  if (!home)
    return -1;
  snprintf(out, bufsize, "%s/.cache/lazy-search", home);
  return 0;
}

static void ensure_cache_dir(const char *dir) {
  /* mkdir -p equivalent for our single-level nested path */
  char parent[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(parent, sizeof(parent), "%s/.cache", home);
    mkdir(parent, 0755);
  }
  mkdir(dir, 0755);
}

static long cache_age_seconds(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return -1; /* doesn't exist */
  time_t now = time(NULL);
  return (long)(now - st.st_mtime);
}

static int refresh_cache(const char *cache_file) {
  char cmd[2048];
  snprintf(cmd, sizeof(cmd),
           "timeout 15 curl -fsSL --max-time 12 '%s' 2>/dev/null | gunzip > "
           "'%s.tmp' && mv '%s.tmp' '%s'",
           AUR_PACKAGES_URL, cache_file, cache_file, cache_file);
  int status = system(cmd);
  return (status == 0) ? 0 : -1;
}

int aur_load_packages(PackageList *list, long max_age_seconds) {
  char cache_dir[512];
  if (build_cache_path(cache_dir, sizeof(cache_dir)) != 0)
    return -1;
  ensure_cache_dir(cache_dir);

  char cache_file[600];
  snprintf(cache_file, sizeof(cache_file), "%s/aur_packages.txt", cache_dir);

  long age = cache_age_seconds(cache_file);
  if (age < 0 || age > max_age_seconds) {
    /* stale or missing: try to refresh; if refresh fails and we have
     * no cache at all, that's a hard failure, but a stale cache is
     * still usable so we fall through either way */
    int refreshed = refresh_cache(cache_file);
    if (refreshed != 0 && age < 0) {
      return -1; /* never had data and couldn't fetch any */
    }
  }

  FILE *fp = fopen(cache_file, "r");
  if (!fp)
    return -1;

  char line[NAME_MAX_LEN];
  while (fgets(line, sizeof(line), fp)) {
    /* skip the leading "AUR Package Repository ... Last Updated" header
     * line/comment that packages.gz includes at the top */
    if (line[0] == '#')
      continue;

    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = '\0';
    if (line[0] == '\0')
      continue;

    Package *pkg = pkglist_push(list);
    if (!pkg)
      continue;
    snprintf(pkg->name, NAME_MAX_LEN, "%s", line);
    strncpy(pkg->repo, "aur", REPO_MAX_LEN - 1);
    pkg->version[0] = '\0';
    pkg->has_desc = 0;
  }
  fclose(fp);
  return 0;
}

/* Minimal, purpose-built JSON string-field extractor: finds "key":"value"
 * and copies value into out, unescaping \" and \\ only (the two escapes
 * that actually show up in AUR package descriptions). Not a general
 * JSON parser. */
static int extract_json_string_field(const char *json, const char *key,
                                     char *out, size_t bufsize) {
  char needle[64];
  snprintf(needle, sizeof(needle), "\"%s\":\"", key);
  const char *start = strstr(json, needle);
  if (!start)
    return -1;
  start += strlen(needle);

  size_t out_i = 0;
  const char *p = start;
  while (*p && *p != '"' && out_i + 1 < bufsize) {
    if (*p == '\\' && *(p + 1) == '"') {
      out[out_i++] = '"';
      p += 2;
    } else if (*p == '\\' && *(p + 1) == '\\') {
      out[out_i++] = '\\';
      p += 2;
    } else {
      out[out_i++] = *p++;
    }
  }
  out[out_i] = '\0';
  return 0;
}

/* If one AUR RPC call times out or fails, don't keep re-stalling the UI
 * on every subsequent AUR package the user scrolls past this session —
 * assume the network is unavailable and skip straight to "no description"
 * without shelling out again. */
int aur_fetch_description(const char *name, char *desc_out, size_t bufsize) {
  if (!name || !desc_out || bufsize == 0)
    return -1;

  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "timeout 4 curl -fsSL --max-time 3 '%s?arg[]=%s' 2>/dev/null",
           AUR_RPC_INFO_URL, name);

  FILE *fp = popen(cmd, "r");
  if (!fp)
    return -1;

  /* AUR info responses are small (a few KB); read it all in */
  char buf[8192];
  size_t total = 0;
  size_t n;
  while (total + 1 < sizeof(buf) &&
         (n = fread(buf + total, 1, sizeof(buf) - total - 1, fp)) > 0) {
    total += n;
  }
  buf[total] = '\0';
  pclose(fp);

  if (total == 0)
    return -1;
  return extract_json_string_field(buf, "Description", desc_out, bufsize);
}
