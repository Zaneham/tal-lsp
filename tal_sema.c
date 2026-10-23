/* tal_sema.c -- TAL semantic pass. Walks the AST, builds the symbol table,
 * counts references and flags unused symbols.
 *
 * (c) 2026 ZH. Apache 2.0.
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
    s->end_tok  = UINT32_MAX;
    return s;
}

static int nameq(const char *a, const char *b)
{
    for (int i = 0; i < TAL_MAX_IDENT; i++) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return 0;
        if (!a[i]) return 1;
    }
    return 1;
}

static tal_sym_t *sym_find_mut(sema_ctx_t *C, const char *name)
{
    for (uint32_t i = C->syms->n_syms; i > 0; i--) {
        tal_sym_t *s = &C->syms->syms[i - 1];
        if (nameq(s->name, name)) return s;
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
    for (uint32_t k = n->first; k; k = C->parse->nodes[k].next)
        walk_node(C, k);
}

static void walk_proc(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];
    uint8_t proc_kind = TS_PROC;

    if (n->tok < C->parse->n_toks &&
        C->parse->toks[n->tok].kind == TK_SUBPROC)
        proc_kind = TS_SUBPROC;

    if (n->first) {
        const pn_node_t *name_nd = &C->parse->nodes[n->first];
        if (name_nd->kind == PN_IDENT) {
            const char *name = tok_text(C, name_nd->tok);
            sym_add(C, name, proc_kind, TT_NONE,
                    &C->parse->toks[name_nd->tok], name_nd->tok);
        }
    }

    C->syms->scope_depth++;
    uint32_t base = C->syms->n_syms;

    for (uint32_t k = n->first ? C->parse->nodes[n->first].next : 0; k;
         k = C->parse->nodes[k].next) {
        const pn_node_t *kid = &C->parse->nodes[k];
        if (kid->kind == PN_PARAM) {
            const char *name = tok_text(C, kid->tok);
            sym_add(C, name, TS_PARAM, TT_NONE,
                    &C->parse->toks[kid->tok], kid->tok);
        } else {
            walk_node(C, k);
        }
    }

    for (uint32_t i = base; i < C->syms->n_syms; i++) {
        if (C->syms->syms[i].end_tok == UINT32_MAX)
            C->syms->syms[i].end_tok = n->etok;
    }

    if (C->syms->scope_depth > 0) C->syms->scope_depth--;
}

static void walk_decl(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];

    uint8_t type = TT_NONE;
    if (n->tok < C->parse->n_toks)
        type = type_from_tok(C->parse->toks[n->tok].kind);

    for (uint32_t k = n->first; k; k = C->parse->nodes[k].next) {
        const pn_node_t *name_nd = &C->parse->nodes[k];
        if (name_nd->kind != PN_IDENT) continue;
        const char *name = tok_text(C, name_nd->tok);

        tal_sym_t *existing = sym_find_mut(C, name);
        if (existing && existing->kind == TS_PARAM &&
            existing->scope == C->syms->scope_depth &&
            existing->type == TT_NONE) {
            existing->type = type;
            continue;
        }

        sym_add(C, name, TS_VAR, type,
                &C->parse->toks[name_nd->tok], name_nd->tok);
    }
}

static void walk_literal(sema_ctx_t *C, uint32_t ni)
{
    const pn_node_t *n = &C->parse->nodes[ni];

    if (n->first) {
        const pn_node_t *name_nd = &C->parse->nodes[n->first];
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

    const char *name = NULL;
    uint32_t name_tok = 0;

    if (n->first) {
        const pn_node_t *name_nd = &C->parse->nodes[n->first];
        if (name_nd->kind == PN_IDENT) {
            name = tok_text(C, name_nd->tok);
            name_tok = name_nd->tok;
        }
    }

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

    uint16_t body_start_pos = n->flags;
    if (body_start_pos > 0 && body_start_pos < C->parse->n_toks) {
        const tal_tok_t *first = &C->parse->toks[body_start_pos];

        uint32_t end_pos = body_start_pos;
        while (end_pos < C->parse->n_toks) {
            uint16_t tk = C->parse->toks[end_pos].kind;
            if (tk == TK_HASH || tk == TK_SEMICOLON || tk == TK_EOF)
                break;
            end_pos++;
        }

        if (end_pos > body_start_pos) {
            const tal_tok_t *last = &C->parse->toks[end_pos - 1];

            if (first->line > 0 && first->col > 0) {

                const char *src = C->lex->src;
                uint32_t slen = C->lex->slen;
                uint32_t soff = 0;
                uint32_t cur_line = 1;

                while (soff < slen && cur_line < first->line) {
                    if (src[soff] == '\n') cur_line++;
                    soff++;
                }

                soff += first->col - 1;

                uint32_t eoff = 0;
                cur_line = 1;
                while (eoff < slen && cur_line < last->line) {
                    if (src[eoff] == '\n') cur_line++;
                    eoff++;
                }
                eoff += last->col - 1 + last->len;

                if (eoff > soff && eoff <= slen) {

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
    if (n->first) {
        const pn_node_t *name_nd = &C->parse->nodes[n->first];
        if (name_nd->kind == PN_IDENT) {
            const char *name = tok_text(C, name_nd->tok);
            sym_add(C, name, TS_STRUCT, TT_STRUCT,
                    &C->parse->toks[name_nd->tok], name_nd->tok);
        }
    }

    for (uint32_t k = n->first ? C->parse->nodes[n->first].next : 0; k;
         k = C->parse->nodes[k].next) {
        const pn_node_t *kid = &C->parse->nodes[k];
        if (kid->kind == PN_FIELD || kid->kind == PN_DECL) {
            walk_decl(C, k);
            if (C->syms->n_syms > 0) {
                tal_sym_t *last = &C->syms->syms[C->syms->n_syms - 1];
                last->kind = TS_FIELD;
                last->parent = parent_idx;
            }
        } else {
            walk_node(C, k);
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

    if (P->n_nodes > 1)
        walk_node(&ctx, 1);
}

/* ---- Reference Counting ---- */

void tal_sema_refs(tal_symtab_t *S, const tal_lex_t *L)
{

    for (uint32_t i = 0; i < S->n_syms; i++)
        S->syms[i].ref_count = 0;

    for (uint32_t t = 0; t < L->n_toks; t++) {
        if (L->toks[t].kind != TK_IDENT) continue;

        const char *name = tal_str(L, L->toks[t].off);
        const tal_sym_t *s = tal_sym_use(S, name, t);
        if (s && s->def_tok != t)
            S->syms[s - S->syms].ref_count++;
    }
}

const tal_sym_t *tal_sym_use(const tal_symtab_t *S, const char *name, uint32_t tok)
{
    const tal_sym_t *best = NULL;
    for (uint32_t i = 0; i < S->n_syms; i++) {
        const tal_sym_t *s = &S->syms[i];
        if (!nameq(s->name, name)) continue;
        if (s->def_tok > tok || tok >= s->end_tok) continue;
        if (!best || s->def_tok >= best->def_tok) best = s;
    }
    return best ? best : tal_sym_lookup(S, name);
}

const tal_sym_t *tal_sym_lookup(const tal_symtab_t *S, const char *name)
{

    for (uint32_t i = S->n_syms; i > 0; i--) {
        if (nameq(S->syms[i - 1].name, name)) return &S->syms[i - 1];
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
            return tal_sym_use(S, name, i);
        }
    }
    return NULL;
}

const char *tal_define_body(const tal_sym_t *sym, const tal_lex_t *L)
{
    if (!sym || sym->body_len == 0) return "";
    return tal_str(L, sym->body_off);
}
