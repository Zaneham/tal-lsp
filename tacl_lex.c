/* tacl_lex.c -- TACL lexer. Metacharacters, separators, comments and
 * directives as the TACL Reference Manual (429513-017) section 2 has them.
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#include "tacl.h"

/* ---- Character classes ---- */

static int sep(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' ||
           c == '(' || c == ')' || c == '/' || c == ';' || c == '\0';
}

static int meta(char c)
{
    return c == '[' || c == ']' || c == '|' || c == '{' || c == '}' ||
           c == '~' || c == '"';
}

static int opc(char c)
{
    return c == '=' || c == '<' || c == '>' || c == '+' || c == '*';
}

static int wordc(char c)
{
    return !sep(c) && !meta(c) && !opc(c) && c != '%';
}

/* ---- Cursor ---- */

static char at(const tal_lex_t *L, uint32_t off)
{
    if (L->pos + off >= L->slen) return '\0';
    return L->src[L->pos + off];
}

static void step(tal_lex_t *L)
{
    if (L->pos >= L->slen) return;
    if (L->src[L->pos] == '\n') { L->line++; L->col = 1; }
    else L->col++;
    L->pos++;
}

static void diag(tal_lex_t *L, uint8_t sev, uint32_t line, uint32_t col,
                 uint32_t len, const char *msg)
{
    if (L->n_diags >= TAL_MAX_DIAGS) return;
    tal_diag_t *d = &L->diags[L->n_diags++];
    d->line = line;
    d->col = col;
    d->len = len ? len : 1;
    d->severity = sev;
    snprintf(d->msg, sizeof d->msg, "%s", msg);
}

static void put(tal_lex_t *L, uint16_t kind, uint32_t start, uint32_t sline,
                uint32_t scol, int64_t ival)
{
    if (L->n_toks >= TAL_MAX_TOKS || start > L->pos) return;
    uint32_t len = L->pos - start;
    tal_tok_t *t = &L->toks[L->n_toks++];
    t->kind = kind;
    t->flags = 0;
    t->line = sline;
    t->col = scol;
    t->len = len;
    t->ival = ival;
    t->off = tal_intern(L, L->src + start, len);
}

/* ---- Pieces ---- */

static void eqcmt(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    for (int n = 0; n < TC_MAXCONT; n++) {
        while (at(L, 0) != '\n' && at(L, 0) != '\0') step(L);
        uint32_t k = L->pos;
        while (k > start && (L->src[k - 1] == ' ' || L->src[k - 1] == '\r')) k--;
        if (k == start || L->src[k - 1] != '&' || at(L, 0) != '\n') break;
        step(L);
    }
    put(L, TC_COMMENT, start, sline, scol, 0);
}

static void brcmt(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    step(L);
    for (uint32_t n = 0; n < TAL_MAX_STRS; n++) {
        char c = at(L, 0);
        if (c == '}' || c == '\0') break;
        if (c == '{') {
            diag(L, 1, L->line, L->col, 1, "a { inside a { } comment is an error");
        }
        if (c == '\n') {
            uint32_t k = L->pos;
            while (k > start && (L->src[k - 1] == ' ' || L->src[k - 1] == '\r')) k--;
            if (k == start || L->src[k - 1] != '&') {
                diag(L, 1, sline, scol, L->pos - start,
                     "{ comment must close on the same line, or end the line with &");
                break;
            }
        }
        step(L);
    }
    if (at(L, 0) == '}') step(L);
    put(L, TC_COMMENT, start, sline, scol, 0);
}

static void str(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    step(L);
    while (at(L, 0) != '"' && at(L, 0) != '\n' && at(L, 0) != '\0') step(L);
    if (at(L, 0) == '"') step(L);
    else diag(L, 2, sline, scol, L->pos - start, "string not closed before end of line");
    put(L, TC_STR, start, sline, scol, 0);
}

static void label(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    step(L);
    while (at(L, 0) != '|' && at(L, 0) != '\n' && at(L, 0) != '\0' &&
           at(L, 0) != '[' && at(L, 0) != ']')
        step(L);
    if (at(L, 0) != '|') {
        L->pos = start + 1;
        L->col = scol + 1;
        put(L, TC_OP, start, sline, scol, 0);
        diag(L, 1, sline, scol, 1, "| starts a label and needs a closing | on the same line");
        return;
    }
    step(L);
    put(L, TC_LABEL, start, sline, scol, 0);
}

static void direct(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    while (at(L, 0) != '\n' && at(L, 0) != '\0') step(L);
    put(L, TC_DIRECT, start, sline, scol, 0);
}

static void macarg(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    step(L);
    if (at(L, 0) == '%') { step(L); put(L, TC_ESC, start, sline, scol, 0); return; }
    for (uint32_t n = 0; n < 32 && at(L, 0) != '%' && at(L, 0) != '\n' && at(L, 0) != '\0'; n++)
        step(L);
    if (at(L, 0) != '%') {
        diag(L, 2, sline, scol, L->pos - start, "% starts a macro argument and needs a closing %");
        put(L, TC_WORD, start, sline, scol, 0);
        return;
    }
    step(L);
    put(L, TC_MACARG, start, sline, scol, 0);
}

static void tilde(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    step(L);
    char c = at(L, 0);
    if (c == ';') { step(L); put(L, TC_EOL, start, sline, scol, 0); return; }
    if (c == '=' && at(L, 1) == '=') { step(L); step(L); put(L, TC_ESC, start, sline, scol, 0); return; }
    if (meta(c) || c == '_' || c == '~') { step(L); put(L, TC_ESC, start, sline, scol, 0); return; }
    if (c != '\n' && c != '\0') step(L);
    put(L, TC_ESC, start, sline, scol, 0);
    diag(L, 2, sline, scol, 2, "~ before a character that is not a metacharacter becomes ?");
}

static int quoted(tal_lex_t *L)
{
    uint32_t k = 1, n = 0;
    while (n < 4 && (at(L, k) == '!' || opc(at(L, k)))) { k++; n++; }
    return n > 0 && at(L, k) == '\'';
}

static void word(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    int digits = 1;
    if (at(L, 0) == '+' || at(L, 0) == '-') step(L);
    if (at(L, 0) == '#' || at(L, 0) == '=') { step(L); digits = 0; }
    for (uint32_t n = 0; n < TC_MAXLINE; n++) {
        char c = at(L, 0);
        if (c == '\'' && quoted(L) && L->pos > start) break;
        if (c == '-' && !isalnum((unsigned char)at(L, 1)) && at(L, 1) != '-') break;
        if (c == '*' && !sep(at(L, 1)) && L->pos > start) { step(L); digits = 0; continue; }
        if (!wordc(c)) break;
        if (!isdigit((unsigned char)c)) digits = 0;
        step(L);
    }
    uint16_t kind = TC_WORD;
    int64_t ival = 0;
    if (L->src[start] == '#' && L->pos - start > 1) kind = TC_BUILTIN;
    else if (digits && L->pos > start) {
        kind = TC_NUM;
        for (uint32_t k = start; k < L->pos && k - start < 20; k++) {
            if (L->src[k] == '+' || L->src[k] == '-') continue;
            ival = ival * 10 + (L->src[k] - '0');
        }
        if (L->src[start] == '-') ival = -ival;
    }
    if (L->pos == start) step(L);
    put(L, kind, start, sline, scol, ival);
}

static void op(tal_lex_t *L)
{
    uint32_t start = L->pos, sline = L->line, scol = L->col;
    char c = at(L, 0);
    if (c == '\'') {
        for (uint32_t n = 0; n < 6; n++) { step(L); if (at(L, 0) == '\'') break; }
        if (at(L, 0) == '\'') step(L);
    } else if ((c == '<' && (at(L, 1) == '>' || at(L, 1) == '=')) ||
               (c == '>' && at(L, 1) == '=')) {
        step(L); step(L);
    } else {
        step(L);
    }
    put(L, TC_OP, start, sline, scol, 0);
}

/* ---- Lexer ---- */

static void newline(tal_lex_t *L, uint32_t lstart, int cont)
{
    if (L->pos - lstart > TC_MAXLINE)
        diag(L, 2, L->line, 1, TC_MAXLINE, "line is longer than 239 characters, TACL truncates it");
    uint32_t sline = L->line, scol = L->col, start = L->pos;
    step(L);
    if (!cont) put(L, TC_EOL, start, sline, scol, 0);
}

static int amp(tal_lex_t *L)
{
    uint32_t k = 1;
    while (at(L, k) == ' ' || at(L, k) == '\t' || at(L, k) == '\r') k++;
    char c = at(L, k);
    return c == '\n' || c == '\0' || c == '{' || (c == '=' && at(L, k + 1) == '=');
}

int tc_lex(tal_lex_t *L, const char *src, uint32_t len)
{
    if (!L || !src) return -1;
    memset(L, 0, sizeof *L);
    L->src = src;
    L->slen = len;
    L->line = 1;
    L->col = 1;
    uint32_t lstart = 0;
    int cont = 0;
    for (uint32_t n = 0; n < TAL_MAX_TOKS && L->pos < L->slen; n++) {
        char c = at(L, 0);
        uint32_t start = L->pos, sline = L->line, scol = L->col;
        if (c == '\n') { newline(L, lstart, cont); lstart = L->pos; cont = 0; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { step(L); continue; }
        if (c == '?' && L->pos == lstart) { direct(L); continue; }
        if (c == '=' && at(L, 1) == '=') { eqcmt(L); continue; }
        if (c == '{') { brcmt(L); continue; }
        if (c == '}') { step(L); put(L, TC_OP, start, sline, scol, 0);
                        diag(L, 1, sline, scol, 1, "} with no opening {"); continue; }
        if (c == '"') { str(L); continue; }
        if (c == '|') { label(L); continue; }
        if (c == '~') { tilde(L); continue; }
        if (c == '%') { macarg(L); continue; }
        if (c == '&' && amp(L)) { step(L); put(L, TC_CONT, start, sline, scol, 0); cont = 1; continue; }
        if (c == '[') { step(L); put(L, TC_LBRACK, start, sline, scol, 0); continue; }
        if (c == ']') { step(L); put(L, TC_RBRACK, start, sline, scol, 0); continue; }
        if (c == '(') { step(L); put(L, TC_LPAREN, start, sline, scol, 0); continue; }
        if (c == ')') { step(L); put(L, TC_RPAREN, start, sline, scol, 0); continue; }
        if (c == ',') { step(L); put(L, TC_COMMA, start, sline, scol, 0); continue; }
        if (c == ';') { step(L); put(L, TC_SEMI, start, sline, scol, 0); continue; }
        if (c == '/') { step(L); put(L, TC_SLASH, start, sline, scol, 0); continue; }
        if (c == '\'' && quoted(L)) { op(L); continue; }
        if (c == '=' && isalpha((unsigned char)at(L, 1))) { word(L); continue; }
        if ((c == '+' || c == '-') && isdigit((unsigned char)at(L, 1))) { word(L); continue; }
        if (c == '*' && sep(at(L, 1))) { op(L); continue; }
        if (opc(c)) { op(L); continue; }
        word(L);
    }
    if (L->pos - lstart > TC_MAXLINE)
        diag(L, 2, L->line, 1, TC_MAXLINE, "line is longer than 239 characters, TACL truncates it");
    put(L, TC_EOL, L->pos, L->line, L->col, 0);
    put(L, TC_EOF, L->pos, L->line, L->col, 0);
    return L->n_diags == 0 ? 0 : -1;
}
