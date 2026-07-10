#ifndef SCAN_H
#define SCAN_H

/* Sort an array of path strings byte-wise (ASCII-betical, by basename).
 * Sorting is in-place; the pointers themselves are reordered. */
void scan_sort(char **paths, int count);

/* Scan a directory for *.delta files, returning a sorted list.
 * *out_paths is malloc'd; caller must call scan_free().
 * Returns 0 on success, non-zero on error. */
int scan_dir(const char *dir, char ***out_paths, int *out_count);

/* Resolve a list of paths (files or dirs) into an ordered list of .delta files.
 * Each path is included directly if it is a file, or scanned if a directory.
 * The final list is ASCII-betically sorted across the combined result.
 * Returns 0 on success. */
int scan_resolve(char **input_paths, int input_count,
                 char ***out_paths, int *out_count);

/* Free the path list returned by scan_dir / scan_resolve. */
void scan_free(char **paths, int count);

#endif /* SCAN_H */
