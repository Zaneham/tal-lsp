/* tal_lex.c -- TAL lexer. Keywords case-insensitive, identifiers kept as
 * written, both comment forms (! and --) kept as tokens.
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#include "tal.h"
#include <ctype.h>

/* ---- Character Helpers ---- */

static int is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '^';
}

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static int is_hex(char c)
{
    return is_digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

static int is_octal(char c)
{
    return c >= '0' && c <= '7';
}

static int is_alnum(char c)
{
    return is_alpha(c) || is_digit(c);
}

/* ---- String Pool ---- */

uint32_t tal_intern(tal_lex_t *L, const char *s, uint32_t len)
{
    if (L->str_len + len + 1 >= TAL_MAX_STRS) return 0;
    uint32_t off = L->str_len;
    memcpy(L->strs + off, s, len);
    L->strs[off + len] = '\0';
    L->str_len += len + 1;
    return off;
}

const char *tal_str(const tal_lex_t *L, uint32_t off)
{
    if (off >= L->str_len) return "";
    return L->strs + off;
}

/* ---- Diagnostic ---- */

static void lex_diag(tal_lex_t *L, uint8_t sev, const char *msg)
{
    if (L->n_diags >= TAL_MAX_DIAGS) return;
    tal_diag_t *d = &L->diags[L->n_diags++];
    d->line = L->line;
    d->col  = L->col;
    d->len  = 1;
    d->severity = sev;
    snprintf(d->msg, sizeof(d->msg), "%s", msg);
}

/* ---- Peek / Advance ---- */

static char peek(tal_lex_t *L)
{
    if (L->pos >= L->slen) return '\0';
    return L->src[L->pos];
}

static char peek2(tal_lex_t *L)
{
    if (L->pos + 1 >= L->slen) return '\0';
    return L->src[L->pos + 1];
}

static char advance(tal_lex_t *L)
{
    if (L->pos >= L->slen) return '\0';
    char c = L->src[L->pos++];
    if (c == '\n') { L->line++; L->col = 1; }
    else { L->col++; }
    return c;
}

/* ---- Token Emit ---- */

static tal_tok_t *emit(tal_lex_t *L, uint16_t kind, uint32_t off,
                       uint32_t len, int64_t ival)
{
    if (L->n_toks >= TAL_MAX_TOKS - 1 && kind != TK_EOF) return NULL;
    if (L->n_toks >= TAL_MAX_TOKS) return NULL;
    tal_tok_t *t = &L->toks[L->n_toks++];
    t->kind  = kind;
    t->flags = 0;
    t->line  = L->line;
    t->col   = L->col;
    t->len   = len;
    t->ival  = ival;

    if (off == 0 && len > 0 && L->pos >= len) {
        t->off = tal_intern(L, L->src + L->pos - len, len);
    } else {
        t->off = off;
    }
    return t;
}

/* ---- Keyword Table ---- */

typedef struct { const char *name; uint16_t kind; } kw_entry_t;

static const kw_entry_t keywords[] = {
    {"AND",         TK_AND},
    {"ASSERT",      TK_ASSERT},
    {"BEGIN",       TK_BEGIN},
    {"BLOCK",       TK_BLOCK},
    {"BY",          TK_BY},
    {"CALL",        TK_CALL},
    {"CALLABLE",    TK_CALLABLE},
    {"CASE",        TK_CASE},
    {"CODE",        TK_CODE},
    {"DEFINE",      TK_DEFINE},
    {"DO",          TK_DO},
    {"DOWNTO",      TK_DOWNTO},
    {"DROP",        TK_DROP},
    {"ELSE",        TK_ELSE},
    {"END",         TK_END},
    {"ENTRY",       TK_ENTRY},
    {"EXTENSIBLE",  TK_EXTENSIBLE},
    {"EXTERNAL",    TK_EXTERNAL},
    {"FIXED",       TK_FIXED},
    {"FOR",         TK_FOR},
    {"FORWARD",     TK_FORWARD},
    {"GLOBAL",      TK_GLOBAL},
    {"GOTO",        TK_GOTO},
    {"IF",          TK_IF},
    {"INT",         TK_INT},
    {"INTERRUPT",   TK_INTERRUPT},
    {"LABEL",       TK_LABEL},
    {"LAND",        TK_LAND},
    {"LANGUAGE",    TK_LANGUAGE},
    {"LITERAL",     TK_LITERAL},
    {"LOCAL",       TK_LOCAL},
    {"LOR",         TK_LOR},
    {"MAIN",        TK_MAIN},
    {"MOVE",        TK_MOVE},
    {"NAME",        TK_NAME},
    {"NOT",         TK_NOT},
    {"OF",          TK_OF},
    {"OR",          TK_OR},
    {"OTHERWISE",   TK_OTHERWISE},
    {"PRIV",        TK_PRIV},
    {"PROC",        TK_PROC},
    {"REAL",        TK_REAL},
    {"RESIDENT",    TK_RESIDENT},
    {"RETURN",      TK_RETURN},
    {"RSCAN",       TK_RSCAN},
    {"SCAN",        TK_SCAN},
    {"STACK",       TK_STACK},
    {"STORE",       TK_STORE},
    {"STRING",      TK_STRING},
    {"STRUCT",      TK_STRUCT},
    {"SUBLOCAL",    TK_SUBLOCAL},
    {"SUBPROC",     TK_SUBPROC},
    {"THEN",        TK_THEN},
    {"TO",          TK_TO},
    {"UNSIGNED",    TK_UNSIGNED},
    {"UNTIL",       TK_UNTIL},
    {"USE",         TK_USE},
    {"VARIABLE",    TK_VARIABLE},
    {"WHILE",       TK_WHILE},
    {"XOR",         TK_XOR},
    {"BADDR",       TK_PTAL},
    {"CBADDR",      TK_PTAL},
    {"CWADDR",      TK_PTAL},
    {"EXTADDR",     TK_PTAL},
    {"EXT32ADDR",   TK_PTAL},
    {"EXT64ADDR",   TK_PTAL},
    {"FIELDALIGN",  TK_PTAL},
    {"PROCADDR",    TK_PTAL},
    {"PROC32ADDR",  TK_PTAL},
    {"PROC64ADDR",  TK_PTAL},
    {"PROCPTR",     TK_PTAL},
    {"PROC32PTR",   TK_PTAL},
    {"PROC64PTR",   TK_PTAL},
    {"REFALIGNED",  TK_PTAL},
    {"SGBADDR",     TK_PTAL},
    {"SGWADDR",     TK_PTAL},
    {"SGXBADDR",    TK_PTAL},
    {"SGXWADDR",    TK_PTAL},
    {"VOLATILE",    TK_PTAL},
    {"WADDR",       TK_PTAL},
    {NULL, 0}
};

static int ci_eq(const char *a, const char *b, int len)
{
    for (int i = 0; i < len; i++) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static uint16_t lookup_keyword(const char *s, int len)
{
    for (const kw_entry_t *kw = keywords; kw->name; kw++) {
        int kwlen = (int)strlen(kw->name);
        if (kwlen == len && ci_eq(s, kw->name, len))
            return kw->kind;
    }
    return TK_IDENT;
}

/* ---- Lexer Core ---- */

static void lex_comment(tal_lex_t *L)
{

    uint32_t start = L->pos - 1;
    uint32_t sline = L->line;
    uint32_t scol  = L->col - 1;

    while (L->pos < L->slen && L->src[L->pos] != '\n')
        advance(L);

    uint32_t len = L->pos - start;
    uint32_t off = tal_intern(L, L->src + start, len);
    tal_tok_t *t = emit(L, TK_COMMENT, off, len, 0);
    if (t) { t->line = sline; t->col = scol; }
}

static void lex_string(tal_lex_t *L)
{

    uint32_t start = L->pos;
    uint32_t sline = L->line;
    uint32_t scol  = L->col - 1;

    while (L->pos < L->slen && L->src[L->pos] != '"') {
        if (L->src[L->pos] == '\n') {
            lex_diag(L, 1, "unterminated string literal");
            break;
        }
        advance(L);
    }

    uint32_t len = L->pos - start;
    uint32_t off = tal_intern(L, L->src + start, len);

    if (L->pos < L->slen) advance(L);

    tal_tok_t *t = emit(L, TK_STRING_LIT, off, len, 0);
    if (t) { t->line = sline; t->col = scol; }
}

static void lex_number(tal_lex_t *L, char first)
{
    uint32_t start = L->pos - 1;
    uint32_t sline = L->line;
    uint32_t scol  = L->col - 1;
    int64_t val = 0;
    int is_real = 0;

    if (first == '%') {

        char next = peek(L);
        if (next == 'H' || next == 'h') {

            advance(L);
            while (L->pos < L->slen && is_hex(L->src[L->pos])) {
                char c = advance(L);
                int d = (c >= '0' && c <= '9') ? c - '0'
                      : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                      :                           c - 'a' + 10;
                val = (val << 4) | d;
            }
        } else if (next == 'B' || next == 'b') {

            advance(L);
            while (L->pos < L->slen && (L->src[L->pos] == '0' || L->src[L->pos] == '1')) {
                val = (val << 1) | (advance(L) - '0');
            }
        } else {

            while (L->pos < L->slen && is_octal(L->src[L->pos])) {
                val = (val << 3) | (advance(L) - '0');
            }
        }
    } else {

        val = first - '0';
        while (L->pos < L->slen && is_digit(L->src[L->pos]))
            val = val * 10 + (advance(L) - '0');

        if (peek(L) == '.') {
            is_real = 1;
            advance(L);
            while (L->pos < L->slen && is_digit(L->src[L->pos]))
                advance(L);
        }
        if (peek(L) == 'E' || peek(L) == 'e' ||
            peek(L) == 'L' || peek(L) == 'l') {
            is_real = 1;
            advance(L);
            if (peek(L) == '+' || peek(L) == '-') advance(L);
            while (L->pos < L->slen && is_digit(L->src[L->pos]))
                advance(L);
        } else if (peek(L) == 'D' || peek(L) == 'd' ||
                   peek(L) == 'F' || peek(L) == 'f') {
            advance(L);
        }
    }

    uint32_t len = L->pos - start;
    uint32_t off = tal_intern(L, L->src + start, len);
    uint16_t kind = is_real ? TK_REAL_LIT : TK_INT_LIT;

    tal_tok_t *t = emit(L, kind, off, len, val);
    if (t) { t->line = sline; t->col = scol; }
}

static void lex_ident(tal_lex_t *L, char first)
{
    uint32_t start = L->pos - 1;
    uint32_t sline = L->line;
    uint32_t scol  = L->col - 1;

    (void)first;
    while (L->pos < L->slen && is_alnum(L->src[L->pos]))
        advance(L);

    uint32_t len = L->pos - start;
    const char *text = L->src + start;

    if (text[0] == '$') {

        if (len == 6 && ci_eq(text, "$CARRY", 6)) {
            uint32_t off = tal_intern(L, text, len);
            tal_tok_t *t = emit(L, TK_DOLLAR_CARRY, off, len, 0);
            if (t) { t->line = sline; t->col = scol; }
            return;
        }
        if (len == 9 && ci_eq(text, "$OVERFLOW", 9)) {
            uint32_t off = tal_intern(L, text, len);
            tal_tok_t *t = emit(L, TK_DOLLAR_OVERFLOW, off, len, 0);
            if (t) { t->line = sline; t->col = scol; }
            return;
        }
        uint32_t off = tal_intern(L, text, len);
        tal_tok_t *t = emit(L, TK_DOLLAR, off, len, 0);
        if (t) { t->line = sline; t->col = scol; }
        return;
    }

    uint16_t kind = lookup_keyword(text, (int)len);

    if (kind == TK_INT && peek(L) == '(') {
        uint32_t save = L->pos;
        advance(L);
        if (peek(L) == '3' && peek2(L) == '2') {
            advance(L); advance(L);
            if (peek(L) == ')') {
                advance(L);
                len = L->pos - start;
                kind = TK_INT32;
            } else {
                L->pos = save;
            }
        } else {
            L->pos = save;
        }
    }

    if (kind == TK_REAL && peek(L) == '(') {
        uint32_t save = L->pos;
        advance(L);
        if (peek(L) == '6' && peek2(L) == '4') {
            advance(L); advance(L);
            if (peek(L) == ')') {
                advance(L);
                len = L->pos - start;
                kind = TK_REAL64;
            } else {
                L->pos = save;
            }
        } else {
            L->pos = save;
        }
    }

    uint32_t off = tal_intern(L, L->src + start, len);
    tal_tok_t *t = emit(L, kind, off, len, 0);
    if (t) { t->line = sline; t->col = scol; }
}

/* ---- Quoted operators and bases ---- */

static uint16_t lex_quoted(tal_lex_t *L)
{
    static const struct { const char *s; uint16_t kind; } q[] = {
        {"+", TK_PLUS_UNSIGNED}, {"-", TK_MINUS_UNSIGNED},
        {"*", TK_STAR_UNSIGNED}, {"/", TK_SLASH_UNSIGNED},
        {"\\", TK_MOD_UNSIGNED}, {"<<", TK_LSHIFT_UNSIGNED},
        {">>", TK_RSHIFT_UNSIGNED}, {":=", TK_MOVE_LR}, {"=:", TK_MOVE_RL},
        {"G", TK_BASE}, {"L", TK_BASE}, {"S", TK_BASE}, {"P", TK_BASE},
        {"SG", TK_BASE}, {"g", TK_BASE}, {"l", TK_BASE}, {"s", TK_BASE},
        {"p", TK_BASE}, {NULL, 0}
    };
    for (int i = 0; q[i].s; i++) {
        uint32_t n = (uint32_t)strlen(q[i].s);
        if (L->pos + n < L->slen && L->src[L->pos + n] == '\'' &&
            memcmp(L->src + L->pos, q[i].s, n) == 0) {
            for (uint32_t k = 0; k < n + 1; k++) advance(L);
            emit(L, q[i].kind, 0, n + 2, 0);
            return q[i].kind;
        }
    }
    return TK_EOF;
}

/* ---- Main Lexer Entry ---- */

int tal_lex(tal_lex_t *L, const char *src, uint32_t len)
{
    memset(L, 0, sizeof(*L));
    L->src  = src;
    L->slen = len;
    L->line = 1;
    L->col  = 1;

    while (L->pos < L->slen && L->n_toks < TAL_MAX_TOKS - 1) {
        char c = advance(L);

        if (c == ' ' || c == '\t' || c == '\r') continue;
        if (c == '\n') continue;

        if (c == '!') { lex_comment(L); continue; }
        if (c == '-' && peek(L) == '-') { advance(L); lex_comment(L); continue; }

        if (c == '"') { lex_string(L); continue; }

        if (is_digit(c)) { lex_number(L, c); continue; }
        if (c == '%') { lex_number(L, c); continue; }

        if (c == '?') {
            uint32_t dstart = L->pos - 1;
            uint32_t sline = L->line;
            uint32_t scol  = L->col - 1;

            while (L->pos < L->slen && is_alpha(L->src[L->pos]))
                advance(L);

            uint32_t kwlen = L->pos - dstart - 1;
            int is_source = (kwlen == 6 &&
                ci_eq(L->src + dstart + 1, "SOURCE", 6));

            while (L->pos < L->slen && (L->src[L->pos] == ' ' || L->src[L->pos] == '\t'))
                advance(L);

            uint32_t arg_start = L->pos;
            while (L->pos < L->slen && L->src[L->pos] != '\n')
                advance(L);

            if (is_source && L->pos > arg_start) {

                uint32_t arg_end = L->pos;
                while (arg_end > arg_start &&
                       (L->src[arg_end - 1] == ' ' || L->src[arg_end - 1] == '\r'))
                    arg_end--;
                uint32_t alen = arg_end - arg_start;
                uint32_t off = tal_intern(L, L->src + arg_start, alen);
                tal_tok_t *t = emit(L, TK_SOURCE, off, alen, 0);
                if (t) { t->line = sline; t->col = scol; }
            } else {

                uint32_t dlen = L->pos - dstart;
                uint32_t off = tal_intern(L, L->src + dstart, dlen);
                tal_tok_t *t = emit(L, TK_DIRECTIVE, off, dlen, 0);
                if (t) { t->line = sline; t->col = scol; }
            }
            continue;
        }

        if (c == '#') {
            emit(L, TK_HASH, 0, 1, 0);
            continue;
        }

        if (is_alpha(c) || c == '$') { lex_ident(L, c); continue; }

        if (c == ':' && peek(L) == '=') {
            advance(L);
            emit(L, TK_ASSIGN, 0, 2, 0);
            continue;
        }
        if (c == '<' && peek(L) == '>') {
            advance(L);
            emit(L, TK_NEQ, 0, 2, 0);
            continue;
        }
        if (c == '<' && peek(L) == '=') {
            advance(L);
            emit(L, TK_LE, 0, 2, 0);
            continue;
        }
        if (c == '>' && peek(L) == '=') {
            advance(L);
            emit(L, TK_GE, 0, 2, 0);
            continue;
        }
        if (c == '<' && peek(L) == '<') {
            advance(L);
            emit(L, TK_LSHIFT, 0, 2, 0);
            continue;
        }
        if (c == '>' && peek(L) == '>') {
            advance(L);
            emit(L, TK_RSHIFT, 0, 2, 0);
            continue;
        }

        if (c == '\'') {
            uint16_t kind = lex_quoted(L);
            if (kind != TK_EOF) continue;
            lex_diag(L, 1, "' must quote an operator ('+', '<<', ':=') or a base ('P', 'G', 'L', 'S')");
            continue;
        }

        if (c == '-' && peek(L) == '>') {
            advance(L);
            emit(L, TK_ARROW, 0, 2, 0);
            continue;
        }

        switch (c) {
        case '+': emit(L, TK_PLUS,      0, 1, 0); break;
        case '-': emit(L, TK_MINUS,     0, 1, 0); break;
        case '*': emit(L, TK_STAR,      0, 1, 0); break;
        case '/': emit(L, TK_SLASH,     0, 1, 0); break;
        case '(': emit(L, TK_LPAREN,    0, 1, 0); break;
        case ')': emit(L, TK_RPAREN,    0, 1, 0); break;
        case ',': emit(L, TK_COMMA,     0, 1, 0); break;
        case ';': emit(L, TK_SEMICOLON, 0, 1, 0); break;
        case ':': emit(L, TK_COLON,     0, 1, 0); break;
        case '.': emit(L, TK_DOT,       0, 1, 0); break;
        case '=': emit(L, TK_EQ,        0, 1, 0); break;
        case '<': emit(L, TK_LT,        0, 1, 0); break;
        case '>': emit(L, TK_GT,        0, 1, 0); break;
        case '@': emit(L, TK_AT,        0, 1, 0); break;
        case '&': emit(L, TK_AMPERSAND, 0, 1, 0); break;
        case '[': emit(L, TK_LBRACKET,  0, 1, 0); break;
        case ']': emit(L, TK_RBRACKET,  0, 1, 0); break;
        default:
            lex_diag(L, 1, "unexpected character");
            break;
        }
    }

    if (L->pos < L->slen)
        lex_diag(L, 2, "token limit reached, the rest of the file is not analysed");
    emit(L, TK_EOF, 0, 0, 0);
    return L->n_diags == 0 ? 0 : -1;
}
