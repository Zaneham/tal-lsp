/* tal_sema.c -- TAL semantic analysis
 *
 * Walks the AST after parsing and populates the symbol table.
 * This is the pass that actually understands what names mean,
 * which is surprisingly important when your job is to help
 * someone navigate a 30-year-old NonStop codebase at 3am.
 *
 * The symbol table is flat (linear scan for lookup) because
 * TAL programs are not large enough to justify a hash table
 * and the simplicity makes debugging pleasant. NonStop
 * developers deal with enough complexity already.
 *
 * Scope tracking is minimal: we push on PROC/BEGIN and pop
 * on END. This isn't perfect (TAL has SUBLOCAL scope and
 * NAME blocks with their own visibility rules) but it's
 * enough for go-to-definition and hover, which is what
 * people are actually begging for.
 *
 * After the declaration walk, a second pass counts references
 * to each symbol by scanning every TK_IDENT token. Symbols
 * with zero references get flagged as unused -- the kind of
 * warning that catches dead code before it becomes folklore.
 */

#include "tal.h"
#include <stdio.h>
#include <ctype.h>

/* ---- Internal Context ---- */

typedef struct {
    tal_symtab_t    *syms;
    const parse_ctx_t *parse;
    const tal_lex_t   *lex;
} sema_ctx_t;

/* ---- Symbol Registration ---- */

static tal_sym_t *sym_add(sema_ctx_t *C, const char *name,
                          uint8_t kind, uint8_t type,
                          const tal_tok_t *tok, uint32_t tok_idx)
{
    if (C->syms->n_syms >= TAL_MAX_SYMS) return NULL;

    tal_sym_t *s = &C->syms->syms[C->syms->n_syms++];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, TAL_MAX_IDENT, "%s", name);
    s->kind     = kind;
    s->type     = type;
    s->scope    = C->syms->scope_depth;
    s->def_line = tok->line;
    s->def_col  = tok->col;
    s->def_tok  = tok_idx;
    return s;
}

/* Find a previously registered symbol by name. Used for
 * merging parameter types -- TAL declares params in the
 * PROC header then gives them types on separate lines,
 * and we need to stitch those together. */
static tal_sym_t *sym_find_mut(sema_ctx_t *C, const char *name)
{
    for (uint32_t i = C->syms->n_syms; i > 0; i--) {
        tal_sym_t *s = &C->syms->syms[i - 1];
#ifdef _WIN32
        if (_stricmp(s->name, name) == 0)
#else
        if (strcasecmp(s->name, name) == 0)
#endif
            return s;
    }
    return NULL;
}

static const char *tok_text(sema_ctx_t *C, uint32_t tok_idx)
{
    if (tok_idx >= C->parse->n_toks) return "";
    return tal_str(C->lex, C->parse->toks[tok_idx].off);
}

static uint8_t type_from_tok(uint16_t kind)
{
    switch (kind) {
    case TK_STRING:   return TT_STRING;
    case TK_INT:      return TT_INT;
    case TK_INT32:    return TT_INT32;
    case TK_FIXED:    return TT_FIXED;
    case TK_REAL:     return TT_REAL;
    case TK_REAL64:   return TT_REAL64;
    case TK_UNSIGNED: return TT_UNSIGNED;
    case TK_STRUCT:   return TT_STRUCT;
    default:          return TT_NONE;
    }
}

/* ---- AST Walk ---- */

static void walk_node(sema_ctx_t *C, uint32_t ni);

static void walk_children(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];
    for (int i = 0; i < n->n_kids; i++)
        walk_node(C, n->kids[i]);
}

static void walk_proc(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];
    uint8_t proc_kind = TS_PROC;

    if (n->tok < C->parse->n_toks &&
        C->parse->toks[n->tok].kind == TK_SUBPROC)
        proc_kind = TS_SUBPROC;

    if (n->n_kids > 0) {
        const pn_node_t *name_nd = &C->parse->nodes[n->kids[0]];
        if (name_nd->kind == PN_IDENT) {
            const char *name = tok_text(C, name_nd->tok);
            sym_add(C, name, proc_kind, TT_NONE,
                    &C->parse->toks[name_nd->tok], name_nd->tok);
        }
    }

    C->syms->scope_depth++;

    for (int i = 1; i < n->n_kids; i++) {
        const pn_node_t *kid = &C->parse->nodes[n->kids[i]];
        if (kid->kind == PN_PARAM) {
            const char *name = tok_text(C, kid->tok);
            sym_add(C, name, TS_PARAM, TT_NONE,
                    &C->parse->toks[kid->tok], kid->tok);
        } else {
            walk_node(C, n->kids[i]);
        }
    }

    if (C->syms->scope_depth > 0) C->syms->scope_depth--;
}

static void walk_decl(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];

    uint8_t type = TT_NONE;
    if (n->tok < C->parse->n_toks)
        type = type_from_tok(C->parse->toks[n->tok].kind);

    if (n->n_kids > 0) {
        const pn_node_t *name_nd = &C->parse->nodes[n->kids[0]];
        if (name_nd->kind == PN_IDENT) {
            const char *name = tok_text(C, name_nd->tok);

            /* TAL declares parameters in the PROC header without
             * types, then redeclares them with types on the next
             * lines. If we already have a param with this name in
             * the current scope, merge the type info instead of
             * creating a duplicate symbol. This is why hover for
             * a parameter actually shows its type. */
            tal_sym_t *existing = sym_find_mut(C, name);
            if (existing && existing->kind == TS_PARAM &&
                existing->scope == C->syms->scope_depth &&
                existing->type == TT_NONE) {
                existing->type = type;
                return;
            }

            sym_add(C, name, TS_VAR, type,
                    &C->parse->toks[name_nd->tok], name_nd->tok);
        }
    }
}

static void walk_literal(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];

    if (n->n_kids > 0) {
        const pn_node_t *name_nd = &C->parse->nodes[n->kids[0]];
        if (name_nd->kind == PN_IDENT) {
            const char *name = tok_text(C, name_nd->tok);
            sym_add(C, name, TS_LITERAL, TT_INT,
                    &C->parse->toks[name_nd->tok], name_nd->tok);
        }
    }
}

static void walk_define(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];

    /* the parser now captures the name as a child node
     * and stores the body start token index in flags */
    const char *name = NULL;
    uint32_t name_tok = 0;

    if (n->n_kids > 0) {
        const pn_node_t *name_nd = &C->parse->nodes[n->kids[0]];
        if (name_nd->kind == PN_IDENT) {
            name = tok_text(C, name_nd->tok);
            name_tok = name_nd->tok;
        }
    }

    /* fallback: scan tokens after DEFINE for the name */
    if (!name) {
        uint32_t tok_after = n->tok + 1;
        while (tok_after < C->parse->n_toks &&
               C->parse->toks[tok_after].kind == TK_COMMENT)
            tok_after++;
        if (tok_after < C->parse->n_toks &&
            C->parse->toks[tok_after].kind == TK_IDENT) {
            name = tal_str(C->lex, C->parse->toks[tok_after].off);
            name_tok = tok_after;
        }
    }

    if (!name) return;

    tal_sym_t *sym = sym_add(C, name, TS_DEFINE, TT_NONE,
                             &C->parse->toks[name_tok], name_tok);
    if (!sym) return;

    /* capture the DEFINE body text from the source.
     * The body starts at the token after '=' (stored in
     * node flags) and runs until '#' or ';'. We grab the
     * raw source span so hover can show the expansion
     * exactly as written. */
    uint16_t body_start_pos = n->flags;
    if (body_start_pos > 0 && body_start_pos < C->parse->n_toks) {
        const tal_tok_t *first = &C->parse->toks[body_start_pos];

        /* find the end: scan forward to # or ; */
        uint32_t end_pos = body_start_pos;
        while (end_pos < C->parse->n_toks) {
            uint16_t tk = C->parse->toks[end_pos].kind;
            if (tk == TK_HASH || tk == TK_SEMICOLON || tk == TK_EOF)
                break;
            end_pos++;
        }

        if (end_pos > body_start_pos) {
            const tal_tok_t *last = &C->parse->toks[end_pos - 1];

            /* use the string pool offsets to find the raw text.
             * The offsets point into the interned strings, but
             * we need the original source positions. Reconstruct
             * from the source text using token positions. */
            if (first->line > 0 && first->col > 0) {
                /* find source offset for first body token */
                const char *src = C->lex->src;
                uint32_t slen = C->lex->slen;
                uint32_t soff = 0;
                uint32_t cur_line = 1;

                /* walk to the right line */
                while (soff < slen && cur_line < first->line) {
                    if (src[soff] == '\n') cur_line++;
                    soff++;
                }
                /* walk to the right column */
                soff += first->col - 1;

                /* find end source offset */
                uint32_t eoff = 0;
                cur_line = 1;
                while (eoff < slen && cur_line < last->line) {
                    if (src[eoff] == '\n') cur_line++;
                    eoff++;
                }
                eoff += last->col - 1 + last->len;

                if (eoff > soff && eoff <= slen) {
                    /* trim trailing whitespace */
                    while (eoff > soff && (src[eoff - 1] == ' ' ||
                           src[eoff - 1] == '\t'))
                        eoff--;

                    uint32_t blen = eoff - soff;
                    if (blen > 0 && blen < 256) {
                        sym->body_off = tal_intern(
                            (tal_lex_t *)C->lex, src + soff, blen);
                        sym->body_len = blen;
                    }
                }
            }
        }
    }
}

static void walk_label(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];
    const char *name = tok_text(C, n->tok);
    if (name[0])
        sym_add(C, name, TS_LABEL, TT_NONE,
                &C->parse->toks[n->tok], n->tok);
}

static void walk_struct(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];

    uint32_t parent_idx = C->syms->n_syms;
    if (n->n_kids > 0) {
        const pn_node_t *name_nd = &C->parse->nodes[n->kids[0]];
        if (name_nd->kind == PN_IDENT) {
            const char *name = tok_text(C, name_nd->tok);
            sym_add(C, name, TS_STRUCT, TT_STRUCT,
                    &C->parse->toks[name_nd->tok], name_nd->tok);
        }
    }

    for (int i = 1; i < n->n_kids; i++) {
        const pn_node_t *kid = &C->parse->nodes[n->kids[i]];
        if (kid->kind == PN_FIELD || kid->kind == PN_DECL) {
            walk_decl(C, n->kids[i]);
            if (C->syms->n_syms > 0) {
                tal_sym_t *last = &C->syms->syms[C->syms->n_syms - 1];
                last->kind = TS_FIELD;
                last->parent = parent_idx;
            }
        } else {
            walk_node(C, n->kids[i]);
        }
    }
}

static void walk_node(sema_ctx_t *C, uint32_t ni)
{
    if (ni >= C->parse->n_nodes) return;
    const pn_node_t *n = &C->parse->nodes[ni];

    switch (n->kind) {
    case PN_PROC:        walk_proc(C, ni);     break;
    case PN_DECL:        walk_decl(C, ni);     break;
    case PN_LITERAL:     walk_literal(C, ni);  break;
    case PN_DEFINE:      walk_define(C, ni);   break;
    case PN_LABEL:       walk_label(C, ni);    break;
    case PN_STRUCT_DECL: walk_struct(C, ni);   break;
    case PN_BEGIN:       walk_children(C, ni); break;
    case PN_IF:          walk_children(C, ni); break;
    case PN_WHILE:       walk_children(C, ni); break;
    case PN_FOR:         walk_children(C, ni); break;
    case PN_CASE:        walk_children(C, ni); break;
    default: break;
    }
}

/* ---- Public API ---- */

void tal_sema(tal_symtab_t *S, const parse_ctx_t *P, const tal_lex_t *L)
{
    memset(S, 0, sizeof(*S));

    sema_ctx_t ctx;
    ctx.syms  = S;
    ctx.parse = P;
    ctx.lex   = L;

    if (P->n_nodes > 0)
        walk_node(&ctx, 0);
}

/* ---- Reference Counting ----
 *
 * Scan every identifier token and check if it matches a
 * known symbol. If so, bump the ref count. We skip the
 * token at the definition site itself -- that's a declaration,
 * not a use. The result lets us flag unused symbols, which
 * is the kind of warning that catches dead code before it
 * becomes folklore that nobody dares touch. */

void tal_sema_refs(tal_symtab_t *S, const tal_lex_t *L)
{
    /* reset counts */
    for (uint32_t i = 0; i < S->n_syms; i++)
        S->syms[i].ref_count = 0;

    for (uint32_t t = 0; t < L->n_toks; t++) {
        if (L->toks[t].kind != TK_IDENT) continue;

        const char *name = tal_str(L, L->toks[t].off);

        /* find matching symbol, searching backwards for
         * innermost scope match (most recent declaration) */
        for (uint32_t i = S->n_syms; i > 0; i--) {
            tal_sym_t *s = &S->syms[i - 1];
#ifdef _WIN32
            if (_stricmp(s->name, name) != 0) continue;
#else
            if (strcasecmp(s->name, name) != 0) continue;
#endif
            /* don't count the definition site as a reference */
            if (s->def_tok == t) break;

            s->ref_count++;
            break;
        }
    }
}

const tal_sym_t *tal_sym_lookup(const tal_symtab_t *S, const char *name)
{
    /* search backwards for innermost scope match */
    for (uint32_t i = S->n_syms; i > 0; i--) {
#ifdef _WIN32
        if (_stricmp(S->syms[i - 1].name, name) == 0)
#else
        if (strcasecmp(S->syms[i - 1].name, name) == 0)
#endif
            return &S->syms[i - 1];
    }
    return NULL;
}

const tal_sym_t *tal_sym_at(const tal_symtab_t *S,
                            const tal_lex_t *L, const parse_ctx_t *P,
                            uint32_t line, uint32_t col)
{
    (void)P;
    for (uint32_t i = 0; i < L->n_toks; i++) {
        const tal_tok_t *t = &L->toks[i];
        if (t->kind != TK_IDENT) continue;
        if (t->line != line) continue;
        if (col >= t->col && col < t->col + t->len) {
            const char *name = tal_str(L, t->off);
            return tal_sym_lookup(S, name);
        }
    }
    return NULL;
}

const char *tal_define_body(const tal_sym_t *sym, const tal_lex_t *L)
{
    if (!sym || sym->body_len == 0) return "";
    return tal_str(L, sym->body_off);
}
