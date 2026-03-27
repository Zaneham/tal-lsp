/* tal.h -- TAL Language Server Protocol
 *
 * Transaction Application Language, Tandem/HP NonStop.
 * The language that runs every stock exchange you've
 * never thought about and every ATM withdrawal you've
 * taken for granted.
 *
 * Built from the TAL Reference Manual (526371-001,
 * September 2003) and the TAL Programmer's Guide
 * (096254, September 1993).
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

/* ---- Token Types ----
 * Ordered to match the reference manual's chapter structure
 * so a maintenance programmer can cross-reference without
 * having to hold two mental models at once. */

typedef enum {
    /* Literals (Chapter 3: Data Representation) */
    TK_INT_LIT,         /* decimal, octal (%nnn), hex (%Hnn) */
    TK_REAL_LIT,        /* floating point */
    TK_STRING_LIT,      /* "..." */
    TK_SNUM_LIT,        /* STRING numeric constant e.g. %56 in string ctx */

    /* Identifier */
    TK_IDENT,

    /* Arithmetic operators (Chapter 4: Expressions) */
    TK_PLUS,            /* +   signed add */
    TK_MINUS,           /* -   signed subtract / unary negate */
    TK_STAR,            /* *   signed multiply */
    TK_SLASH,           /* /   signed divide */
    TK_LSHIFT,          /* <<  left shift */
    TK_RSHIFT,          /* >>  right shift (arithmetic) */
    TK_PLUS_UNSIGNED,   /* (+) unsigned add */
    TK_MINUS_UNSIGNED,  /* (-) unsigned subtract */
    TK_STAR_UNSIGNED,   /* (*) unsigned multiply */
    TK_SLASH_UNSIGNED,  /* (/) unsigned divide */

    /* Bitwise logical operators */
    TK_LAND,            /* LAND */
    TK_LOR,             /* LOR  */
    TK_XOR,             /* XOR  */

    /* Relational operators */
    TK_EQ,              /* =   */
    TK_NEQ,             /* <>  */
    TK_LT,              /* <   */
    TK_GT,              /* >   */
    TK_LE,              /* <=  */
    TK_GE,              /* >=  */

    /* Boolean operators */
    TK_AND,             /* AND */
    TK_OR,              /* OR  */
    TK_NOT,             /* NOT */

    /* Bit operations (Chapter 4: Bit Operations) */
    TK_LBRACKET,        /* <   bit extraction open */
    TK_RBRACKET,        /* >   bit extraction close */

    /* Assignment and delimiters */
    TK_ASSIGN,          /* :=  */
    TK_LPAREN,
    TK_RPAREN,
    TK_COMMA,
    TK_SEMICOLON,
    TK_COLON,
    TK_DOT,             /* structure member access */
    TK_AMPERSAND,       /* & concatenation in MOVE */
    TK_AT,              /* @ address-of */
    TK_APOSTROPHE,      /* ' dereference */
    TK_CARET,           /* ^ used in extended pointers */
    TK_EQUALS,          /* = in LITERAL/DEFINE context */

    /* Type keywords (Chapter 3: Data Types) */
    TK_STRING,
    TK_INT,
    TK_INT32,           /* INT(32) */
    TK_FIXED,           /* FIXED(fpoint) */
    TK_REAL,
    TK_REAL64,          /* REAL(64) */
    TK_UNSIGNED,        /* UNSIGNED(width) */

    /* Declaration keywords (Chapters 5-10) */
    TK_LITERAL,
    TK_DEFINE,
    TK_STRUCT,          /* STRUCT */
    TK_BEGIN,
    TK_END,
    TK_FILLER,
    TK_BIT_FILLER,

    /* Storage qualifiers */
    TK_GLOBAL,
    TK_LOCAL,
    TK_SUBLOCAL,

    /* Pointer keywords (Chapter 9) */
    TK_DOT_EXT,        /* .EXT extended pointer */

    /* Procedure keywords (Chapter 13: Procedures in ref) */
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

    /* Statement keywords (Chapter 12: Statements) */
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

    /* Block keywords (Chapter 11: NAMEs and BLOCKs) */
    TK_NAME,
    TK_BLOCK,

    /* Equivalence (Chapter 10) */
    TK_EQUIV,           /* = in equivalence context */

    /* Compiler directives (Chapter 16) */
    TK_DIRECTIVE,       /* ? prefix for directive lines */
    TK_SOURCE,          /* ?SOURCE directive */
    TK_HASH,            /* # DEFINE body terminator */

    /* Standard function prefix */
    TK_DOLLAR,          /* $ prefix for standard functions */

    /* Hardware indicators */
    TK_DOLLAR_CARRY,    /* $CARRY */
    TK_DOLLAR_OVERFLOW, /* $OVERFLOW */

    /* Read-only array */
    TK_READONLY,        /* 'P' prefix for read-only arrays */

    /* Sentinel */
    TK_COMMENT,         /* retained for LSP hover/highlight */
    TK_NEWLINE,
    TK_EOF,
    TK_COUNT
} tal_tk_t;

/* ---- Token ---- */

typedef struct {
    uint16_t kind;      /* tal_tk_t */
    uint16_t flags;
    uint32_t line;      /* 1-based source line */
    uint32_t col;       /* 1-based column */
    uint32_t off;       /* offset into string pool */
    uint32_t len;       /* token text length */
    int64_t  ival;      /* numeric value for literals */
} tal_tok_t;

/* ---- Error / Diagnostic ---- */

typedef struct {
    uint32_t line;
    uint32_t col;
    uint32_t len;
    uint8_t  severity;  /* 1=error, 2=warning, 3=info */
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
    uint8_t     kind;       /* tal_sk_t */
    uint8_t     type;       /* tal_type_t */
    uint8_t     scope;
    uint8_t     is_array;
    uint8_t     is_ptr;
    uint8_t     is_readonly;
    uint32_t    def_line;   /* where it was declared */
    uint32_t    def_col;
    uint32_t    def_tok;    /* token index of definition */
    uint32_t    parent;     /* struct parent sym index, or 0 */
    uint32_t    ref_count;  /* how many times referenced (for unused warnings) */
    uint32_t    body_off;   /* string pool offset for DEFINE body text */
    uint32_t    body_len;   /* length of body text (0 if none) */
} tal_sym_t;

/* ---- Symbol Table ---- */

typedef struct {
    tal_sym_t   syms[TAL_MAX_SYMS];
    uint32_t    n_syms;
    uint8_t     scope_depth;
} tal_symtab_t;

/* ---- AST Node Types ---- */

typedef enum {
    PN_PROC,        /* procedure declaration */
    PN_PARAM,       /* formal parameter */
    PN_DECL,        /* variable declaration */
    PN_STRUCT_DECL, /* structure declaration */
    PN_FIELD,       /* structure field */
    PN_LITERAL,     /* LITERAL declaration */
    PN_DEFINE,      /* DEFINE declaration */
    PN_ASSIGN,      /* assignment statement */
    PN_IF,          /* IF/THEN/ELSE */
    PN_WHILE,       /* WHILE/DO */
    PN_FOR,         /* FOR loop */
    PN_DO,          /* DO/UNTIL */
    PN_CASE,        /* CASE statement */
    PN_CALL,        /* CALL statement */
    PN_RETURN,      /* RETURN statement */
    PN_GOTO,        /* GOTO */
    PN_ASSERT,      /* ASSERT */
    PN_BEGIN,       /* BEGIN/END block */
    PN_LABEL,       /* label: */
    PN_MOVE,        /* MOVE statement */
    PN_SCAN,        /* SCAN/RSCAN */
    PN_CODE,        /* CODE block */
    PN_USE,         /* USE statement */
    PN_DROP,        /* DROP statement */
    PN_STORE,       /* STORE statement */
    PN_BINOP,       /* binary expression */
    PN_UNOP,        /* unary expression */
    PN_INTLIT,      /* integer literal */
    PN_REALLIT,     /* real literal */
    PN_STRLIT,      /* string literal */
    PN_IDENT,       /* identifier reference */
    PN_ARRREF,      /* array reference: id[expr] */
    PN_DOTREF,      /* structure member: a.b */
    PN_DEREF,       /* pointer deref: p' */
    PN_ADDROF,      /* address-of: @var */
    PN_FUNCCALL,    /* function call in expression */
    PN_STDFUNC,     /* $function call */
    PN_EMPTY,       /* placeholder for optional parts */
    PN_SOURCE,      /* ?SOURCE directive */
    PN_COUNT
} pn_kind_t;

/* ---- AST Node ---- */

#define PN_MAX_KIDS  8

typedef struct {
    uint8_t  kind;        /* pn_kind_t */
    uint8_t  n_kids;
    uint16_t flags;
    uint32_t tok;         /* token index this node came from */
    uint32_t kids[PN_MAX_KIDS];
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

/* Lexer: tokenise TAL source */
int  tal_lex(tal_lex_t *L, const char *src, uint32_t len);

/* Intern a token string and return pool offset */
uint32_t tal_intern(tal_lex_t *L, const char *s, uint32_t len);

/* Retrieve interned string */
const char *tal_str(const tal_lex_t *L, uint32_t off);

/* Parser: build AST from tokens */
uint32_t tal_parse(parse_ctx_t *P, const tal_tok_t *toks,
                   uint32_t n_toks, const tal_lex_t *lex);

/* Human-readable token name for diagnostics */
const char *tal_tk_name(uint16_t kind);

/* Semantic analysis: walk AST, populate symbol table */
void tal_sema(tal_symtab_t *S, const parse_ctx_t *P, const tal_lex_t *L);

/* Find symbol by name (returns NULL if not found) */
const tal_sym_t *tal_sym_lookup(const tal_symtab_t *S, const char *name);

/* Find symbol at a given source position (line/col, 1-based) */
const tal_sym_t *tal_sym_at(const tal_symtab_t *S,
                            const tal_lex_t *L, const parse_ctx_t *P,
                            uint32_t line, uint32_t col);

/* Count references to all symbols (second pass after sema) */
void tal_sema_refs(tal_symtab_t *S, const tal_lex_t *L);

/* Get DEFINE body text (returns "" if no body) */
const char *tal_define_body(const tal_sym_t *sym, const tal_lex_t *L);

#endif /* TAL_H */
