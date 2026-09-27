#ifndef PACKAGE_H
#define PACKAGE_H

#include <stddef.h>

#define NAME_MAX_LEN 128
#define REPO_MAX_LEN 16
#define VERSION_MAX_LEN 64
#define DESC_MAX_LEN 300

typedef struct {
  char name[NAME_MAX_LEN];
  char repo[REPO_MAX_LEN]; /* "core", "extra", "aur", etc */
  char version[VERSION_MAX_LEN];
  char description[DESC_MAX_LEN];
  int has_desc; /* 1 once description has been fetched */
  int fetching; /* 1 while a background fetch is in flight */
} Package;

typedef struct {
  Package *items;
  size_t count;
  size_t capacity;
} PackageList;

/* Grow-as-needed list helpers, shared by both source modules */
void pkglist_init(PackageList *list);
Package *pkglist_push(PackageList *list);
void pkglist_free(PackageList *list);

#endif
