#ifndef CHANGELOG_H
#define CHANGELOG_H

#include "conn.h"

typedef struct {
    char changeset[256];
    char filename[256];
    char applied_at[32];
} changelog_entry_t;

/* Ensure the DBDELTA_CHANGELOG table exists in the connected database. */
int changelog_ensure(db2delta_conn_t *c);

/* Check whether a named changeset has been recorded. */
int changelog_has(db2delta_conn_t *c, const char *changeset, int *out_present);

/* Record an applied changeset. author may be NULL or empty. */
int changelog_record(db2delta_conn_t *c,
                     const char *changeset,
                     const char *filename,
                     const char *sha256_hex,
                     const char *author);

/* Fetch the most recently applied changeset.
 * Returns  0 on success (out is populated),
 *          1 if the changelog is empty,
 *         -1 on error. */
int changelog_latest(db2delta_conn_t *c, changelog_entry_t *out);

/* SQL builders — exposed for unit testing (DB-free). */
void changelog_ddl_sql(char *buf, size_t buf_sz);
void changelog_exists_sql(char *buf, size_t buf_sz);
void changelog_check_sql(char *buf, size_t buf_sz);
void changelog_insert_sql(char *buf, size_t buf_sz);
void changelog_latest_sql(char *buf, size_t buf_sz);

#endif /* CHANGELOG_H */
