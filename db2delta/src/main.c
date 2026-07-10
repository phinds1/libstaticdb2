#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "conn.h"
#include "engine.h"

int main(int argc, char **argv) {
    db2delta_args_t args;
    memset(&args, 0, sizeof(args));

    /* Load database.env defaults first (lower priority than CLI) */
    args_load_env("database.env", &args);

    int r = args_parse(argc, argv, &args);

    if (r != 0 || args.mode == MODE_USAGE) {
        args_usage(argv[0]);
        return 2;
    }

    switch (args.mode) {
    case MODE_VERSION: {
        db2delta_conn_t conn;
        if (conn_open(&conn, &args) != 0) return 1;
        char ver[256] = {0};
        int r2 = conn_server_version(&conn, ver, sizeof(ver));
        conn_close(&conn);
        if (r2 != 0) { fprintf(stderr, "error: could not retrieve version\n"); return 1; }
        printf("%s\n", ver);
        return 0;
    }

    case MODE_QUERY: {
        db2delta_conn_t conn;
        if (conn_open(&conn, &args) != 0) return 1;
        int r2 = engine_query(&conn, args.query, args.script_mode);
        conn_close(&conn);
        return r2 != 0 ? 1 : 0;
    }

    case MODE_APPLY: {
        db2delta_conn_t conn;
        if (conn_open(&conn, &args) != 0) return 1;
        engine_result_t res;
        int r2 = engine_apply(&conn, &args, &res);
        conn_close(&conn);
        if (!args.script_mode)
            printf("\napplied=%d skipped=%d failed=%d\n",
                   res.applied, res.skipped, res.failed);
        /* Exit code: 1 if any failed; 0 otherwise */
        return (r2 != 0 || res.failed > 0) ? 1 : 0;
    }

    default:
        args_usage(argv[0]);
        return 2;
    }
}
