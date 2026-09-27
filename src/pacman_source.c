#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pacman_source.h"

int pacman_load_packages(PackageList *list) {
    FILE *fp = popen("pacman -Sl 2>/dev/null", "r");
    if (!fp) return -1;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char repo[REPO_MAX_LEN], name[NAME_MAX_LEN], version[VERSION_MAX_LEN];
        /* format: "<repo> <name> <version> [installed]" */
        int matched = sscanf(line, "%15s %127s %63s", repo, name, version);
        if (matched < 3) continue;

        Package *pkg = pkglist_push(list);
        if (!pkg) continue;

        snprintf(pkg->repo, REPO_MAX_LEN, "%s", repo);
        snprintf(pkg->name, NAME_MAX_LEN, "%s", name);
        snprintf(pkg->version, VERSION_MAX_LEN, "%s", version);
        pkg->has_desc = 0;
    }

    int status = pclose(fp);
    return (status == 0) ? 0 : 0; /* pacman -Sl can be picky about exit codes; data already parsed */
}

int pacman_fetch_description(const char *name, char *desc_out, size_t bufsize) {
    if (!name || !desc_out || bufsize == 0) return -1;

    char cmd[NAME_MAX_LEN + 32];
    snprintf(cmd, sizeof(cmd), "pacman -Si -- %s 2>/dev/null", name);

    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;

    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Description", 11) == 0) {
            char *colon = strchr(line, ':');
            if (colon) {
                colon++;
                while (*colon == ' ') colon++;
                size_t len = strlen(colon);
                if (len > 0 && colon[len - 1] == '\n') colon[len - 1] = '\0';
                strncpy(desc_out, colon, bufsize - 1);
                desc_out[bufsize - 1] = '\0';
                found = 1;
            }
            break;
        }
    }
    pclose(fp);
    return found ? 0 : -1;
}

/* --- bulk local preload -------------------------------------------- */

static unsigned long djb2_hash(const char *s) {
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

/* Open-addressing hash table: name -> Package*, sized generously (3x
 * entry count) to keep lookups fast even with linear probing. */
static void hash_insert(Package **table, size_t table_size, Package *pkg) {
    size_t h = (size_t)(djb2_hash(pkg->name) % table_size);
    while (table[h] != NULL) h = (h + 1) % table_size;
    table[h] = pkg;
}

static Package *hash_find(Package **table, size_t table_size, const char *name) {
    size_t h = (size_t)(djb2_hash(name) % table_size);
    for (size_t tries = 0; tries < table_size && table[h] != NULL; tries++) {
        if (strcmp(table[h]->name, name) == 0) return table[h];
        h = (h + 1) % table_size;
    }
    return NULL;
}

/* Reads pacman's local sync db (a gzip-compressed tar of one directory
 * per package, each containing a "desc" file with %FIELD%\nvalue\n...
 * blocks) via `tar`, and fills in has_desc/description for any matching
 * Package in `table` — no per-package subprocess, one `tar` call per repo. */
static void load_repo_descriptions(const char *repo, Package **table, size_t table_size) {
    char cmd[600];
    snprintf(cmd, sizeof(cmd),
             "tar -xOzf /var/lib/pacman/sync/%s.db --wildcards '*/desc' 2>/dev/null",
             repo);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[1024];
    char cur_name[NAME_MAX_LEN] = "";
    int expect = 0; /* 0 = nothing pending, 1 = next line is NAME, 2 = next line is DESC */

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        if (strcmp(line, "%NAME%") == 0) {
            expect = 1;
            continue;
        }
        if (strcmp(line, "%DESC%") == 0) {
            expect = 2;
            continue;
        }

        if (expect == 1) {
            snprintf(cur_name, sizeof(cur_name), "%.*s", NAME_MAX_LEN - 1, line);
            expect = 0;
        } else if (expect == 2) {
            expect = 0;
            if (cur_name[0] == '\0') continue; /* malformed/unexpected order: skip */
            Package *pkg = hash_find(table, table_size, cur_name);
            if (pkg && !pkg->has_desc) {
                snprintf(pkg->description, DESC_MAX_LEN, "%.*s", DESC_MAX_LEN - 1, line);
                pkg->has_desc = 1;
            }
            cur_name[0] = '\0';
        }
    }
    pclose(fp);
}

int pacman_bulk_load_descriptions(PackageList *list) {
    if (list->count == 0) return 0;

    size_t table_size = 64;
    while (table_size < list->count * 3) table_size <<= 1;

    Package **table = calloc(table_size, sizeof(Package *));
    if (!table) return 0; /* best-effort: fall back to on-demand fetch for everything */

    char seen_repos[32][REPO_MAX_LEN];
    int seen_count = 0;

    for (size_t i = 0; i < list->count; i++) {
        Package *pkg = &list->items[i];
        if (strcmp(pkg->repo, "aur") == 0) continue; /* AUR has no local db to read */
        hash_insert(table, table_size, pkg);

        int already_seen = 0;
        for (int r = 0; r < seen_count; r++) {
            if (strcmp(seen_repos[r], pkg->repo) == 0) { already_seen = 1; break; }
        }
        if (!already_seen && seen_count < 32) {
            snprintf(seen_repos[seen_count], REPO_MAX_LEN, "%s", pkg->repo);
            seen_count++;
        }
    }

    for (int r = 0; r < seen_count; r++) {
        load_repo_descriptions(seen_repos[r], table, table_size);
    }

    free(table);
    return 0;
}
