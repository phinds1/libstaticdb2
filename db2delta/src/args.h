#ifndef ARGS_H
#define ARGS_H

#define DB2DELTA_MAX_FILES 256

typedef enum {
    MODE_USAGE   = 0,
    MODE_VERSION,   /* version subcommand — print tool version, no DB needed */
    MODE_EXEC,      /* exec subcommand — execute arbitrary SQL */
    MODE_APPLY      /* apply subcommand — apply delta files */
} db2delta_mode_t;

typedef struct {
    /* Raw DSN string: host:port/database */
    char dsn[512];
    /* Parsed DSN components */
    char host[256];
    int  port;          /* 0 = not specified → default 50000 in conn layer */
    char database[128];
    /* Credentials */
    char user[128];
    char password[256];
    /* Mode */
    db2delta_mode_t mode;
    /* exec: arbitrary SQL query */
    char query[4096];
    /* apply: list of delta files / directories */
    char *sql_files[DB2DELTA_MAX_FILES];
    int   sql_file_count;
    /* Flags */
    int script_mode;        /* -x */
    int continue_on_error;  /* -c / --continue-on-error */
} db2delta_args_t;

/* Parse argc/argv into *out.  Returns 0 on success, non-zero on error.
 * out->mode will be MODE_USAGE if required args are missing. */
int args_parse(int argc, char **argv, db2delta_args_t *out);

/* Parse "host:port/database" DSN string into the host/port/database fields
 * of *out.  Port is left 0 if not present in the DSN. */
int args_parse_dsn(const char *dsn, db2delta_args_t *out);

/* Load defaults from a database.env-style file into *out.
 * Existing non-zero/non-empty values are NOT overwritten (env file is
 * lower-priority than explicit CLI flags — call this BEFORE args_parse). */
int args_load_env(const char *path, db2delta_args_t *out);

/* Print usage to stderr. */
void args_usage(const char *prog);

#endif /* ARGS_H */
