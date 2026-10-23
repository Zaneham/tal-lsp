/* tal.h -- TAL and pTAL token, AST and symbol table types. Built from the
 * TAL Reference Manual (526371-001) and the pTAL Reference Manual (523746-009).
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#ifndef TAL_H
#define TAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Limits ---- */

#define TAL_MAX_TOKS    (1 << 16)
#define TAL_MAX_NODES   (1 << 16)
#define TAL_MAX_SYMS    4096
#define TAL_MAX_SCOPES  64
#define TAL_MAX_STRS    (128 * 1024)
#define TAL_MAX_LINE    256
#define TAL_MAX_IDENT   64
#define TAL_MAX_ERRORS  256
#define TAL_MAX_DIAGS   256
#define TAL_MAX_FIELDS  256

/* ---- Token Types ---- */

typedef enum {

    TK_INT_LIT,
    TK_REAL_LIT,
    TK_STRING_LIT,
    TK_SNUM_LIT,

    TK_IDENT,

    TK_PLUS,
    TK_MINUS,
    TK_STAR,
    TK_SLASH,
    TK_LSHIFT,
    TK_RSHIFT,
    TK_PLUS_UNSIGNED,
    TK_MINUS_UNSIGNED,
    TK_STAR_UNSIGNED,
    TK_SLASH_UNSIGNED,
    TK_MOD_UNSIGNED,
    TK_LSHIFT_UNSIGNED,
    TK_RSHIFT_UNSIGNED,
    TK_MOVE_LR,
    TK_MOVE_RL,
    TK_BASE,
    TK_ARROW,

    TK_LAND,
    TK_LOR,
    TK_XOR,

    TK_EQ,
    TK_NEQ,
    TK_LT,
    TK_GT,
    TK_LE,
    TK_GE,

    TK_AND,
    TK_OR,
    TK_NOT,

    TK_LBRACKET,
    TK_RBRACKET,

    TK_ASSIGN,
    TK_LPAREN,
    TK_RPAREN,
    TK_COMMA,
    TK_SEMICOLON,
    TK_COLON,
    TK_DOT,
    TK_AMPERSAND,
    TK_AT,
    TK_CARET,
    TK_EQUALS,

    TK_STRING,
    TK_INT,
    TK_INT32,
    TK_FIXED,
    TK_REAL,
    TK_REAL64,
    TK_UNSIGNED,

    TK_LITERAL,
    TK_DEFINE,
    TK_STRUCT,
    TK_BEGIN,
    TK_END,
    TK_FILLER,
    TK_BIT_FILLER,

    TK_GLOBAL,
    TK_LOCAL,
    TK_SUBLOCAL,

    TK_DOT_EXT,

    TK_PROC,
    TK_SUBPROC,
    TK_FORWARD,
    TK_EXTERNAL,
    TK_MAIN,
    TK_INTERRUPT,
    TK_RESIDENT,
    TK_CALLABLE,
    TK_PRIV,
    TK_VARIABLE,
    TK_EXTENSIBLE,
    TK_LANGUAGE,
    TK_ENTRY,
    TK_LABEL,
    TK_PTAL,

    TK_IF,
    TK_THEN,
    TK_ELSE,
    TK_CASE,
    TK_OF,
    TK_OTHERWISE,
    TK_WHILE,
    TK_DO,
    TK_FOR,
    TK_TO,
    TK_DOWNTO,
    TK_BY,
    TK_UNTIL,
    TK_GOTO,
    TK_CALL,
    TK_RETURN,
    TK_ASSERT,
    TK_SCAN,
    TK_MOVE,
    TK_STORE,
    TK_STACK,
    TK_CODE,
    TK_USE,
    TK_DROP,
    TK_RSCAN,

    TK_NAME,
    TK_BLOCK,

    TK_EQUIV,

    TK_DIRECTIVE,
    TK_SOURCE,
    TK_HASH,

    TK_DOLLAR,

    TK_DOLLAR_CARRY,
    TK_DOLLAR_OVERFLOW,

    TK_COMMENT,
    TK_NEWLINE,
    TK_EOF,
    TK_COUNT
} tal_tk_t;

/* ---- Token ---- */

typedef struct {
    uint16_t kind;
    uint16_t flags;
    uint32_t line;
    uint32_t col;
    uint32_t off;
    uint32_t len;
    int64_t  ival;
} tal_tok_t;

/* ---- Error / Diagnostic ---- */

typedef struct {
    uint32_t line;
    uint32_t col;
    uint32_t len;
    uint8_t  severity;
    char     msg[128];
} tal_diag_t;

/* ---- Lexer Context ---- */

typedef struct {
    const char *src;
    uint32_t    slen;
    uint32_t    pos;
    uint32_t    line;
    uint32_t    col;

    tal_tok_t   toks[TAL_MAX_TOKS];
    uint32_t    n_toks;

    char        strs[TAL_MAX_STRS];
    uint32_t    str_len;

    tal_diag_t  diags[TAL_MAX_DIAGS];
    int         n_diags;
} tal_lex_t;

/* ---- Symbol ---- */

typedef enum {
    TS_VAR, TS_LITERAL, TS_DEFINE, TS_PROC, TS_SUBPROC,
    TS_LABEL, TS_ENTRY, TS_STRUCT, TS_FIELD, TS_PARAM
} tal_sk_t;

typedef enum {
    TT_NONE, TT_STRING, TT_INT, TT_INT32, TT_FIXED,
    TT_REAL, TT_REAL64, TT_UNSIGNED, TT_STRUCT
} tal_type_t;

typedef struct {
    char        name[TAL_MAX_IDENT];
    uint8_t     kind;
    uint8_t     type;
    uint8_t     scope;
    uint8_t     is_array;
    uint8_t     is_ptr;
    uint8_t     is_readonly;
    uint32_t    def_line;
    uint32_t    def_col;
    uint32_t    def_tok;
    uint32_t    end_tok;
    uint32_t    parent;
    uint32_t    ref_count;
    uint32_t    body_off;
    uint32_t    body_len;
} tal_sym_t;

/* ---- Symbol Table ---- */

typedef struct {
    tal_sym_t   syms[TAL_MAX_SYMS];
    uint32_t    n_syms;
    uint8_t     scope_depth;
} tal_symtab_t;

/* ---- AST Node Types ---- */

typedef enum {
    PN_PROC,
    PN_PARAM,
    PN_DECL,
    PN_STRUCT_DECL,
    PN_FIELD,
    PN_LITERAL,
    PN_DEFINE,
    PN_ASSIGN,
    PN_IF,
    PN_WHILE,
    PN_FOR,
    PN_DO,
    PN_CASE,
    PN_CALL,
    PN_RETURN,
    PN_GOTO,
    PN_ASSERT,
    PN_BEGIN,
    PN_LABEL,
    PN_MOVE,
    PN_SCAN,
    PN_CODE,
    PN_USE,
    PN_DROP,
    PN_STORE,
    PN_BINOP,
    PN_UNOP,
    PN_INTLIT,
    PN_REALLIT,
    PN_STRLIT,
    PN_IDENT,
    PN_ARRREF,
    PN_DOTREF,
    PN_DEREF,
    PN_ADDROF,
    PN_FUNCCALL,
    PN_STDFUNC,
    PN_EMPTY,
    PN_SOURCE,
    PN_COUNT
} pn_kind_t;

/* ---- AST Node ---- */

typedef struct {
    uint8_t  kind;
    uint8_t  pad;
    uint16_t flags;
    uint32_t tok;
    uint32_t n_kids;
    uint32_t first;
    uint32_t last;
    uint32_t next;
    uint32_t etok;
} pn_node_t;

/* ---- Parse Context ---- */

#define PN_MAX_NODES (1 << 16)

typedef struct {
    const tal_tok_t *toks;
    uint32_t         n_toks;
    uint32_t         pos;
    const tal_lex_t *lex;

    pn_node_t        nodes[PN_MAX_NODES];
    uint32_t         n_nodes;

    tal_diag_t       diags[TAL_MAX_DIAGS];
    int              n_diags;
} parse_ctx_t;

/* ---- API ---- */

int  tal_lex(tal_lex_t *L, const char *src, uint32_t len);

uint32_t tal_intern(tal_lex_t *L, const char *s, uint32_t len);

const char *tal_str(const tal_lex_t *L, uint32_t off);

uint32_t tal_parse(parse_ctx_t *P, const tal_tok_t *toks,
                   uint32_t n_toks, const tal_lex_t *lex);

const char *tal_tk_name(uint16_t kind);

void tal_sema(tal_symtab_t *S, const parse_ctx_t *P, const tal_lex_t *L);

const tal_sym_t *tal_sym_lookup(const tal_symtab_t *S, const char *name);

const tal_sym_t *tal_sym_use(const tal_symtab_t *S, const char *name, uint32_t tok);

const tal_sym_t *tal_sym_at(const tal_symtab_t *S,
                            const tal_lex_t *L, const parse_ctx_t *P,
                            uint32_t line, uint32_t col);

void tal_sema_refs(tal_symtab_t *S, const tal_lex_t *L);

const char *tal_define_body(const tal_sym_t *sym, const tal_lex_t *L);

/* ---- Guardian procedures and standard functions ---- */

typedef struct {
    const char *name;
    const char *summary;
    const char *syntax;
    uint8_t     superseded;
} gp_ent_t;

typedef struct {
    const char *name;
    const char *desc;
    const char *syntax;
} sf_ent_t;

extern const gp_ent_t gp_tab[];
extern const uint32_t gp_n;
extern const sf_ent_t sf_tab[];
extern const uint32_t sf_n;

const gp_ent_t *gp_find(const char *s, uint32_t len);
const sf_ent_t *sf_find(const char *s, uint32_t len);

#endif
