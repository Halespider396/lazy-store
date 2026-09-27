#ifndef PACMAN_SOURCE_H
#define PACMAN_SOURCE_H

#include "package.h"

/* Appends every package from `pacman -Sl` (official repo sync DBs, no
 * network needed) into `list`. Returns 0 on success, -1 if pacman
 * could not be run. */
int pacman_load_packages(PackageList *list);

/* Fetches the Description field for a single package via `pacman -Si`
 * and writes it (truncated to bufsize) into desc_out. Returns 0 on
 * success, -1 if not found. */
int pacman_fetch_description(const char *name, char *desc_out, size_t bufsize);

/* Preloads descriptions for every official-repo package in `list` in
 * one pass per repo, by reading pacman's local sync database files
 * directly (the same files `pacman -Sl` already relies on) instead of
 * shelling out to `pacman -Si` once per package. This is what makes
 * official-repo descriptions show up instantly with zero per-keystroke
 * fetch — no network, no per-package subprocess. Packages it can't
 * match (missing db file, unusual format, no %DESC% field) are simply
 * left unset and fall back to pacman_fetch_description on demand.
 * Always returns 0 (best-effort; nothing to fail hard on). */
int pacman_bulk_load_descriptions(PackageList *list);

#endif
