/* main.c -- TAL LSP test harness */

#include "tal.h"

static const char *tk_name(uint16_t k)
{
    switch (k) {
    case TK_INT_LIT:    return "INT_LIT";
    case TK_REAL_LIT:   return "REAL_LIT";
    case TK_STRING_LIT: return "STRING_LIT";
    case TK_IDENT:      return "IDENT";
    case TK_COMMENT:    return "COMMENT";
    case TK_EOF:        return "EOF";
    case TK_PROC:       return "PROC";
    case TK_MAIN:       return "MAIN";
    case TK_BEGIN:      return "BEGIN";
    case TK_END:        return "END";
    case TK_INT:        return "INT";
    case TK_INT32:      return "INT(32)";
    case TK_STRING:     return "STRING";
    case TK_REAL:       return "REAL";
    case TK_REAL64:     return "REAL(64)";
    case TK_IF:         return "IF";
    case TK_THEN:       return "THEN";
    case TK_ELSE:       return "ELSE";
    case TK_WHILE:      return "WHILE";
    case TK_DO:         return "DO";
    case TK_FOR:        return "FOR";
    case TK_CALL:       return "CALL";
    case TK_RETURN:     return "RETURN";
    case TK_ASSIGN:     return "ASSIGN";
    case TK_SEMICOLON:  return "SEMI";
    case TK_LPAREN:     return "LPAREN";
    case TK_RPAREN:     return "RPAREN";
    case TK_COMMA:      return "COMMA";
    case TK_PLUS:       return "PLUS";
    case TK_MINUS:      return "MINUS";
    case TK_STAR:       return "STAR";
    case TK_EQ:         return "EQ";
    case TK_LT:         return "LT";
    case TK_GT:         return "GT";
    case TK_COLON:      return "COLON";
    case TK_DOT:        return "DOT";
    case TK_AT:         return "AT";
    case TK_LITERAL:    return "LITERAL";
    case TK_DEFINE:     return "DEFINE";
    case TK_STRUCT:     return "STRUCT";
    case TK_DOLLAR:     return "STDFUNC";
    case TK_MOVE:       return "MOVE";
    case TK_SCAN:       return "SCAN";
    case TK_CASE:       return "CASE";
    case TK_GOTO:       return "GOTO";
    default:            return "???";
    }
}

int main(int argc, char **argv)
{
    /* sample TAL programme from the Programmer's Guide */
    const char *sample =
        "! Sample TAL program\n"
        "PROC my_proc(msg, len) MAIN;\n"
        "  STRING .msg;\n"
        "  INT len;\n"
        "  BEGIN\n"
        "    INT count;\n"
        "    count := 0;\n"
        "    WHILE count < len DO\n"
        "    BEGIN\n"
        "      CALL WRITE(msg, count);\n"
        "      count := count + 1;\n"
        "    END;\n"
        "  END;\n";

    const char *src = sample;
    uint32_t slen = (uint32_t)strlen(src);

    /* allow file argument */
    if (argc > 1) {
        FILE *fp = fopen(argv[1], "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long fsz = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *buf = (char *)malloc((size_t)fsz + 1);
            if (buf) {
                fread(buf, 1, (size_t)fsz, fp);
                buf[fsz] = '\0';
                src = buf;
                slen = (uint32_t)fsz;
            }
            fclose(fp);
        }
    }

    static tal_lex_t lex;
    tal_lex(&lex, src, slen);

    printf("TAL Lexer -- %u tokens, %d diagnostics\n\n", lex.n_toks, lex.n_diags);

    for (uint32_t i = 0; i < lex.n_toks; i++) {
        tal_tok_t *t = &lex.toks[i];
        const char *text = tal_str(&lex, t->off);
        printf("%3u:%-3u  %-12s  %s\n", t->line, t->col, tk_name(t->kind), text);
    }

    for (int i = 0; i < lex.n_diags; i++) {
        tal_diag_t *d = &lex.diags[i];
        printf("DIAG %u:%u [%s] %s\n", d->line, d->col,
               d->severity == 1 ? "ERR" : d->severity == 2 ? "WARN" : "INFO",
               d->msg);
    }

    /* ---- Parse ---- */
    printf("\n--- Parser ---\n\n");

    static parse_ctx_t parse;
    uint32_t root = tal_parse(&parse, lex.toks, lex.n_toks, &lex);

    printf("AST: %u nodes, %d diagnostics\n\n", parse.n_nodes, parse.n_diags);

    static const char *nk_names[] = {
        [PN_PROC]    = "PROC",
        [PN_PARAM]   = "PARAM",
        [PN_DECL]    = "DECL",
        [PN_ASSIGN]  = "ASSIGN",
        [PN_IF]      = "IF",
        [PN_WHILE]   = "WHILE",
        [PN_FOR]     = "FOR",
        [PN_CASE]    = "CASE",
        [PN_CALL]    = "CALL",
        [PN_RETURN]  = "RETURN",
        [PN_GOTO]    = "GOTO",
        [PN_BEGIN]   = "BEGIN",
        [PN_LABEL]   = "LABEL",
        [PN_BINOP]   = "BINOP",
        [PN_UNOP]    = "UNOP",
        [PN_INTLIT]  = "INT",
        [PN_REALLIT] = "REAL",
        [PN_STRLIT]  = "STR",
        [PN_IDENT]   = "IDENT",
        [PN_ARRREF]  = "ARRREF",
        [PN_DOTREF]  = "DOTREF",
        [PN_DEREF]   = "DEREF",
        [PN_ADDROF]  = "ADDROF",
        [PN_FUNCCALL]= "FNCALL",
        [PN_STDFUNC] = "STDFUNC",
        [PN_EMPTY]   = "EMPTY",
        [PN_LITERAL] = "LITERAL",
        [PN_DEFINE]  = "DEFINE",
        [PN_MOVE]    = "MOVE",
        [PN_SCAN]    = "SCAN",
        [PN_CODE]    = "CODE",
        [PN_USE]     = "USE",
        [PN_DROP]    = "DROP",
        [PN_STORE]   = "STORE",
        [PN_ASSERT]  = "ASSERT",
        [PN_STRUCT_DECL] = "STRUCT",
        [PN_FIELD]   = "FIELD",
        [PN_SOURCE]  = "SOURCE",
    };

    /* simple tree dump */
    for (uint32_t i = 0; i < parse.n_nodes; i++) {
        pn_node_t *n = &parse.nodes[i];
        const char *nk = (n->kind < PN_COUNT && nk_names[n->kind])
                         ? nk_names[n->kind] : "???";
        const char *text = "";
        if (n->tok < lex.n_toks)
            text = tal_str(&lex, lex.toks[n->tok].off);

        printf("  [%3u] %-8s  %-16s  kids:", i, nk, text);
        for (int j = 0; j < n->n_kids; j++)
            printf(" %u", n->kids[j]);
        printf("\n");
    }

    for (int i = 0; i < parse.n_diags; i++) {
        tal_diag_t *d = &parse.diags[i];
        printf("PARSE DIAG %u:%u [%s] %s\n", d->line, d->col,
               d->severity == 1 ? "ERR" : "WARN", d->msg);
    }

    return 0;
}
