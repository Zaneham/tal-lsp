/* tal_parse.c -- TAL recursive descent parser
 *
 * Builds an AST from TAL tokens. The grammar follows the
 * reference manual structure closely enough that you can
 * hold both documents open and cross-reference.
 *
 * The parser is intentionally loose in some areas because
 * an LSP needs to handle partially-written code gracefully.
 * A strict parser that dies on the first missing semicolon
 * is useless when someone is mid-keystroke.
 */

#include "tal.h"
#include <stdio.h>

/* Types now in tal.h */

/* ---- Token Names ----
 * The parser's error messages used to say "expected token 31,
 * got 107" which is about as helpful as a NonStop crash dump
 * without the INSPECT manual. Now they say what they mean. */

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
    /* skip comments during parsing */
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

    /* error recovery: tell the programmer what we wanted
     * and what we got, in words they can actually use */
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
    if (n->n_kids < PN_MAX_KIDS)
        n->kids[n->n_kids++] = child;
}

/* ---- Forward Declarations ---- */

static uint32_t parse_expr(parse_ctx_t *P);
static uint32_t parse_stmt(parse_ctx_t *P);
static uint32_t parse_block(parse_ctx_t *P);

/* ---- Expression Parser ----
 * Precedence climbing. TAL operator precedence from the
 * reference manual (Chapter 4, page 4-3):
 *   Highest: unary NOT, - (negate), @
 *            *, /, (*), (/)
 *            +, -, (+), (-)
 *            <<, >>
 *            <, >, <=, >=, =, <>
 *            AND, LAND
 *            OR, LOR, XOR
 *   Lowest
 */

static int prec_of(uint16_t kind)
{
    switch (kind) {
    case TK_OR: case TK_LOR: case TK_XOR:    return 1;
    case TK_AND: case TK_LAND:               return 2;
    case TK_EQ: case TK_NEQ:
    case TK_LT: case TK_GT:
    case TK_LE: case TK_GE:                  return 3;
    case TK_LSHIFT: case TK_RSHIFT:          return 4;
    case TK_PLUS: case TK_MINUS:
    case TK_PLUS_UNSIGNED:
    case TK_MINUS_UNSIGNED:                   return 5;
    case TK_STAR: case TK_SLASH:
    case TK_STAR_UNSIGNED:
    case TK_SLASH_UNSIGNED:                   return 6;
    default:                                  return -1;
    }
}

static uint32_t parse_primary(parse_ctx_t *P)
{
    uint16_t k = peek_kind(P);

    /* unary operators */
    if (k == TK_NOT || k == TK_MINUS || k == TK_AT) {
        const tal_tok_t *op = eat(P);
        uint32_t operand = parse_primary(P);
        uint32_t nd = mk_node(P, PN_UNOP, (uint32_t)(op - P->toks));
        add_kid(P, nd, operand);
        return nd;
    }

    /* parenthesised expression */
    if (k == TK_LPAREN) {
        eat(P);
        uint32_t expr = parse_expr(P);
        expect(P, TK_RPAREN);
        return expr;
    }

    /* integer literal */
    if (k == TK_INT_LIT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_INTLIT, (uint32_t)(t - P->toks));
        return nd;
    }

    /* real literal */
    if (k == TK_REAL_LIT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_REALLIT, (uint32_t)(t - P->toks));
        return nd;
    }

    /* string literal */
    if (k == TK_STRING_LIT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_STRLIT, (uint32_t)(t - P->toks));
        return nd;
    }

    /* $function call */
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

    /* identifier, possibly with array subscript, struct member, or deref */
    if (k == TK_IDENT) {
        const tal_tok_t *t = eat(P);
        uint32_t nd = mk_node(P, PN_IDENT, (uint32_t)(t - P->toks));

        /* postfix: array subscript */
        if (at(P, TK_LBRACKET) || at(P, TK_LPAREN)) {
            /* TAL uses [ ] for bit extraction, ( ) for array subscript
             * but we use LPAREN here as TAL arrays use parens */
            if (at(P, TK_LPAREN)) {
                uint32_t arr = mk_node(P, PN_ARRREF, (uint32_t)(t - P->toks));
                add_kid(P, arr, nd);
                eat(P); /* ( */
                add_kid(P, arr, parse_expr(P));
                expect(P, TK_RPAREN);
                nd = arr;
            }
        }

        /* postfix: structure member access */
        while (at(P, TK_DOT)) {
            eat(P);
            if (at(P, TK_IDENT)) {
                const tal_tok_t *member = eat(P);
                uint32_t dot = mk_node(P, PN_DOTREF, (uint32_t)(member - P->toks));
                add_kid(P, dot, nd);
                uint32_t mnd = mk_node(P, PN_IDENT, (uint32_t)(member - P->toks));
                add_kid(P, dot, mnd);
                nd = dot;
            }
        }

        /* postfix: dereference */
        if (at(P, TK_APOSTROPHE)) {
            eat(P);
            uint32_t deref = mk_node(P, PN_DEREF, P->nodes[nd].tok);
            add_kid(P, deref, nd);
            nd = deref;
        }

        return nd;
    }

    /* fallthrough: unexpected token */
    const tal_tok_t *t = eat(P);
    return mk_node(P, PN_EMPTY, (uint32_t)(t - P->toks));
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

static uint32_t parse_decl(parse_ctx_t *P)
{
    /* type keyword already peeked by caller */
    const tal_tok_t *type_tok = eat(P);
    uint32_t decl = mk_node(P, PN_DECL, (uint32_t)(type_tok - P->toks));

    /* optional indirection: . for byte pointer, .EXT for extended */
    if (at(P, TK_DOT)) eat(P);

    /* variable name */
    if (at(P, TK_IDENT)) {
        const tal_tok_t *name = eat(P);
        uint32_t name_nd = mk_node(P, PN_IDENT, (uint32_t)(name - P->toks));
        add_kid(P, decl, name_nd);
    }

    /* optional array bounds: name[lo:hi] using parens in TAL */
    if (at(P, TK_LPAREN)) {
        eat(P);
        while (!at(P, TK_RPAREN) && !at(P, TK_EOF)) eat(P);
        if (at(P, TK_RPAREN)) eat(P);
    }

    /* optional initialisation: := value */
    if (at(P, TK_ASSIGN)) {
        eat(P);
        add_kid(P, decl, parse_expr(P));
    }

    return decl;
}

static uint32_t parse_if(parse_ctx_t *P)
{
    const tal_tok_t *ift = eat(P); /* IF */
    uint32_t nd = mk_node(P, PN_IF, (uint32_t)(ift - P->toks));

    add_kid(P, nd, parse_expr(P)); /* condition */
    expect(P, TK_THEN);
    add_kid(P, nd, parse_stmt(P)); /* then branch */

    if (at(P, TK_ELSE)) {
        eat(P);
        add_kid(P, nd, parse_stmt(P)); /* else branch */
    }
    return nd;
}

static uint32_t parse_while(parse_ctx_t *P)
{
    const tal_tok_t *wt = eat(P); /* WHILE */
    uint32_t nd = mk_node(P, PN_WHILE, (uint32_t)(wt - P->toks));

    add_kid(P, nd, parse_expr(P)); /* condition */
    expect(P, TK_DO);
    add_kid(P, nd, parse_stmt(P)); /* body */
    return nd;
}

static uint32_t parse_for(parse_ctx_t *P)
{
    const tal_tok_t *ft = eat(P); /* FOR */
    uint32_t nd = mk_node(P, PN_FOR, (uint32_t)(ft - P->toks));

    /* loop variable and range */
    add_kid(P, nd, parse_expr(P)); /* var := start */
    if (at(P, TK_TO) || at(P, TK_DOWNTO)) eat(P);
    add_kid(P, nd, parse_expr(P)); /* limit */
    if (at(P, TK_BY)) {
        eat(P);
        add_kid(P, nd, parse_expr(P)); /* step */
    }
    expect(P, TK_DO);
    add_kid(P, nd, parse_stmt(P)); /* body */
    return nd;
}

static uint32_t parse_case(parse_ctx_t *P)
{
    const tal_tok_t *ct = eat(P); /* CASE */
    uint32_t nd = mk_node(P, PN_CASE, (uint32_t)(ct - P->toks));

    add_kid(P, nd, parse_expr(P)); /* selector */
    expect(P, TK_OF);
    expect(P, TK_BEGIN);

    while (!at(P, TK_END) && !at(P, TK_EOF)) {
        if (at(P, TK_OTHERWISE)) {
            eat(P);
            add_kid(P, nd, parse_stmt(P));
            if (at(P, TK_SEMICOLON)) eat(P);
            break;
        }
        add_kid(P, nd, parse_stmt(P));
        if (at(P, TK_SEMICOLON)) eat(P);
    }

    expect(P, TK_END);
    return nd;
}

static uint32_t parse_call(parse_ctx_t *P)
{
    const tal_tok_t *ct = eat(P); /* CALL */
    uint32_t nd = mk_node(P, PN_CALL, (uint32_t)(ct - P->toks));

    if (at(P, TK_IDENT)) {
        const tal_tok_t *name = eat(P);
        uint32_t name_nd = mk_node(P, PN_IDENT, (uint32_t)(name - P->toks));
        add_kid(P, nd, name_nd);
    }

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

static uint32_t parse_block(parse_ctx_t *P)
{
    const tal_tok_t *bt = eat(P); /* BEGIN */
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

    /* declarations */
    if (is_type_keyword(k)) return parse_decl(P);

    /* compound statement */
    if (k == TK_BEGIN) return parse_block(P);

    /* control flow */
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
        /* MOVE/SCAN have complex syntax, for now consume to semicolon */
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

        /* DEFINE name = body #;
         * or DEFINE name(params) = body #;
         *
         * The body runs from '=' to '#' and can contain
         * basically anything -- it's TAL's macro system.
         * We capture the name and record the body range
         * so hover can show the expansion. */
        if (at(P, TK_IDENT)) {
            const tal_tok_t *name = eat(P);
            add_kid(P, nd, mk_node(P, PN_IDENT, (uint32_t)(name - P->toks)));
        }

        /* skip optional parameter list */
        if (at(P, TK_LPAREN)) {
            eat(P);
            while (!at(P, TK_RPAREN) && !at(P, TK_EOF))
                eat(P);
            if (at(P, TK_RPAREN)) eat(P);
        }

        /* '=' or skip to body end */
        if (at(P, TK_EQ)) eat(P);

        /* record body start token for sema to extract text */
        P->nodes[nd].flags = (uint16_t)P->pos;

        /* consume body tokens until # or ; or EOF */
        while (!at(P, TK_HASH) && !at(P, TK_SEMICOLON) && !at(P, TK_EOF))
            eat(P);
        if (at(P, TK_HASH)) eat(P);

        return nd;
    }

    /* identifier: could be label, assignment, or bare call */
    if (k == TK_IDENT) {
        uint32_t lhs = parse_expr(P);

        /* assignment */
        if (at(P, TK_ASSIGN)) {
            const tal_tok_t *eq = eat(P);
            uint32_t rhs = parse_expr(P);
            uint32_t nd = mk_node(P, PN_ASSIGN, (uint32_t)(eq - P->toks));
            add_kid(P, nd, lhs);
            add_kid(P, nd, rhs);
            return nd;
        }

        /* label: ident followed by colon */
        /* this is tricky because we already consumed the ident
         * as an expression. check if we see a colon. */
        if (at(P, TK_COLON)) {
            eat(P);
            uint32_t nd = mk_node(P, PN_LABEL, P->nodes[lhs].tok);
            return nd;
        }

        /* bare expression statement (e.g., function call without CALL) */
        return lhs;
    }

    /* skip unrecognised tokens */
    skip_to_semi(P);
    return mk_node(P, PN_EMPTY, (uint32_t)(cur(P) - P->toks));
}

/* ---- Procedure Parser ---- */

static uint32_t parse_proc(parse_ctx_t *P)
{
    const tal_tok_t *pt = eat(P); /* PROC */
    uint32_t nd = mk_node(P, PN_PROC, (uint32_t)(pt - P->toks));

    /* procedure name */
    if (at(P, TK_IDENT)) {
        const tal_tok_t *name = eat(P);
        uint32_t name_nd = mk_node(P, PN_IDENT, (uint32_t)(name - P->toks));
        add_kid(P, nd, name_nd);
    }

    /* formal parameter list */
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

    /* procedure attributes: MAIN, INTERRUPT, RESIDENT, etc */
    while (at(P, TK_MAIN) || at(P, TK_INTERRUPT) || at(P, TK_RESIDENT) ||
           at(P, TK_CALLABLE) || at(P, TK_PRIV) || at(P, TK_VARIABLE) ||
           at(P, TK_EXTENSIBLE) || at(P, TK_FORWARD) || at(P, TK_EXTERNAL) ||
           at(P, TK_LANGUAGE)) {
        eat(P);
        /* LANGUAGE takes a string arg */
        if (P->toks[P->pos - 1].kind == TK_LANGUAGE && at(P, TK_IDENT))
            eat(P);
    }

    expect(P, TK_SEMICOLON);

    /* procedure body: parameter declarations, local decls, statements */
    /* parameter type declarations come first */
    while (is_type_keyword(peek_kind(P))) {
        add_kid(P, nd, parse_decl(P));
        if (at(P, TK_SEMICOLON)) eat(P);
    }

    /* body: either BEGIN/END or single statement */
    if (at(P, TK_BEGIN)) {
        add_kid(P, nd, parse_block(P));
    }

    if (at(P, TK_SEMICOLON)) eat(P);
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

    /* skip leading comments */
    while (P->pos < P->n_toks && P->toks[P->pos].kind == TK_COMMENT)
        P->pos++;

    uint32_t root = mk_node(P, PN_BEGIN, 0);

    while (!at(P, TK_EOF)) {
        if (at(P, TK_PROC) || at(P, TK_SUBPROC)) {
            add_kid(P, root, parse_proc(P));
        } else if (is_type_keyword(peek_kind(P))) {
            /* global declarations */
            add_kid(P, root, parse_decl(P));
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_LITERAL)) {
            add_kid(P, root, parse_stmt(P));
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_DEFINE)) {
            add_kid(P, root, parse_stmt(P));
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_NAME) || at(P, TK_BLOCK)) {
            /* NAME/BLOCK declarations */
            eat(P);
            if (at(P, TK_IDENT)) eat(P);
            if (at(P, TK_SEMICOLON)) eat(P);
        } else if (at(P, TK_SOURCE)) {
            /* ?SOURCE filename -- record it so the LSP can
             * resolve cross-file references */
            const tal_tok_t *st = eat(P);
            uint32_t nd = mk_node(P, PN_SOURCE, (uint32_t)(st - P->toks));
            add_kid(P, root, nd);
        } else if (at(P, TK_DIRECTIVE)) {
            /* other ?directives -- skip them */
            eat(P);
        } else if (at(P, TK_COMMENT)) {
            eat(P);
        } else {
            /* skip unrecognised top-level tokens */
            eat(P);
        }
    }

    return root;
}
