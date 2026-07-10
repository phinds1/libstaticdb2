#include "delta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── string helpers ──────────────────────────────────────────────────────── */

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

static char *strdup_n(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ── section init / free ─────────────────────────────────────────────────── */

static void section_free(delta_section_t *s) {
    if (!s) return;
    for (int i = 0; i < s->stmt_count; i++) free(s->stmts[i]);
    free(s->stmts);
    s->stmts = NULL;
    s->stmt_count = 0;
}

/* ── split body on terminator into s->stmts ─────────────────────────────── */

static int section_split(delta_section_t *s, const char *body, char terminator) {
    /* Copy body so we can modify it */
    char *buf = strdup_n(body);
    if (!buf) return -1;

    int cap = 8;
    s->stmts = malloc((size_t)cap * sizeof(char *));
    if (!s->stmts) { free(buf); return -1; }
    s->stmt_count = 0;

    char *p = buf;
    while (*p) {
        /* Find next terminator */
        char *end = strchr(p, terminator);
        if (!end) end = p + strlen(p); /* last chunk — no trailing terminator */

        /* Extract statement */
        size_t len = (size_t)(end - p);
        char *stmt = malloc(len + 1);
        if (!stmt) { free(buf); return -1; }
        memcpy(stmt, p, len);
        stmt[len] = '\0';

        const char *trimmed = trim(stmt);
        if (*trimmed) {
            /* Not empty — add to list (we store trimmed text in a new alloc) */
            char *stored = strdup_n(trimmed);
            free(stmt);
            if (!stored) { free(buf); return -1; }

            if (s->stmt_count >= cap) {
                cap *= 2;
                char **tmp = realloc(s->stmts, (size_t)cap * sizeof(char *));
                if (!tmp) { free(stored); free(buf); return -1; }
                s->stmts = tmp;
            }
            s->stmts[s->stmt_count++] = stored;
        } else {
            free(stmt);
        }

        if (*end == '\0') break;
        p = end + 1; /* skip terminator */
    }
    free(buf);
    return 0;
}

/* ── main parser ─────────────────────────────────────────────────────────── */

int delta_parse_string(const char *text, const char *filename,
                        delta_file_t *out) {
    memset(out, 0, sizeof(*out));
    out->terminator = ';';
    if (filename)
        strncpy(out->filename, filename, sizeof(out->filename) - 1);

    out->sections = calloc(DELTA_MAX_SECTIONS, sizeof(delta_section_t));
    if (!out->sections) return -1;

    /* Work through the text line by line */
    char *buf = strdup_n(text);
    if (!buf) { free(out->sections); return -1; }

    /* Accumulate current section body */
    char body[DELTA_STMT_MAX] = {0};
    size_t body_len = 0;
    int in_section = 0;
    int cur_section = -1;

    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *l = trim(line);

        /* Skip comment lines */
        if (l[0] == '#') {
            line = nl ? nl + 1 : NULL;
            continue;
        }

        if (l[0] == '[') {
            /* Flush previous section body */
            if (in_section && cur_section >= 0) {
                delta_section_t *sec = &out->sections[cur_section];
                body[body_len] = '\0';
                section_split(sec, body, out->terminator);
            }

            /* Parse section header: [changeset:name] or [validation:name] */
            char *colon = strchr(l + 1, ':');
            char *close = strchr(l, ']');
            if (colon && close && colon < close) {
                *colon = '\0'; *close = '\0';
                const char *kind_str = l + 1;
                const char *name_str = colon + 1;

                if (out->section_count >= DELTA_MAX_SECTIONS) {
                    free(buf); return -1;
                }
                cur_section = out->section_count++;
                delta_section_t *sec = &out->sections[cur_section];
                if (strcmp(kind_str, "changeset") == 0)
                    sec->kind = DELTA_CHANGESET;
                else
                    sec->kind = DELTA_VALIDATION;
                strncpy(sec->name, name_str, DELTA_NAME_MAX - 1);
                body_len = 0;
                body[0] = '\0';
                in_section = 1;
            }
        } else if (in_section) {
            /* Append line to section body */
            size_t ll = strlen(l);
            if (body_len + ll + 2 < DELTA_STMT_MAX) {
                if (body_len > 0) body[body_len++] = '\n';
                memcpy(body + body_len, l, ll);
                body_len += ll;
            }
        } else {
            /* Header key=value */
            char *eq = strchr(l, '=');
            if (eq) {
                *eq = '\0';
                const char *key = trim(l);
                const char *val = trim(eq + 1);
                if (strcmp(key, "terminator") == 0 && *val)
                    out->terminator = val[0];
                else if (strcmp(key, "continue-on-error") == 0)
                    out->continue_on_error = (strcmp(val, "true") == 0) ? 1 : 0;
                else if (strcmp(key, "author") == 0)
                    strncpy(out->author, val, DELTA_NAME_MAX - 1);
                else if (strcmp(key, "logging") == 0)
                    strncpy(out->logging, val, sizeof(out->logging) - 1);
                /* Unknown keys: ignored */
            }
        }

        line = nl ? nl + 1 : NULL;
    }

    /* Flush last section */
    if (in_section && cur_section >= 0) {
        body[body_len] = '\0';
        section_split(&out->sections[cur_section], body, out->terminator);
    }

    free(buf);
    return 0;
}

int delta_parse_file(const char *path, delta_file_t *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0 || sz > 1024 * 1024) { fclose(f); return -1; } /* 1 MB limit */

    char *text = malloc((size_t)sz + 1);
    if (!text) { fclose(f); return -1; }
    size_t nr = fread(text, 1, (size_t)sz, f);
    text[nr] = '\0';
    fclose(f);

    int r = delta_parse_string(text, path, out);
    free(text);
    return r;
}

void delta_free(delta_file_t *f) {
    if (!f) return;
    for (int i = 0; i < f->section_count; i++)
        section_free(&f->sections[i]);
    free(f->sections);
    f->sections = NULL;
    f->section_count = 0;
}
