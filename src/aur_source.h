#ifndef AUR_SOURCE_H
#define AUR_SOURCE_H

#include "package.h"

/* Loads the full AUR package name list into `list` (repo field set to
 * "aur"). Uses a local cache at ~/.cache/lazy-search/aur_packages.txt,
 * refreshed from https://aur.archlinux.org/packages.gz via `curl | gunzip`
 * whenever the cache is missing or older than max_age_seconds.
 * Returns 0 on success (even if using a stale cache because refresh
 * failed), -1 if no data could be obtained at all. */
int aur_load_packages(PackageList *list, long max_age_seconds);

/* Fetches the Description field for a single AUR package via the AUR
 * RPC info endpoint (requires network). Returns 0 on success, -1 if
 * not found or the request failed. */
int aur_fetch_description(const char *name, char *desc_out, size_t bufsize);

#endif
