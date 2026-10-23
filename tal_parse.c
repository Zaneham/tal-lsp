/* tal_parse.c -- TAL recursive descent parser, loose on purpose so
 * half-typed code still gets an AST.
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#include "tal.h"
#include <ctype.h>
#include <stdio.h>

/* ---- Token Names ---- */

const char *tal_tk_name(uint16_t k)
{
    switch (k) {
    case TK_INT_LIT:    return "integer literal";
    case TK_REAL_LIT:   return "real literal";
    case TK_STRING_LIT: return "string literal";
    case TK_IDENT:      return "identifier";
    case TK_PLUS:       return "'+'";
    case TK_MINUS:      return "'-'";
    case TK_STAR:       return "'*'";
    case TK_SLASH:      return "'/'";
    case TK_LSHIFT:     return "'<<'";
    case TK_RSHIFT:     return "'>>'";
    case TK_EQ:         return "'='";
    case TK_NEQ:        return "'<>'";
    case TK_LT:         return "'<'";
    case TK_GT:         return "'>'";
    case TK_LE:         return "'<='";
    case TK_GE:         return "'>='";
    case TK_AND:        return "AND";
    case TK_OR:         return "OR";
    case TK_NOT:        return "NOT";
    case TK_LAND:       return "LAND";
    case TK_LOR:        return "LOR";
    case TK_XOR:        return "XOR";
    case TK_ASSIGN:     return "':='";
    case TK_LPAREN:     return "'('";
    case TK_RPAREN:     return "')'";
    case TK_COMMA:      return "','";
    case TK_SEMICOLON:  return "';'";
    case TK_COLON:      return "':'";
    case TK_DOT:        return "'.'";
    case TK_AT:         return "'@'";
    case TK_STRING:     return "STRING";
    case TK_INT:        return "INT";
    case TK_INT32:      return "INT(32)";
    case TK_FIXED:      return "FIXED";
    case TK_REAL:       return "REAL";
    case TK_REAL64:     return "REAL(64)";
    case TK_UNSIGNED:   return "UNSIGNED";
    case TK_LITERAL:    return "LITERAL";
    case TK_DEFINE:     return "DEFINE";
    case TK_STRUCT:     return "STRUCT";
    case TK_BEGIN:      return "BEGIN";
    case TK_END:        return "END";
    case TK_PROC:       return "PROC";
    case TK_SUBPROC:    return "SUBPROC";
    case TK_MAIN:       return "MAIN";
    case TK_IF:         return "IF";
    case TK_THEN:       return "THEN";
    case TK_ELSE:       return "ELSE";
    case TK_WHILE:      return "WHILE";
    case TK_DO:         return "DO";
    case TK_FOR:        return "FOR";
    case TK_TO:         return "TO";
    case TK_DOWNTO:     return "DOWNTO";
    case TK_BY:         return "BY";
    case TK_CASE:       return "CASE";
    case TK_OF:         return "OF";
    case TK_OTHERWISE:  return "OTHERWISE";
    case TK_CALL:       return "CALL";
    case TK_RETURN:     return "RETURN";
    case TK_GOTO:       return "GOTO";
    case TK_MOVE:       return "MOVE";
    case TK_SCAN:       return "SCAN";
    case TK_DOLLAR:     return "'$'";
    case TK_HASH:       return "'#'";
    case TK_SOURCE:     return "?SOURCE";
    case TK_DIRECTIVE:  return "directive";
    case TK_COMMENT:    return "comment";
    case TK_EOF:        return "end of file";
    default:            return "token";
    }
}

/* ---- Helpers ---- */

static const tal_tok_t *cur(parse_ctx_t *P)
{
    if (P->pos >= P->n_toks) return &P->toks[P->n_toks - 1];
    return &P->toks[P->pos];
}

static uint16_t peek_kind(parse_ctx_t *P)
{
    return cur(P)->kind;
}

static const tal_tok_t *eat(parse_ctx_t *P)
{
    const tal_tok_t *t = cur(P);
    if (P->pos < P->n_toks) P->pos++;

    while (P->pos < P->n_toks && P->toks[P->pos].kind == TK_COMMENT)
        P->pos++;
    return t;
}

static int at(parse_ctx_t *P, uint16_t kind)
{
    return peek_kind(P) == kind;
}

static const tal_tok_t *expect(parse_ctx_t *P, uint16_t kind)
{
    if (at(P, kind)) return eat(P);

    if (P->n_diags < TAL_MAX_DIAGS) {
        tal_diag_t *d = &P->diags[P->n_diags++];
        const tal_tok_t *t = cur(P);
        d->line = t->line;
        d->col  = t->col;
        d->len  = t->len > 0 ? t->len : 1;
        d->severity = 1;
        const char *got_text = tal_str(P->lex, t->off);
        if (got_text[0])
            snprintf(d->msg, sizeof(d->msg),
                     "expected %s, got '%s'", tal_tk_name(kind), got_text);
        else
            snprintf(d->msg, sizeof(d->msg),
                     "expected %s, got %s", tal_tk_name(kind), tal_tk_name(t->kind));
    }
    return NULL;
}

static void skip_to_semi(parse_ctx_t *P)
{
    while (!at(P, TK_SEMICOLON) && !at(P, TK_EOF))
        eat(P);
    if (at(P, TK_SEMICOLON)) eat(P);
}

static uint32_t mk_node(parse_ctx_t *P, uint8_t kind, uint32_t tok)
{
    if (P->n_nodes >= PN_MAX_NODES) return 0;
    uint32_t ni = P->n_nodes++;
    pn_node_t *n = &P->nodes[ni];
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->tok  = tok;
    return ni;
}

static void add_kid(parse_ctx_t *P, uint32_t parent, uint32_t child)
{
    pn_node_t *n = &P->nodes[parent];
    if (!parent || !child || child == n->last || P->nodes[child].next) return;
    if (n->last) P->nodes[n->last].next = child;
    else n->first = child;
    n->last = child;
    n->n_kids++;
}

/* ---- Forward Declarations ---- */

static uint32_t parse_expr(parse_ctx_t *P);
static uint32_t parse_stmt(parse_ctx_t *P);
static uint32_t parse_block(parse_ctx_t *P);
static uint32_t postfix(parse_ctx_t *P, uint32_t nd, const tal_tok_t *t);

/* ---- Expression Parser ---- */

static int prec_of(uint16_t kind)
{
    switch (kind) {
    case TK_OR: case TK_LOR: case TK_XOR:    return 1;
    case TK_AND: case TK_LAND:               return 2;
    case TK_EQ: case TK_NEQ:
    case TK_LT: case TK_GT:
    case TK_LE: case TK_GE:                  return 3;
    case TK_LSHIFT: case TK_RSHIFT:
    case TK_LSHIFT_UNSIGNED:
    case TK_RSHIFT_UNSIGNED:                  return 4;
    case TK_PLUS: case TK_MINUS:
    case TK_PLUS_UNSIGNED:
    case TK_MINUS_UNSIGNED:                   return 5;
    case TK_STAR: case TK_SLASH:
    case TK_STAR_UNSIGNED:
    case TK_SLASH_UNSIGNED:
    case TK_MOD_UNSIGNED:                     return 6;
    default:                                  return -1;
    }
}

static uint32_t parse_primary(parse_ctx_t *P)
{
    uint16_t k = peek_kind(P);

    if (k == TK_NOT || k == TK_MINUS || k == TK_AT) {
        const tal_tok_t *op = eat(P);
        uint32_t operand = parse_primary(P);
        uint32_t nd = mk_node(P, PN_UNOP, (uint32_t)(op - P->toks));
        add_kid(P, nd, operand);
        return nd;
    }

    if (k == TK_LPAREN) {
        eat(P);
        uint32_t expr = parse_expr(P);
        expect(P, TK_RPAREN);
        return expr;
    }

    if (k == TK_INT_LIT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_INTLIT, (uint32_t)(t - P->toks));
        return nd;
    }

    if (k == TK_REAL_LIT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_REALLIT, (uint32_t)(t - P->toks));
        return nd;
    }

    if (k == TK_STRING_LIT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_STRLIT, (uint32_t)(t - P->toks));
        return nd;
    }

    if (k == TK_DOLLAR || k == TK_DOLLAR_CARRY || k == TK_DOLLAR_OVERFLOW) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_STDFUNC, (uint32_t)(t - P->toks));
        if (at(P, TK_LPAREN)) {
            eat(P);
            while (!at(P, TK_RPAREN) && !at(P, TK_EOF)) {
                add_kid(P, nd, parse_expr(P));
                if (at(P, TK_COMMA)) eat(P);
                else break;
            }
            expect(P, TK_RPAREN);
        }
        return nd;
    }

    if (k == TK_BASE) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_IDENT, (uint32_t)(t - P->toks));
        if (at(P, TK_LBRACKET)) {
            eat(P);
            add_kid(P, nd, parse_expr(P));
            expect(P, TK_RBRACKET);
        }
        return nd;
    }

    if (k == TK_IDENT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_IDENT, (uint32_t)(t - P->toks));
        return postfix(P, nd, t);
    }

    const tal_tok_t *t = eat(P);
    return mk_node(P, PN_EMPTY, (uint32_t)(t - P->toks));
}

static uint32_t postfix(parse_ctx_t *P, uint32_t nd, const tal_tok_t *t)
{
    for (int n = 0; n < 32; n++) {
        if (at(P, TK_LBRACKET)) {
            uint32_t arr = mk_node(P, PN_ARRREF, (uint32_t)(t - P->toks));
            add_kid(P, arr, nd);
            eat(P);
            add_kid(P, arr, parse_expr(P));
            expect(P, TK_RBRACKET);
            nd = arr;
        } else if (at(P, TK_LPAREN)) {
            uint32_t call = mk_node(P, PN_FUNCCALL, (uint32_t)(t - P->toks));
            add_kid(P, call, nd);
            eat(P);
            for (int a = 0; a < 32 && !at(P, TK_RPAREN) && !at(P, TK_EOF); a++) {
                if (!at(P, TK_COMMA)) add_kid(P, call, parse_expr(P));
                if (at(P, TK_COLON)) { eat(P); (void)parse_expr(P); }
                if (at(P, TK_COMMA)) eat(P);
                else break;
            }
            expect(P, TK_RPAREN);
            nd = call;
        } else if (at(P, TK_DOT)) {
            eat(P);
            if (at(P, TK_LT)) {
                for (int b = 0; b < 8 && !at(P, TK_GT) && !at(P, TK_EOF); b++) eat(P);
                expect(P, TK_GT);
            } else if (at(P, TK_IDENT)) {
                const tal_tok_t *member = eat(P);
                uint32_t dot = mk_node(P, PN_DOTREF, (uint32_t)(member - P->toks));
                add_kid(P, dot, nd);
                add_kid(P, dot, mk_node(P, PN_IDENT, (uint32_t)(member - P->toks)));
                nd = dot;
            }
        } else {
            break;
        }
    }
    return nd;
}

static uint32_t parse_expr_prec(parse_ctx_t *P, int min_prec)
{
    uint32_t left = parse_primary(P);

    while (1) {
        int p = prec_of(peek_kind(P));
        if (p < min_prec) break;

        const tal_tok_t *op = eat(P);
        uint32_t right = parse_expr_prec(P, p + 1);

        uint32_t bin = mk_node(P, PN_BINOP, (uint32_t)(op - P->toks));
        add_kid(P, bin, left);
        add_kid(P, bin, right);
        left = bin;
    }
    return left;
}

static uint32_t parse_expr(parse_ctx_t *P)
{
    return parse_expr_prec(P, 1);
}

/* ---- Statement Parser ---- */

static int isword(parse_ctx_t *P, const char *w)
{
    if (!at(P, TK_IDENT)) return 0;
    const char *s = tal_str(P->lex, cur(P)->off);
    for (int i = 0; i < TAL_MAX_IDENT; i++) {
        if (toupper((unsigned char)s[i]) != w[i]) return 0;
        if (!w[i]) return 1;
    }
    return 0;
}

static void skip_to(parse_ctx_t *P, uint16_t close)
{
    eat(P);
    for (int n = 0; n < 256 && !at(P, close) && !at(P, TK_EOF); n++) eat(P);
    if (at(P, close)) eat(P);
}

static void decl_one(parse_ctx_t *P, uint32_t decl)
{
    if (at(P, TK_DOT)) {
        eat(P);
        if (isword(P, "EXT") || isword(P, "SG")) eat(P);
    }
    if (!at(P, TK_IDENT)) return;
    const tal_tok_t *name = eat(P);
    add_kid(P, decl, mk_node(P, PN_IDENT, (uint32_t)(name - P->toks)));
    if (at(P, TK_LBRACKET)) skip_to(P, TK_RBRACKET);
    if (at(P, TK_LPAREN)) skip_to(P, TK_RPAREN);
    if (at(P, TK_EQ)) {
        eat(P);
        if (at(P, TK_BASE) || at(P, TK_IDENT)) eat(P);
        if (at(P, TK_LBRACKET)) skip_to(P, TK_RBRACKET);
    }
    if (at(P, TK_ASSIGN)) {
        eat(P);
        if (at(P, TK_LBRACKET)) skip_to(P, TK_RBRACKET);
        else (void)parse_expr(P);
    }
}

static uint32_t parse_decl(parse_ctx_t *P)
{
    const tal_tok_t *type_tok = eat(P);
    uint32_t decl = mk_node(P, PN_DECL, (uint32_t)(type_tok - P->toks));
    if (at(P, TK_LPAREN)) skip_to(P, TK_RPAREN);
    for (int n = 0; n < 1024; n++) {
        decl_one(P, decl);
        if (!at(P, TK_COMMA)) break;
        eat(P);
    }
    return decl;
}

static uint32_t parse_if(parse_ctx_t *P)
{
    const tal_tok_t *ift = eat(P);
    uint32_t nd = mk_node(P, PN_IF, (uint32_t)(ift - P->toks));

    add_kid(P, nd, parse_expr(P));
    expect(P, TK_THEN);
    add_kid(P, nd, parse_stmt(P));

    if (at(P, TK_ELSE)) {
        eat(P);
        add_kid(P, nd, parse_stmt(P));
    }
    return nd;
}

static uint32_t parse_while(parse_ctx_t *P)
{
    const tal_tok_t *wt = eat(P);
    uint32_t nd = mk_node(P, PN_WHILE, (uint32_t)(wt - P->toks));

    add_kid(P, nd, parse_expr(P));
    expect(P, TK_DO);
    add_kid(P, nd, parse_stmt(P));
    return nd;
}

static uint32_t parse_for(parse_ctx_t *P)
{
    const tal_tok_t *ft = eat(P);
    uint32_t nd = mk_node(P, PN_FOR, (uint32_t)(ft - P->toks));

    add_kid(P, nd, parse_expr(P));
    if (at(P, TK_ASSIGN)) {
        eat(P);
        add_kid(P, nd, parse_expr(P));
    }
    if (at(P, TK_TO) || at(P, TK_DOWNTO)) eat(P);
    add_kid(P, nd, parse_expr(P));
    if (at(P, TK_BY)) {
        eat(P);
        add_kid(P, nd, parse_expr(P));
    }
    expect(P, TK_DO);
    add_kid(P, nd, parse_stmt(P));
    return nd;
}

static void case_label(parse_ctx_t *P)
{
    uint32_t k = P->pos;
    for (int n = 0; n < 32 && k < P->n_toks; n++, k++) {
        uint16_t t = P->toks[k].kind;
        if (t == TK_SEMICOLON || t == TK_BEGIN || t == TK_END ||
            t == TK_ASSIGN || t == TK_EOF)
            return;
        if (t == TK_ARROW) {
            for (uint32_t i = P->pos; i <= k; i++) eat(P);
            return;
        }
    }
}

static uint32_t parse_case(parse_ctx_t *P)
{
    const tal_tok_t *ct = eat(P);
    uint32_t nd = mk_node(P, PN_CASE, (uint32_t)(ct - P->toks));

    add_kid(P, nd, parse_expr(P));
    expect(P, TK_OF);
    expect(P, TK_BEGIN);

    while (!at(P, TK_END) && !at(P, TK_EOF)) {
        if (at(P, TK_OTHERWISE)) {
            eat(P);
            if (at(P, TK_ARROW)) eat(P);
            add_kid(P, nd, parse_stmt(P));
            if (at(P, TK_SEMICOLON)) eat(P);
            break;
        }
        case_label(P);
        add_kid(P, nd, parse_stmt(P));
        if (at(P, TK_SEMICOLON)) eat(P);
    }

    expect(P, TK_END);
    return nd;
}

static uint32_t parse_call(parse_ctx_t *P)
{
    const tal_tok_t *ct = eat(P);
    uint32_t nd = mk_node(P, PN_CALL, (uint32_t)(ct - P->toks));

    if (at(P, TK_IDENT)) {
        const tal_tok_t *name = eat(P);
        uint32_t name_nd = mk_node(P, PN_IDENT, (uint32_t)(name - P->toks));
        add_kid(P, nd, name_nd);
    }

    if (at(P, TK_LPAREN)) {
        eat(P);
        for (int a = 0; a < 64 && !at(P, TK_RPAREN) && !at(P, TK_EOF); a++) {
            if (!at(P, TK_COMMA)) add_kid(P, nd, parse_expr(P));
            if (at(P, TK_COLON)) { eat(P); (void)parse_expr(P); }
            if (at(P, TK_COMMA)) eat(P);
            else break;
        }
        expect(P, TK_RPAREN);
    }
    return nd;
}

static uint32_t parse_block(parse_ctx_t *P)
{
    const tal_tok_t *bt = eat(P);
    uint32_t nd = mk_node(P, PN_BEGIN, (uint32_t)(bt - P->toks));

    while (!at(P, TK_END) && !at(P, TK_EOF)) {
        add_kid(P, nd, parse_stmt(P));
        if (at(P, TK_SEMICOLON)) eat(P);
    }

    expect(P, TK_END);
    return nd;
}

static int is_type_keyword(uint16_t kind)
{
    return kind == TK_STRING || kind == TK_INT || kind == TK_INT32 ||
           kind == TK_FIXED || kind == TK_REAL || kind == TK_REAL64 ||
           kind == TK_UNSIGNED || kind == TK_STRUCT;
}

static uint32_t parse_stmt(parse_ctx_t *P)
{
    uint16_t k = peek_kind(P);

    if (is_type_keyword(k)) return parse_decl(P);

    if (k == TK_BEGIN) return parse_block(P);

    if (k == TK_IF)    return parse_if(P);
    if (k == TK_WHILE) return parse_while(P);
    if (k == TK_FOR)   return parse_for(P);
    if (k == TK_CASE)  return parse_case(P);
    if (k == TK_CALL)  return parse_call(P);

    if (k == TK_RETURN) {
        const tal_tok_t *rt = eat(P);
        uint32_t nd = mk_node(P, PN_RETURN, (uint32_t)(rt - P->toks));
        if (!at(P, TK_SEMICOLON) && !at(P, TK_EOF))
            add_kid(P, nd, parse_expr(P));
        return nd;
    }

    if (k == TK_GOTO) {
        const tal_tok_t *gt = eat(P);
        uint32_t nd = mk_node(P, PN_GOTO, (uint32_t)(gt - P->toks));
        if (at(P, TK_IDENT)) {
            const tal_tok_t *lbl = eat(P);
            add_kid(P, nd, mk_node(P, PN_IDENT, (uint32_t)(lbl - P->toks)));
        }
        return nd;
    }

    if (k == TK_ASSERT) {
        const tal_tok_t *at_tok = eat(P);
        uint32_t nd = mk_node(P, PN_ASSERT, (uint32_t)(at_tok - P->toks));
        add_kid(P, nd, parse_expr(P));
        return nd;
    }

    if (k == TK_MOVE || k == TK_SCAN || k == TK_RSCAN) {
        const tal_tok_t *mt = eat(P);
        uint32_t nd = mk_node(P, k == TK_MOVE ? PN_MOVE : PN_SCAN,
                              (uint32_t)(mt - P->toks));

        while (!at(P, TK_SEMICOLON) && !at(P, TK_EOF))
            eat(P);
        return nd;
    }

    if (k == TK_CODE) {
        const tal_tok_t *ct = eat(P);
        uint32_t nd = mk_node(P, PN_CODE, (uint32_t)(ct - P->toks));
        expect(P, TK_LPAREN);
        while (!at(P, TK_RPAREN) && !at(P, TK_EOF))
            eat(P);
        expect(P, TK_RPAREN);
        return nd;
    }

    if (k == TK_STORE) {
        const tal_tok_t *st = eat(P);
        uint32_t nd = mk_node(P, PN_STORE, (uint32_t)(st - P->toks));
        while (!at(P, TK_SEMICOLON) && !at(P, TK_EOF))
            eat(P);
        return nd;
    }

    if (k == TK_USE) {
        const tal_tok_t *ut = eat(P);
        uint32_t nd = mk_node(P, PN_USE, (uint32_t)(ut - P->toks));
        if (at(P, TK_IDENT)) eat(P);
        return nd;
    }

    if (k == TK_DROP) {
        const tal_tok_t *dt = eat(P);
        uint32_t nd = mk_node(P, PN_DROP, (uint32_t)(dt - P->toks));
        if (at(P, TK_IDENT)) eat(P);
        return nd;
    }

    if (k == TK_LITERAL) {
        const tal_tok_t *lt = eat(P);
        uint32_t nd = mk_node(P, PN_LITERAL, (uint32_t)(lt - P->toks));
        if (at(P, TK_IDENT)) {
            const tal_tok_t *name = eat(P);
            add_kid(P, nd, mk_node(P, PN_IDENT, (uint32_t)(name - P->toks)));
        }
        if (at(P, TK_EQ)) {
            eat(P);
            add_kid(P, nd, parse_expr(P));
        }
        return nd;
    }

    if (k == TK_DEFINE) {
        const tal_tok_t *dt = eat(P);
        uint32_t nd = mk_node(P, PN_DEFINE, (uint32_t)(dt - P->toks));

        if (at(P, TK_IDENT)) {
            const tal_tok_t *name = eat(P);
            add_kid(P, nd, mk_node(P, PN_IDENT, (uint32_t)(name - P->toks)));
        }

        if (at(P, TK_LPAREN)) {
            eat(P);
            while (!at(P, TK_RPAREN) && !at(P, TK_EOF))
                eat(P);
            if (at(P, TK_RPAREN)) eat(P);
        }

        if (at(P, TK_EQ)) eat(P);

        P->nodes[nd].flags = (uint16_t)P->pos;

        while (!at(P, TK_HASH) && !at(P, TK_SEMICOLON) && !at(P, TK_EOF))
            eat(P);
        if (at(P, TK_HASH)) eat(P);

        return nd;
    }

    if (k == TK_IDENT) {
        uint32_t lhs = parse_expr(P);

        if (at(P, TK_ASSIGN)) {
            const tal_tok_t *eq = eat(P);
            uint32_t rhs = parse_expr(P);
            uint32_t nd = mk_node(P, PN_ASSIGN, (uint32_t)(eq - P->toks));
            add_kid(P, nd, lhs);
            add_kid(P, nd, rhs);
            return nd;
        }

        if (at(P, TK_COLON)) {
            eat(P);
            uint32_t nd = mk_node(P, PN_LABEL, P->nodes[lhs].tok);
            return nd;
        }

        return lhs;
    }

    skip_to_semi(P);
    return mk_node(P, PN_EMPTY, (uint32_t)(cur(P) - P->toks));
}

/* ---- Procedure Parser ---- */

static uint32_t parse_proc(parse_ctx_t *P)
{
    const tal_tok_t *pt = eat(P);
    uint32_t nd = mk_node(P, PN_PROC, (uint32_t)(pt - P->toks));

    if (at(P, TK_IDENT)) {
        const tal_tok_t *name = eat(P);
        uint32_t name_nd = mk_node(P, PN_IDENT, (uint32_t)(name - P->toks));
        add_kid(P, nd, name_nd);
    }

    if (at(P, TK_LPAREN)) {
        eat(P);
        while (!at(P, TK_RPAREN) && !at(P, TK_EOF)) {
            if (at(P, TK_IDENT)) {
                const tal_tok_t *param = eat(P);
                uint32_t pnd = mk_node(P, PN_PARAM, (uint32_t)(param - P->toks));
                add_kid(P, nd, pnd);
            }
            if (at(P, TK_COMMA)) eat(P);
            else break;
        }
        expect(P, TK_RPAREN);
    }

    while (at(P, TK_MAIN) || at(P, TK_INTERRUPT) || at(P, TK_RESIDENT) ||
           at(P, TK_CALLABLE) || at(P, TK_PRIV) || at(P, TK_VARIABLE) ||
           at(P, TK_EXTENSIBLE) || at(P, TK_FORWARD) || at(P, TK_EXTERNAL) ||
           at(P, TK_LANGUAGE)) {
        eat(P);

        if (P->toks[P->pos - 1].kind == TK_LANGUAGE && at(P, TK_IDENT))
            eat(P);
    }

    expect(P, TK_SEMICOLON);

    while (is_type_keyword(peek_kind(P))) {
        add_kid(P, nd, parse_decl(P));
        if (at(P, TK_SEMICOLON)) eat(P);
    }

    if (at(P, TK_BEGIN)) {
        add_kid(P, nd, parse_block(P));
    }

    if (at(P, TK_SEMICOLON)) eat(P);
    P->nodes[nd].etok = P->pos;
    return nd;
}

/* ---- Top Level Parser ---- */

uint32_t tal_parse(parse_ctx_t *P, const tal_tok_t *toks, uint32_t n_toks,
                   const tal_lex_t *lex)
{
    memset(P, 0, sizeof(*P));
    P->toks   = toks;
    P->n_toks = n_toks;
    P->lex    = lex;

    while (P->pos < P->n_toks && P->toks[P->pos].kind == TK_COMMENT)
        P->pos++;

    (void)mk_node(P, PN_EMPTY, 0);
    uint32_t root = mk_node(P, PN_BEGIN, 0);

    while (!at(P, TK_EOF)) {
        if (at(P, TK_PROC) || at(P, TK_SUBPROC)) {
            add_kid(P, root, parse_proc(P));
        } else if (is_type_keyword(peek_kind(P))) {

            add_kid(P, root, parse_decl(P));
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_LITERAL)) {
            add_kid(P, root, parse_stmt(P));
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_DEFINE)) {
            add_kid(P, root, parse_stmt(P));
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_NAME) || at(P, TK_BLOCK)) {

            eat(P);
            if (at(P, TK_IDENT)) eat(P);
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_SOURCE)) {

            const tal_tok_t *st = eat(P);
            uint32_t nd = mk_node(P, PN_SOURCE, (uint32_t)(st - P->toks));
            add_kid(P, root, nd);
        } else if (at(P, TK_DIRECTIVE)) {

            eat(P);
        } else if (at(P, TK_COMMENT)) {
            eat(P);
        } else {

            eat(P);
        }
    }

    return root;
}
