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
    out->database[sizeof(out->database) - 1] = '\0';

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
    out->host[sizeof(out->host) - 1] = '\0';
    if (!*out->host) return -1;

    strncpy(out->dsn, dsn, sizeof(out->dsn) - 1);
    out->dsn[sizeof(out->dsn) - 1] = '\0';
    return 0;
}

/* ── Usage ──────────────────────────────────────────────────────────────── */

void args_usage(const char *prog) {
    fprintf(stderr,
        "🚧 db2delta — zero-dependency DB2 schema migration tool\n"
        "\n"
        "Usage: %s <subcommand> [options]\n"
        "\n"
        "Subcommands:\n"
        "  version                        Print db2delta version and exit\n"
        "  exec   -d <dsn> -u <user> <SQL>         Execute SQL and print results\n"
        "  apply  -d <dsn> -u <user> <file|dir>...  Apply delta files\n"
        "\n"
        "Options (exec, apply):\n"
        "  -d, --database <host[:port]/db>  DB2 connection string (port defaults to 50000)\n"
        "  -u, --user <user>              DB2 username\n"
        "  -p, --password <password>      DB2 password (or set in database.env)\n"
        "  -c, --continue-on-error        Do not abort on changeset error\n"
        "  -x, --script                   Script mode: minimal output, strict exit codes\n"
        "\n"
        "If ./database.env exists its key=value pairs are used as defaults.\n"
        "CLI flags always override database.env.\n"
        "\n"
        "Examples:\n"
        "  db2delta version\n"
        "  db2delta exec -d myhost/MYDB -u db2inst1 \"SELECT 1 FROM SYSIBM.SYSDUMMY1\"\n"
        "  db2delta apply -d myhost/MYDB -u db2inst1 changes/001_add_users.delta\n"
        "  db2delta apply -d myhost/MYDB -u db2inst1 -c migrations/\n",
        prog ? prog : "db2delta");
}

/* ── CLI parser ─────────────────────────────────────────────────────────── */

int args_parse(int argc, char **argv, db2delta_args_t *out) {
    int have_dsn  = (out->host[0] != '\0');
    int have_user = (out->user[0] != '\0');
    int subcmd_found = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if ((strcmp(a, "-d") == 0 || strcmp(a, "--database") == 0) && i + 1 < argc) {
            if (args_parse_dsn(argv[++i], out) != 0) {
                fprintf(stderr, "error: invalid DSN '%s'\n", argv[i]);
                out->mode = MODE_USAGE;
                return -1;
            }
            have_dsn = 1;
        } else if ((strcmp(a, "-u") == 0 || strcmp(a, "--user") == 0) && i + 1 < argc) {
            strncpy(out->user, argv[++i], sizeof(out->user) - 1);
            out->user[sizeof(out->user) - 1] = '\0';
            have_user = 1;
        } else if ((strcmp(a, "-p") == 0 || strcmp(a, "--password") == 0) && i + 1 < argc) {
            strncpy(out->password, argv[++i], sizeof(out->password) - 1);
            out->password[sizeof(out->password) - 1] = '\0';
        } else if (strcmp(a, "-x") == 0 || strcmp(a, "--script") == 0) {
            out->script_mode = 1;
        } else if (strcmp(a, "-c") == 0 || strcmp(a, "--continue-on-error") == 0) {
            out->continue_on_error = 1;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", a);
            out->mode = MODE_USAGE;
            return -1;
        } else if (!subcmd_found) {
            subcmd_found = 1;
            if      (strcmp(a, "version") == 0) out->mode = MODE_VERSION;
            else if (strcmp(a, "exec")    == 0) out->mode = MODE_EXEC;
            else if (strcmp(a, "apply")   == 0) out->mode = MODE_APPLY;
            else if (strcmp(a, "help")    == 0) { /* leave MODE_USAGE */ }
            else {
                fprintf(stderr, "error: unknown subcommand '%s'\n"
                                "       run 'db2delta help' for usage\n", a);
                out->mode = MODE_USAGE;
                return -1;
            }
        } else {
            if (out->mode == MODE_EXEC) {
                strncpy(out->query, a, sizeof(out->query) - 1);
                out->query[sizeof(out->query) - 1] = '\0';
            } else if (out->mode == MODE_APPLY) {
                if (out->sql_file_count < DB2DELTA_MAX_FILES)
                    out->sql_files[out->sql_file_count++] = argv[i];
            }
        }
    }

    if (out->mode == MODE_USAGE) return 0;

    if (out->mode == MODE_VERSION) return 0;

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

    if (out->mode == MODE_EXEC && !out->query[0]) {
        fprintf(stderr, "error: exec requires a SQL string argument\n");
        out->mode = MODE_USAGE;
        return -1;
    }

    if (out->mode == MODE_APPLY && out->sql_file_count == 0) {
        fprintf(stderr, "error: apply requires at least one file or directory\n");
        out->mode = MODE_USAGE;
        return -1;
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
            if (!out->user[0]) {
                strncpy(out->user, val, sizeof(out->user) - 1);
                out->user[sizeof(out->user) - 1] = '\0';
            }
        } else if (strcmp(key, "PASSWORD") == 0) {
            if (!out->password[0]) {
                strncpy(out->password, val, sizeof(out->password) - 1);
                out->password[sizeof(out->password) - 1] = '\0';
            }
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
