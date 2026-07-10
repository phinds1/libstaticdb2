#include "args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── DSN parser ─────────────────────────────────────────────────────────── */

int args_parse_dsn(const char *dsn, db2delta_args_t *out) {
    if (!dsn || !*dsn) return -1;

    /* Format: host:port/database  or  host/database */
    char buf[512];
    strncpy(buf, dsn, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Find database part after '/' */
    char *slash = strchr(buf, '/');
    if (!slash || !*(slash + 1)) return -1;
    *slash = '\0';
    strncpy(out->database, slash + 1, sizeof(out->database) - 1);

    /* Check for port */
    char *colon = strchr(buf, ':');
    if (colon) {
        *colon = '\0';
        out->port = atoi(colon + 1);
        if (out->port < 1 || out->port > 65535) return -1;
    } else {
        out->port = 0; /* caller defaults to 50000 */
    }

    strncpy(out->host, buf, sizeof(out->host) - 1);
    if (!*out->host) return -1;

    strncpy(out->dsn, dsn, sizeof(out->dsn) - 1);
    return 0;
}

/* ── Usage ──────────────────────────────────────────────────────────────── */

void args_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -d host[:port]/database -u user [-p password] <mode>\n"
        "\n"
        "Modes:\n"
        "  -V                    Print server version and exit\n"
        "  -q <SQL>              Execute arbitrary SQL and print results\n"
        "  -s <file|dir>...      Apply delta files (default mode)\n"
        "\n"
        "Options:\n"
        "  -d <dsn>              host[:port]/database  (port defaults to 50000)\n"
        "  -u <user>             DB2 username\n"
        "  -p <password>         DB2 password (omit to prompt or use database.env)\n"
        "  -c, --continue-on-error  Do not stop on changeset error\n"
        "  -x                    Scripting mode: minimal output, strict exit codes\n"
        "\n"
        "If ./database.env exists its key=value pairs are used as defaults.\n"
        "CLI flags always override database.env.\n",
        prog ? prog : "db2delta");
}

/* ── CLI parser ─────────────────────────────────────────────────────────── */

int args_parse(int argc, char **argv, db2delta_args_t *out) {
    /* Determine mode from what was already set (env file may have pre-loaded) */
    int have_dsn  = (out->host[0] != '\0');
    int have_user = (out->user[0] != '\0');
    int have_mode = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "-d") == 0 && i + 1 < argc) {
            if (args_parse_dsn(argv[++i], out) != 0) {
                fprintf(stderr, "error: invalid DSN '%s'\n", argv[i]);
                out->mode = MODE_USAGE;
                return -1;
            }
            have_dsn = 1;
        } else if (strcmp(a, "-u") == 0 && i + 1 < argc) {
            strncpy(out->user, argv[++i], sizeof(out->user) - 1);
            have_user = 1;
        } else if (strcmp(a, "-p") == 0 && i + 1 < argc) {
            strncpy(out->password, argv[++i], sizeof(out->password) - 1);
        } else if (strcmp(a, "-V") == 0) {
            out->mode = MODE_VERSION;
            have_mode = 1;
        } else if (strcmp(a, "-q") == 0 && i + 1 < argc) {
            strncpy(out->query, argv[++i], sizeof(out->query) - 1);
            out->mode = MODE_QUERY;
            have_mode = 1;
        } else if (strcmp(a, "-s") == 0) {
            /* Consume all remaining args as file/dir paths */
            out->mode = MODE_APPLY;
            have_mode = 1;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                if (out->sql_file_count < DB2DELTA_MAX_FILES)
                    out->sql_files[out->sql_file_count++] = argv[++i];
                else
                    i++;
            }
        } else if (strcmp(a, "-x") == 0) {
            out->script_mode = 1;
        } else if (strcmp(a, "-c") == 0 ||
                   strcmp(a, "--continue-on-error") == 0) {
            out->continue_on_error = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            out->mode = MODE_USAGE;
            return -1;
        }
        (void)have_mode;
    }

    /* Validate required args for non-usage modes */
    if (out->mode == MODE_USAGE) {
        /* Still usage/unset — infer MODE_APPLY if files given but no explicit mode */
        if (out->sql_file_count > 0)
            out->mode = MODE_APPLY;
        else
            return 0; /* bare invocation — will print usage in main */
    }

    if (out->mode != MODE_USAGE) {
        if (!have_dsn) {
            fprintf(stderr, "error: -d <dsn> is required\n");
            out->mode = MODE_USAGE;
            return -1;
        }
        if (!have_user) {
            fprintf(stderr, "error: -u <user> is required\n");
            out->mode = MODE_USAGE;
            return -1;
        }
    }

    return 0;
}

/* ── database.env loader ─────────────────────────────────────────────────── */

int args_load_env(const char *path, db2delta_args_t *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1; /* file absent is not an error to caller */

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        /* Skip comments and blank lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;

        /* Only set if not already populated (env file = lower priority) */
        if (strcmp(key, "DSN") == 0) {
            if (!out->host[0]) args_parse_dsn(val, out);
        } else if (strcmp(key, "USER") == 0) {
            if (!out->user[0])
                strncpy(out->user, val, sizeof(out->user) - 1);
        } else if (strcmp(key, "PASSWORD") == 0) {
            if (!out->password[0])
                strncpy(out->password, val, sizeof(out->password) - 1);
        } else if (strcmp(key, "CONTINUE_ON_ERROR") == 0) {
            if (!out->continue_on_error)
                out->continue_on_error = (strcmp(val, "true") == 0) ? 1 : 0;
        } else if (strcmp(key, "SCRIPT") == 0) {
            if (!out->script_mode)
                out->script_mode = (strcmp(val, "true") == 0) ? 1 : 0;
        }
        /* Unknown keys are ignored */
    }

    fclose(f);
    return 0;
}
