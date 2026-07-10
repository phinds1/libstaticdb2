#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "args.h"
#include "changelog.h"
#include "conn.h"
#include "engine.h"

/* Prompt for password on /dev/tty with echo disabled.
 * Returns 0 on success, -1 if the tty cannot be opened or read. */
static int prompt_password(char *buf, size_t bufsz) {
    FILE *tty = fopen("/dev/tty", "r+");
    if (!tty) return -1;

    fputs("Password: ", tty);
    fflush(tty);

    struct termios old, noecho;
    tcgetattr(fileno(tty), &old);
    noecho = old;
    noecho.c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK | ECHONL);
    tcsetattr(fileno(tty), TCSAFLUSH, &noecho);

    int ok = (fgets(buf, (int)bufsz, tty) != NULL);

    tcsetattr(fileno(tty), TCSAFLUSH, &old);
    fputc('\n', tty);   /* newline after hidden input */
    fclose(tty);

    if (!ok) return -1;

    /* strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    return 0;
}

int main(int argc, char **argv) {
    db2delta_args_t args;
    memset(&args, 0, sizeof(args));

    /* Load database.env defaults first (lower priority than CLI) */
    args_load_env("database.env", &args);

    int r = args_parse(argc, argv, &args);

    if (r != 0) {
        args_usage(argv[0]);
        return 2;
    }
    if (args.mode == MODE_USAGE) {
        args_usage(argv[0]);
        return 0;
    }

    switch (args.mode) {
    case MODE_SELF_VERSION:
        printf("db2delta %s\n", DB2DELTA_VERSION);
        return 0;

    case MODE_DB_VERSION:
    case MODE_EXEC:
    case MODE_APPLY: {
        /* Prompt for password interactively if not supplied */
        if (!args.password[0]) {
            if (args.script_mode) {
                fprintf(stderr, "error: password required (use -p or set PASSWORD in database.env)\n");
                return 2;
            }
            if (prompt_password(args.password, sizeof(args.password)) != 0) {
                fprintf(stderr, "error: could not read password\n");
                return 2;
            }
        }

        db2delta_conn_t conn;
        if (conn_open(&conn, &args) != 0) return 1;

        int r2 = 0;
        if (args.mode == MODE_DB_VERSION) {
            changelog_entry_t entry;
            int rc = changelog_latest(&conn, &entry);
            if (rc < 0) {
                fprintf(stderr, "error: could not query changelog\n");
                r2 = 1;
            } else if (rc == 1) {
                printf("no changesets applied yet\n");
            } else if (args.script_mode) {
                printf("%s\n", entry.changeset);
            } else {
                printf("changeset: %s\n", entry.changeset);
                printf("file:      %s\n", entry.filename);
                printf("applied:   %s\n", entry.applied_at);
            }
        } else if (args.mode == MODE_EXEC) {
            r2 = engine_query(&conn, args.query, args.script_mode);
        } else {
            engine_result_t res;
            r2 = engine_apply(&conn, &args, &res);
            if (!args.script_mode)
                printf("\napplied=%d skipped=%d failed=%d\n",
                       res.applied, res.skipped, res.failed);
            if (res.failed > 0) r2 = 1;
        }
        conn_close(&conn);
        return r2 != 0 ? 1 : 0;
    }

    default:
        args_usage(argv[0]);
        return 2;
    }
}
