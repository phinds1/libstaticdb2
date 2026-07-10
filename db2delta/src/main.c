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

    if (r != 0) {
        args_usage(argv[0]);
        return 2;
    }
    if (args.mode == MODE_USAGE) {
        args_usage(argv[0]);
        return 0;
    }

    switch (args.mode) {
    case MODE_VERSION:
        printf("db2delta %s\n", DB2DELTA_VERSION);
        return 0;

    case MODE_EXEC: {
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
        return (r2 != 0 || res.failed > 0) ? 1 : 0;
    }

    default:
        args_usage(argv[0]);
        return 2;
    }
}
