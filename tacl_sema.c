/* tacl_sema.c -- one pass over TACL tokens: bracket balance, enclosure
 * labels, directives, declarations, references and completion context.
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#include "tacl.h"

/* ---- Frames ---- */

enum { F_NONE, F_IF, F_CASE, F_LOOP, F_DEF, F_ARG, F_OTHER };

typedef struct {
    uint32_t tok;
    uint8_t  fn;
    uint8_t  seen;
    uint8_t  other;
} frame_t;

typedef struct {
    tal_symtab_t *S;
    tal_lex_t    *L;
    tal_diag_t   *diags;
    int          *nd;
    frame_t       fr[TC_MAXNEST];
    int           depth;
    int           scope;
    uint32_t      strct;
    int           instr;
    int           first;
    int           framed;
} pass_t;

/* ---- Helpers ---- */

static void diag(pass_t *C, uint8_t sev, const tal_tok_t *t, const char *msg)
{
    if (*C->nd >= TAL_MAX_DIAGS) return;
    tal_diag_t *d = &C->diags[(*C->nd)++];
    d->line = t->line;
    d->col = t->col;
    d->len = t->len ? t->len : 1;
    d->severity = sev;
    snprintf(d->msg, sizeof d->msg, "%s", msg);
}

static const char *text(pass_t *C, const tal_tok_t *t)
{
    return tal_str(C->L, t->off);
}

static int eqi(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}

static int isw(pass_t *C, const tal_tok_t *t, const char *w)
{
    uint32_t n = (uint32_t)strlen(w);
    return t->len == n && eqi(text(C, t), w, n);
}

static uint32_t base(const char *s, uint32_t len)
{
    uint32_t i = 0;
    if (len && s[0] == ':') return len;
    while (i < len && s[i] != '.' && s[i] != ':' && s[i] != '(') i++;
    return i;
}

static tal_sym_t *lookup(pass_t *C, const char *s, uint32_t len)
{
    if (len == 0 || len >= TAL_MAX_IDENT) return NULL;
    for (uint32_t i = C->S->n_syms; i > 0; i--) {
        tal_sym_t *y = &C->S->syms[i - 1];
        if (strlen(y->name) == len && eqi(y->name, s, len)) return y;
    }
    return NULL;
}

static tal_sym_t *add(pass_t *C, const tal_tok_t *t, uint32_t ti,
                      const char *s, uint32_t len, uint8_t kind)
{
    if (C->S->n_syms >= TAL_MAX_SYMS || len == 0 || len >= TAL_MAX_IDENT) return NULL;
    tal_sym_t *y = &C->S->syms[C->S->n_syms++];
    memset(y, 0, sizeof *y);
    memcpy(y->name, s, len);
    y->name[len] = '\0';
    y->kind = kind;
    y->scope = (uint8_t)C->scope;
    y->def_line = t->line;
    y->def_col = t->col;
    y->def_tok = ti;
    y->parent = C->instr ? C->strct : 0;
    return y;
}

static uint8_t vtype(pass_t *C, const tal_tok_t *t)
{
    static const struct { const char *w; uint8_t k; } m[] = {
        {"TEXT", TC_S_TEXT}, {"ALIAS", TC_S_ALIAS}, {"MACRO", TC_S_MACRO},
        {"ROUTINE", TC_S_ROUTINE}, {"STRUCT", TC_S_STRUCT},
        {"DIRECTORY", TC_S_DIRECTORY}, {"DELTA", TC_S_DELTA}, {NULL, 0}
    };
    for (int i = 0; m[i].w; i++)
        if (isw(C, t, m[i].w)) return m[i].k;
    return 0;
}

const char *tc_kname(uint8_t kind)
{
    switch (kind) {
    case TC_S_TEXT:      return "TEXT";
    case TC_S_ALIAS:     return "ALIAS";
    case TC_S_MACRO:     return "MACRO";
    case TC_S_ROUTINE:   return "ROUTINE";
    case TC_S_STRUCT:    return "STRUCT";
    case TC_S_DIRECTORY: return "DIRECTORY";
    case TC_S_DELTA:     return "DELTA";
    case TC_S_ITEM:      return "STRUCT item";
    case TC_S_PUSHED:    return "pushed variable";
    default:             return "variable";
    }
}

/* ---- Directives ---- */

static void direct(pass_t *C, const tal_tok_t *t, uint32_t ti)
{
    const char *s = text(C, t);
    char w[3][TAL_MAX_IDENT];
    uint32_t off[3] = {0, 0, 0};
    int n = 0;
    uint32_t i = 1;
    for (; n < 3; n++) {
        while (i < t->len && (s[i] == ' ' || s[i] == '\t')) i++;
        uint32_t st = i;
        while (i < t->len && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') i++;
        if (i == st || i - st >= TAL_MAX_IDENT) break;
        memcpy(w[n], s + st, i - st);
        w[n][i - st] = '\0';
        off[n] = st;
    }
    C->instr = 0;
    if (n == 0) { diag(C, 2, t, "? alone is a command line directive, not a file directive"); return; }
    if (eqi(w[0], "TACL", 5)) {
        if (!C->first) diag(C, 1, t, "?TACL must be the first line of the file");
        if (n < 2 || !vtype(C, &(tal_tok_t){.off = t->off + off[1], .len = (uint32_t)strlen(w[1])}))
            diag(C, 1, t, "?TACL needs a type: TEXT, ALIAS, MACRO, ROUTINE, STRUCT, DIRECTORY or DELTA");
        else if (eqi(w[1], "STRUCT", 7)) C->instr = 1;
        return;
    }
    if (eqi(w[0], "SECTION", 8)) {
        if (n < 3) { diag(C, 1, t, "?SECTION needs a name and a type"); return; }
        tal_tok_t nt = {.off = t->off + off[1], .len = (uint32_t)strlen(w[1]), .line = t->line, .col = t->col + off[1]};
        tal_tok_t tt = {.off = t->off + off[2], .len = (uint32_t)strlen(w[2])};
        uint8_t k = vtype(C, &tt);
        if (!k) { diag(C, 1, t, "?SECTION type must be TEXT, ALIAS, MACRO, ROUTINE, STRUCT, DIRECTORY or DELTA"); return; }
        int save = C->scope;
        C->scope = 0;
        tal_sym_t *y = add(C, &nt, ti, w[1], nt.len, k);
        C->scope = save;
        if (y && k == TC_S_STRUCT) { C->instr = 1; C->strct = (uint32_t)(y - C->S->syms); }
        return;
    }
    if (eqi(w[0], "FORMAT", 7)) {
        if (n < 2 || !(eqi(w[1], "PLAIN", 6) || eqi(w[1], "QUOTED", 7) || eqi(w[1], "TACL", 5)))
            diag(C, 1, t, "?FORMAT takes PLAIN, QUOTED or TACL");
        return;
    }
    if (eqi(w[0], "BLANK", 6)) return;
    diag(C, 2, t, "not a TACL directive (?TACL, ?SECTION, ?FORMAT, ?BLANK)");
}

/* ---- Labels ---- */

static void label(pass_t *C, const tal_tok_t *t)
{
    const char *s = text(C, t);
    uint32_t a = 1, b = t->len > 1 ? t->len - 1 : 1;
    while (a < b && s[a] == ' ') a++;
    while (b > a && s[b - 1] == ' ') b--;
    uint32_t n = b - a;
    frame_t *f = C->depth ? &C->fr[C->depth - 1] : NULL;
    if (!f || f->fn == F_NONE || f->fn == F_OTHER || f->fn == F_ARG) {
        diag(C, 1, t, "a |label| only belongs inside [#IF ...], [#CASE ...], [#LOOP ...] or [#DEF ...]");
        return;
    }
    if (f->other) diag(C, 1, t, "|OTHERWISE| must be the last label in a #CASE");
    if (f->fn == F_IF) {
        if (n == 4 && eqi(s + a, "THEN", 4)) { if (f->seen & 1) diag(C, 1, t, "#IF has a second |THEN|"); f->seen |= 1; }
        else if (n == 4 && eqi(s + a, "ELSE", 4)) { if (!(f->seen & 1)) diag(C, 1, t, "|ELSE| before |THEN|"); if (f->seen & 2) diag(C, 1, t, "#IF has a second |ELSE|"); f->seen |= 2; }
        else diag(C, 1, t, "#IF takes |THEN| and |ELSE| only");
    } else if (f->fn == F_LOOP) {
        if (n == 5 && eqi(s + a, "WHILE", 5)) { if (f->seen) diag(C, 1, t, "|WHILE| must come first in a #LOOP"); f->seen |= 1; }
        else if (n == 2 && eqi(s + a, "DO", 2)) { if (f->seen & 2) diag(C, 1, t, "#LOOP has a second |DO|"); f->seen |= 2; }
        else if (n == 5 && eqi(s + a, "UNTIL", 5)) { if (!(f->seen & 2) || (f->seen & 1)) diag(C, 1, t, "|UNTIL| follows |DO| and cannot be mixed with |WHILE|"); f->seen |= 4; }
        else diag(C, 1, t, "#LOOP takes |WHILE| |DO| or |DO| |UNTIL|");
    } else if (f->fn == F_DEF) {
        if (!(n == 4 && eqi(s + a, "BODY", 4))) diag(C, 2, t, "#DEF ignores labels other than |BODY|");
        f->seen |= 1;
    } else if (f->fn == F_CASE) {
        if (n == 9 && eqi(s + a, "OTHERWISE", 9)) f->other = 1;
        f->seen = 1;
    }
}

/* ---- Statements ---- */

static uint8_t fnkind(pass_t *C, const tal_tok_t *t)
{
    if (t->kind != TC_BUILTIN) return F_OTHER;
    if (isw(C, t, "#IF")) return F_IF;
    if (isw(C, t, "#CASE")) return F_CASE;
    if (isw(C, t, "#LOOP")) return F_LOOP;
    if (isw(C, t, "#DEF")) return F_DEF;
    if (isw(C, t, "#ARGUMENT")) return F_ARG;
    return F_OTHER;
}

static uint32_t decls(pass_t *C, uint32_t i, uint8_t kind)
{
    tal_tok_t *tk = C->L->toks;
    uint32_t n = C->L->n_toks;
    for (uint32_t k = 0; k < 64 && i < n; k++, i++) {
        if (tk[i].kind == TC_EOL || tk[i].kind == TC_RBRACK || tk[i].kind == TC_EOF) break;
        if (tk[i].kind != TC_WORD) continue;
        const char *s = text(C, &tk[i]);
        add(C, &tk[i], i, s, base(s, tk[i].len), kind);
        tk[i].flags = 1;
    }
    return i;
}

static uint32_t def(pass_t *C, uint32_t i)
{
    tal_tok_t *tk = C->L->toks;
    if (i + 2 >= C->L->n_toks || tk[i + 1].kind != TC_WORD) {
        diag(C, 1, &tk[i], "#DEF needs a name and a type");
        return i + 1;
    }
    uint8_t k = tk[i + 2].kind == TC_WORD ? vtype(C, &tk[i + 2]) : 0;
    if (!k) { diag(C, 1, &tk[i + 1], "#DEF type must be TEXT, ALIAS, MACRO, ROUTINE, STRUCT, DIRECTORY or DELTA"); return i + 2; }
    const char *s = text(C, &tk[i + 1]);
    tal_sym_t *y = add(C, &tk[i + 1], i + 1, s, base(s, tk[i + 1].len), k);
    tk[i + 1].flags = 1;
    if (y && k == TC_S_STRUCT) { C->instr = 1; C->strct = (uint32_t)(y - C->S->syms); }
    return i + 3;
}

static int sttype(pass_t *C, const tal_tok_t *t)
{
    if (t->kind != TC_WORD) return 0;
    for (uint32_t i = 0; i < tc_nst; i++)
        if (isw(C, t, tc_sttab[i])) return 1;
    return 0;
}

static uint32_t item(pass_t *C, uint32_t i)
{
    tal_tok_t *tk = C->L->toks;
    if (i + 1 < C->L->n_toks && tk[i + 1].kind == TC_WORD && !isw(C, &tk[i + 1], "REDEFINES")) {
        const char *s = text(C, &tk[i + 1]);
        add(C, &tk[i + 1], i + 1, s, base(s, tk[i + 1].len), TC_S_ITEM);
        tk[i + 1].flags = 1;
        return i + 2;
    }
    return i + 1;
}

static uint32_t builtin(pass_t *C, uint32_t i)
{
    tal_tok_t *t = &C->L->toks[i];
    const char *s = text(C, t);
    const tc_ent_t *e = tc_bfind(s, t->len);
    if (!e) { diag(C, 1, t, "not a TACL built-in function or variable"); return i + 1; }
    if (isw(C, t, "#FRAME")) { if (C->scope < TC_MAXFRAME) C->scope++; C->framed = 1; return i + 1; }
    if (isw(C, t, "#UNFRAME")) {
        if (!C->framed) diag(C, 2, t, "#UNFRAME with no #FRAME anywhere before it");
        if (C->scope > 0) C->scope--;
        return i + 1;
    }
    if (isw(C, t, "#PUSH")) return decls(C, i + 1, TC_S_PUSHED);
    if (isw(C, t, "#DEF")) return def(C, i);
    return i + 1;
}

static void refs(pass_t *C)
{
    tal_tok_t *tk = C->L->toks;
    for (uint32_t i = 0; i < C->L->n_toks; i++) {
        if (tk[i].kind != TC_WORD || tk[i].flags) continue;
        const char *s = text(C, &tk[i]);
        tal_sym_t *y = lookup(C, s, base(s, tk[i].len));
        if (y) y->ref_count++;
    }
    for (uint32_t i = 0; i < C->S->n_syms; i++) {
        tal_sym_t *y = &C->S->syms[i];
        if (y->kind != TC_S_PUSHED || y->ref_count) continue;
        tal_tok_t t = {.line = y->def_line, .col = y->def_col, .len = (uint32_t)strlen(y->name)};
        char msg[128];
        snprintf(msg, sizeof msg, "'%s' is pushed but never used", y->name);
        diag(C, 2, &t, msg);
    }
}

/* ---- Pass ---- */

void tc_pass(tal_symtab_t *S, tal_lex_t *L, tal_diag_t *diags, int *n_diags)
{
    if (!S || !L || !diags || !n_diags) return;
    pass_t C;
    memset(&C, 0, sizeof C);
    C.S = S; C.L = L; C.diags = diags; C.nd = n_diags;
    C.first = 1;
    memset(S, 0, sizeof *S);
    tal_tok_t *tk = L->toks;
    uint32_t i = 0;
    int start = 1;
    for (uint32_t n = 0; n < TAL_MAX_TOKS && i < L->n_toks; n++) {
        tal_tok_t *t = &tk[i];
        uint16_t k = t->kind;
        if (k == TC_EOF) break;
        if (k == TC_COMMENT || k == TC_CONT || k == TC_ESC) { i++; continue; }
        if (k == TC_DIRECT) { direct(&C, t, i); C.first = 0; i++; start = 1; continue; }
        C.first = 0;
        if (k == TC_EOL) { i++; start = 1; continue; }
        if (k == TC_LBRACK) {
            if (C.depth < TC_MAXNEST) {
                C.fr[C.depth].tok = i;
                C.fr[C.depth].fn = (i + 1 < L->n_toks) ? fnkind(&C, &tk[i + 1]) : F_NONE;
                C.fr[C.depth].seen = 0;
                C.fr[C.depth].other = 0;
                C.depth++;
            } else diag(&C, 1, t, "brackets nested deeper than 64");
            i++; start = 1; continue;
        }
        if (k == TC_RBRACK) {
            if (C.depth == 0) diag(&C, 1, t, "] with no matching [");
            else C.depth--;
            i++; start = 0; continue;
        }
        if (k == TC_LABEL) { label(&C, t); i++; start = 1; continue; }
        if (k == TC_BUILTIN) { i = builtin(&C, i); start = 0; continue; }
        if (k == TC_WORD && start && C.instr && sttype(&C, t)) { i = item(&C, i); start = 0; continue; }
        if (k == TC_WORD && start && C.instr && isw(&C, t, "END")) { i++; start = 0; continue; }
        i++; start = 0;
    }
    for (int d = C.depth; d > 0; d--)
        diag(&C, 1, &tk[C.fr[d - 1].tok], "[ is never closed");
    refs(&C);
}

/* ---- Completion context ---- */

static uint32_t lstart(const char *src, uint32_t len, uint32_t line)
{
    uint32_t off = 0, cur = 1;
    while (off < len && cur < line) { if (src[off] == '\n') cur++; off++; }
    return off;
}

static int has(const char *s, uint32_t n, const char *w)
{
    uint32_t wl = (uint32_t)strlen(w);
    for (uint32_t i = 0; i + wl <= n; i++)
        if (eqi(s + i, w, wl)) return 1;
    return 0;
}

tc_ctx_t tc_ctx(const char *src, uint32_t len, uint32_t line, uint32_t col)
{
    if (!src || line == 0 || col == 0) return TC_C_WORD;
    uint32_t ls = lstart(src, len, line);
    uint32_t pos = ls + col - 1;
    if (pos > len) pos = len;
    uint32_t ws = pos;
    while (ws > ls && src[ws - 1] != ' ' && src[ws - 1] != '\t' && src[ws - 1] != '[' &&
           src[ws - 1] != ']' && src[ws - 1] != '(' && src[ws - 1] != ',' && src[ws - 1] != '/')
        ws--;
    uint32_t fs = ls;
    while (fs < pos && (src[fs] == ' ' || src[fs] == '\t')) fs++;
    if (src[ws] == '#') return TC_C_BUILTIN;
    if (src[ws] == '|') return TC_C_LABEL;
    if (src[fs] == '?' && ws == fs) return TC_C_DIRECT;
    if (src[fs] == '?' && (has(src + fs, ws - fs, "?SECTION") || has(src + fs, ws - fs, "?TACL"))) {
        uint32_t words = 0, k = fs;
        for (uint32_t n = 0; n < 8 && k < ws; n++) {
            while (k < ws && src[k] != ' ' && src[k] != '\t') k++;
            words++;
            while (k < ws && (src[k] == ' ' || src[k] == '\t')) k++;
        }
        if ((has(src + fs, 5, "?TACL") && words == 1) || words == 2) return TC_C_SECTYPE;
    }
    if (has(src + ls, ws - ls, "#DEF ")) {
        uint32_t k = ws;
        while (k > ls && src[k - 1] != '#') k--;
        uint32_t words = 0;
        for (uint32_t n = 0; n < 8 && k < ws; n++) {
            while (k < ws && src[k] != ' ' && src[k] != '\t') k++;
            words++;
            while (k < ws && (src[k] == ' ' || src[k] == '\t')) k++;
        }
        if (words == 2) return TC_C_SECTYPE;
    }
    if (has(src + ls, ws - ls, "#ARGUMENT")) return TC_C_ARGTYPE;
    return TC_C_WORD;
}
