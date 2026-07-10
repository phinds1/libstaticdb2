#ifndef ENGINE_H
#define ENGINE_H

#include "conn.h"
#include "args.h"
#include "delta.h"
#include "../../include/staticdb2/sdb2.h"

typedef struct {
    int applied;   /* changesets successfully applied */
    int skipped;   /* changesets skipped (already in changelog) */
    int failed;    /* changesets that errored */
} engine_result_t;

/* Decision enum for unit testing the engine logic. */
typedef enum {
    ENG_APPLY_RECORD,   /* not present + executed ok → record it */
    ENG_SKIP_PRESENT,   /* already in changelog → skip */
    ENG_STOP_FAIL,      /* error + fail-fast → abort run */
    ENG_CONTINUE_FAIL,  /* error + continue-on-error → log, keep going */
} engine_decision_t;

/* Pure decision function — unit testable without a DB. */
engine_decision_t engine_decide(int already_present, int stmt_failed,
                                 int continue_on_error);

/* Apply all delta files specified in args.  Populates *result. */
int engine_apply(db2delta_conn_t *c, const db2delta_args_t *args,
                 engine_result_t *result);

/* Execute an arbitrary SQL query and print result rows to stdout.
 * Used for -q mode. */
int engine_query(db2delta_conn_t *c, const char *sql, int script_mode);

/* Evaluate a validation section.
 * *out_pass is set to 1 if the validation passes, 0 if it fails.
 * Returns 0 on success (query ran), non-zero on execution error. */
int engine_validate(db2delta_conn_t *c, const delta_section_t *sec,
                    int *out_pass);

/* Pure pass/fail interpretation — unit testable without a DB.
 * had_row: 1 if at least one row was returned, 0 if no rows.
 * first_col: pointer to the first column value (may be NULL if had_row=0). */
int validation_passes(const sdb2_value_t *first_col, int had_row);

#endif /* ENGINE_H */
