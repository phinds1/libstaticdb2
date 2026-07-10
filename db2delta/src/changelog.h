#ifndef CHANGELOG_H
#define CHANGELOG_H

#include "conn.h"

/* Ensure the DBDELTA_CHANGELOG table exists in the connected database.
 * Uses SYSCAT.TABLES existence check (avoids CREATE TABLE IF NOT EXISTS
 * portability issues across DB2 versions).
 * Safe to call multiple times (idempotent). */
int changelog_ensure(db2delta_conn_t *c);

/* Check whether a named changeset has been recorded.
 * *out_present is set to 1 if present, 0 if not.
 * Returns 0 on success, non-zero on error. */
int changelog_has(db2delta_conn_t *c, const char *changeset, int *out_present);

/* Record an applied changeset.
 * author may be NULL or empty. */
int changelog_record(db2delta_conn_t *c,
                     const char *changeset,
                     const char *filename,
                     const char *sha256_hex,
                     const char *author);

/* SQL builders — exposed for unit testing (DB-free). */
void changelog_ddl_sql(char *buf, size_t buf_sz);
void changelog_exists_sql(char *buf, size_t buf_sz);
void changelog_check_sql(char *buf, size_t buf_sz);
void changelog_insert_sql(char *buf, size_t buf_sz);

#endif /* CHANGELOG_H */
