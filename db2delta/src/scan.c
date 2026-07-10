#define _POSIX_C_SOURCE 200809L
#include "scan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* ── sort helpers ────────────────────────────────────────────────────────── */

/* Return pointer to basename component of path (after last '/'). */
static const char *basename_of(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static int cmp_by_basename(const void *a, const void *b) {
    return strcmp(basename_of(*(const char *const *)a),
                  basename_of(*(const char *const *)b));
}

void scan_sort(char **paths, int count) {
    if (count > 1)
        qsort(paths, (size_t)count, sizeof(char *), cmp_by_basename);
}

/* ── scan_free ───────────────────────────────────────────────────────────── */

void scan_free(char **paths, int count) {
    if (!paths) return;
    for (int i = 0; i < count; i++) free(paths[i]);
    free(paths);
}

/* ── scan_dir ────────────────────────────────────────────────────────────── */

int scan_dir(const char *dir, char ***out_paths, int *out_count) {
    DIR *d = opendir(dir);
    if (!d) return -1;

    int cap = 32;
    char **paths = malloc((size_t)cap * sizeof(char *));
    if (!paths) { closedir(d); return -1; }
    int count = 0;

    const struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        /* Skip dots and dotfiles */
        if (ent->d_name[0] == '.') continue;

        /* Only regular files (or unknown type — stat to check) */
        size_t nlen = strlen(ent->d_name);
        if (nlen < 7) continue; /* shorter than ".delta" */

        /* Check .delta extension */
        if (strcmp(ent->d_name + nlen - 6, ".delta") != 0) continue;

        /* Build full path */
        size_t dirlen = strlen(dir);
        char *full = malloc(dirlen + 1 + nlen + 1);
        if (!full) { scan_free(paths, count); closedir(d); return -1; }
        memcpy(full, dir, dirlen);
        full[dirlen] = '/';
        memcpy(full + dirlen + 1, ent->d_name, nlen + 1);

        /* Verify it is a regular file */
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) {
            free(full);
            continue;
        }

        if (count >= cap) {
            cap *= 2;
            char **tmp = realloc(paths, (size_t)cap * sizeof(char *));
            if (!tmp) { free(full); scan_free(paths, count); closedir(d); return -1; }
            paths = tmp;
        }
        paths[count++] = full;
    }
    closedir(d);

    scan_sort(paths, count);
    *out_paths = paths;
    *out_count = count;
    return 0;
}

/* ── scan_resolve ────────────────────────────────────────────────────────── */

int scan_resolve(char **input_paths, int input_count,
                 char ***out_paths, int *out_count) {
    int cap = 64;
    char **result = malloc((size_t)cap * sizeof(char *));
    if (!result) return -1;
    int total = 0;

    for (int i = 0; i < input_count; i++) {
        struct stat st;
        if (stat(input_paths[i], &st) != 0) {
            fprintf(stderr, "warning: cannot stat '%s', skipping\n",
                    input_paths[i]);
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            char **sub = NULL; int sub_count = 0;
            if (scan_dir(input_paths[i], &sub, &sub_count) != 0) {
                fprintf(stderr, "warning: cannot scan dir '%s'\n",
                        input_paths[i]);
                continue;
            }
            for (int j = 0; j < sub_count; j++) {
                if (total >= cap) {
                    cap *= 2;
                    char **tmp = realloc(result, (size_t)cap * sizeof(char *));
                    if (!tmp) { free(sub[j]); scan_free(sub + j + 1, sub_count - j - 1);
                                free(sub); scan_free(result, total); return -1; }
                    result = tmp;
                }
                result[total++] = sub[j];
            }
            free(sub); /* pointers transferred; free the array but not strings */
        } else if (S_ISREG(st.st_mode)) {
            char *dup = strdup(input_paths[i]);
            if (!dup) { scan_free(result, total); return -1; }
            if (total >= cap) {
                cap *= 2;
                char **tmp = realloc(result, (size_t)cap * sizeof(char *));
                if (!tmp) { free(dup); scan_free(result, total); return -1; }
                result = tmp;
            }
            result[total++] = dup;
        }
    }

    scan_sort(result, total);
    *out_paths = result;
    *out_count = total;
    return 0;
}
