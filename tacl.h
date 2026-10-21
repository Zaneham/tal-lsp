/* tacl.h -- TACL, the NonStop command language, in the same server.
 * Built from the TACL Reference Manual (429513-017, August 2013).
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#ifndef TACL_H
#define TACL_H

#include "tal.h"
#include <ctype.h>

/* ---- Limits ---- */

#define TC_MAXLINE   239
#define TC_MAXNEST   64
#define TC_MAXFRAME  64
#define TC_MAXLABEL  64
#define TC_MAXCONT   64

/* ---- Tokens ---- */

typedef enum {
    TC_WORD,
    TC_NUM,
    TC_STR,
    TC_BUILTIN,
    TC_MACARG,
    TC_LBRACK,
    TC_RBRACK,
    TC_LABEL,
    TC_COMMENT,
    TC_DIRECT,
    TC_CONT,
    TC_EOL,
    TC_OP,
    TC_LPAREN,
    TC_RPAREN,
    TC_COMMA,
    TC_SEMI,
    TC_SLASH,
    TC_ESC,
    TC_EOF,
    TC_COUNT
} tc_tk_t;

/* ---- Symbols ---- */

typedef enum {
    TC_S_TEXT = 32,
    TC_S_ALIAS,
    TC_S_MACRO,
    TC_S_ROUTINE,
    TC_S_STRUCT,
    TC_S_DIRECTORY,
    TC_S_DELTA,
    TC_S_ITEM,
    TC_S_PUSHED
} tc_sk_t;

/* ---- Tables ---- */

typedef enum { TC_K_FUNC, TC_K_VAR, TC_K_CMD, TC_K_PROG } tc_kind_t;

typedef struct {
    const char *name;
    uint8_t     kind;
    uint8_t     restricted;
    const char *desc;
} tc_ent_t;

extern const tc_ent_t tc_bltab[];
extern const uint32_t tc_nbl;
extern const tc_ent_t tc_cmtab[];
extern const uint32_t tc_ncm;
extern const char *const tc_argtab[];
extern const uint32_t tc_narg;
extern const char *const tc_sttab[];
extern const uint32_t tc_nst;

const tc_ent_t *tc_bfind(const char *s, uint32_t len);
const tc_ent_t *tc_cfind(const char *s, uint32_t len);

/* ---- Completion context ---- */

typedef enum {
    TC_C_WORD,
    TC_C_BUILTIN,
    TC_C_LABEL,
    TC_C_DIRECT,
    TC_C_ARGTYPE,
    TC_C_STTYPE,
    TC_C_SECTYPE
} tc_ctx_t;

/* ---- API ---- */

int  tc_lex(tal_lex_t *L, const char *src, uint32_t len);
void tc_pass(tal_symtab_t *S, tal_lex_t *L, tal_diag_t *diags, int *n_diags);
tc_ctx_t tc_ctx(const char *src, uint32_t len, uint32_t line, uint32_t col);
const char *tc_kname(uint8_t kind);

#endif /* TACL_H */
