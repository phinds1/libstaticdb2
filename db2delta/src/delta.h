#ifndef DELTA_H
#define DELTA_H

#define DELTA_MAX_SECTIONS  256
#define DELTA_MAX_STMTS     512
#define DELTA_NAME_MAX      256
#define DELTA_STMT_MAX      65536

typedef enum {
    DELTA_CHANGESET,
    DELTA_VALIDATION
} delta_kind_t;

typedef struct {
    delta_kind_t  kind;
    char          name[DELTA_NAME_MAX];
    char        **stmts;      /* NULL-terminated array of SQL statement strings */
    int           stmt_count;
} delta_section_t;

typedef struct {
    char  terminator;           /* ';' default, '@' for procedures */
    int   continue_on_error;    /* 0 = fail-fast, 1 = continue */
    char  author[DELTA_NAME_MAX];
    char  logging[32];          /* debug|warn|error */
    char  filename[512];        /* source path */
    delta_section_t *sections;
    int               section_count;
} delta_file_t;

/* Parse a .delta file by path. Returns 0 on success. */
int delta_parse_file(const char *path, delta_file_t *out);

/* Parse a .delta from an in-memory string (filename is for error messages). */
int delta_parse_string(const char *text, const char *filename, delta_file_t *out);

/* Free all memory owned by *f. */
void delta_free(delta_file_t *f);

#endif /* DELTA_H */
