/* tal_lsp.c -- the server. JSON-RPC over stdin/stdout with Content-Length
 * framing, single-threaded, full lex and parse on every open and change.
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#include "tal_lsp.h"
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* ---- Logging ---- */

void lsp_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[tal-lsp] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    fflush(stderr);
    va_end(ap);
}

/* ---- JSON-RPC transport ---- */

int lsp_read_msg(lsp_msg_t *msg)
{
    int content_length = -1;
    char hdr[512];

    for (;;) {
        if (!fgets(hdr, (int)sizeof(hdr), stdin))
            return -1;

        if (hdr[0] == '\r' || hdr[0] == '\n')
            break;

        if (strncmp(hdr, "Content-Length:", 15) == 0)
            content_length = atoi(hdr + 15);
    }

    if (content_length < 0 || (uint32_t)content_length >= LSP_MSG_MAX)
        return -1;

    uint32_t total = 0;
    while (total < (uint32_t)content_length) {
        size_t n = fread(msg->buf + total, 1,
                         (size_t)(content_length - (int)total), stdin);
        if (n == 0) return -1;
        total += (uint32_t)n;
    }

    msg->buf[total] = '\0';
    msg->len = total;
    return 0;
}

void lsp_write_msg(const char *json, uint32_t len)
{
    fprintf(stdout, "Content-Length: %u\r\n\r\n", len);
    fwrite(json, 1, len, stdout);
    fflush(stdout);
}

/* ---- Response helpers ---- */

void lsp_respond(int64_t id, jw_buf_t *result)
{
    jw_buf_t w;
    if (result->full || result->pos > LSP_OUT_MAX - 64) {
        lsp_respond_err(id, -32603, "result too large for one message");
        return;
    }
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "jsonrpc"); jw_str(&w, "2.0");
    jw_key(&w, "id");      jw_int(&w, id);
    jw_key(&w, "result");
    for (uint32_t i = 0; i < result->pos; i++) {
        if (w.pos < LSP_OUT_MAX - 1)
            w.buf[w.pos++] = result->buf[i];
    }
    jw_obj_close(&w);
    w.buf[w.pos] = '\0';

    lsp_write_msg(w.buf, w.pos);
}

void lsp_respond_null(int64_t id)
{
    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "jsonrpc"); jw_str(&w, "2.0");
    jw_key(&w, "id");      jw_int(&w, id);
    jw_key(&w, "result");  jw_null(&w);
    jw_obj_close(&w);
    w.buf[w.pos] = '\0';

    lsp_write_msg(w.buf, w.pos);
}

void lsp_respond_err(int64_t id, int code, const char *message)
{
    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "jsonrpc"); jw_str(&w, "2.0");
    jw_key(&w, "id");      jw_int(&w, id);
    jw_key(&w, "error");
    jw_obj_open(&w);
    jw_key(&w, "code");    jw_int(&w, code);
    jw_key(&w, "message"); jw_str(&w, message);
    jw_obj_close(&w);
    jw_obj_close(&w);
    w.buf[w.pos] = '\0';

    lsp_write_msg(w.buf, w.pos);
}

void lsp_notify(const char *method, jw_buf_t *params)
{
    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "jsonrpc"); jw_str(&w, "2.0");
    jw_key(&w, "method");  jw_str(&w, method);
    jw_key(&w, "params");
    for (uint32_t i = 0; i < params->pos; i++) {
        if (w.pos < LSP_OUT_MAX - 1)
            w.buf[w.pos++] = params->buf[i];
    }
    jw_obj_close(&w);
    w.buf[w.pos] = '\0';

    lsp_write_msg(w.buf, w.pos);
}

/* ---- Document store ---- */

lsp_doc_t *doc_find(lsp_store_t *S, const char *uri)
{
    for (int i = 0; i < S->n_docs; i++) {
        if (S->docs[i].is_open && strcmp(S->docs[i].uri, uri) == 0)
            return &S->docs[i];
    }
    return NULL;
}

lsp_doc_t *doc_open(lsp_store_t *S, const char *uri,
                    const char *text, uint32_t len, int version,
                    uint8_t lang)
{

    lsp_doc_t *doc = NULL;
    for (int i = 0; i < S->n_docs; i++) {
        if (!S->docs[i].is_open) { doc = &S->docs[i]; break; }
    }
    if (!doc && S->n_docs < LSP_MAX_DOCS)
        doc = &S->docs[S->n_docs++];

    if (!doc) {
        lsp_log("document store full -- someone has too many tabs open");
        return NULL;
    }

    snprintf(doc->uri, LSP_MAX_URI, "%s", uri);
    doc->src = (char *)malloc(len + 1);
    if (doc->src) {
        memcpy(doc->src, text, len);
        doc->src[len] = '\0';
    }
    doc->src_len = len;
    doc->version = version;
    doc->is_open = 1;
    doc->lang = lang;

    lsp_log("opened: %s (%u bytes, v%d, lang %u)", uri, len, version, lang);
    return doc;
}

void doc_update(lsp_doc_t *doc, const char *text,
                uint32_t len, int version)
{
    free(doc->src);
    doc->src = (char *)malloc(len + 1);
    if (doc->src) {
        memcpy(doc->src, text, len);
        doc->src[len] = '\0';
    }
    doc->src_len = len;
    doc->version = version;

    lsp_log("updated: %s (%u bytes, v%d)", doc->uri, len, version);
}

void doc_close(lsp_store_t *S, const char *uri)
{
    lsp_doc_t *doc = doc_find(S, uri);
    if (doc) {
        free(doc->src);
        doc->src = NULL;
        doc->src_len = 0;
        doc->is_open = 0;
        if (S->cur == doc) S->cur = NULL;
        lsp_log("closed: %s", uri);
    }
}

/* ---- Analysis pipeline ---- */

static void publish_diagnostics_3(const char *uri, const char *src,
                                  const tal_diag_t *d1, int n1,
                                  const tal_diag_t *d2, int n2,
                                  const tal_diag_t *d3, int n3);

static void emit_diag(jw_buf_t *w, const tal_diag_t *d, const char *src)
{
    uint32_t line = d->line > 0 ? d->line - 1 : 0;
    uint32_t col  = d->col  > 0 ? d->col  - 1 : 0;
    uint32_t end_col = col + (d->len > 0 ? d->len : 1);

    jw_obj_open(w);
    jw_key(w, "range");
    jw_obj_open(w);
      jw_key(w, "start");
      jw_obj_open(w);
        jw_key(w, "line");      jw_int(w, (int64_t)line);
        jw_key(w, "character"); jw_int(w, (int64_t)col);
      jw_obj_close(w);
      jw_key(w, "end");
      jw_obj_open(w);
        jw_key(w, "line");      jw_int(w, (int64_t)line);
        jw_key(w, "character"); jw_int(w, (int64_t)end_col);
      jw_obj_close(w);
    jw_obj_close(w);
    jw_key(w, "severity"); jw_int(w, d->severity);
    jw_key(w, "source");   jw_str(w, src);
    jw_key(w, "message");  jw_str(w, d->msg);
    jw_obj_close(w);
}

static void publish_diagnostics_3(const char *uri, const char *src,
                                  const tal_diag_t *d1, int n1,
                                  const tal_diag_t *d2, int n2,
                                  const tal_diag_t *d3, int n3)
{
    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "uri"); jw_str(&w, uri);
    jw_key(&w, "diagnostics");
    jw_arr_open(&w);

    for (int i = 0; i < n1; i++) emit_diag(&w, &d1[i], src);
    for (int i = 0; i < n2; i++) emit_diag(&w, &d2[i], src);
    for (int i = 0; i < n3; i++) emit_diag(&w, &d3[i], src);

    jw_arr_close(&w);
    jw_obj_close(&w);

    int total = n1 + n2 + n3;
    lsp_notify("textDocument/publishDiagnostics", &w);
    lsp_log("published %d diagnostics for %s", total, uri);
}

static int should_warn_unused(const tal_sym_t *s)
{
    if (s->ref_count > 0) return 0;
    if (s->kind == TS_VAR && s->scope > 0) return 1;
    if (s->kind == TS_PARAM) return 1;
    return 0;
}

static const char *lang_src(uint8_t lang)
{
    return lang == LANG_TACL ? "tacl" : lang == LANG_PTAL ? "ptal" : "tal";
}

static void dialect(lsp_store_t *S, uint8_t lang, tal_diag_t *out, int *n)
{
    const tal_lex_t *L = S->lex;
    for (uint32_t i = 0; i < L->n_toks && *n < TAL_MAX_DIAGS; i++) {
        const tal_tok_t *t = &L->toks[i];
        uint16_t k = t->kind;
        int old = k == TK_CODE || k == TK_STACK || k == TK_STORE || k == TK_USE || k == TK_DROP;
        if (!(lang == LANG_PTAL && old) && !(lang == LANG_TAL && k == TK_PTAL)) continue;
        tal_diag_t *d = &out[(*n)++];
        d->line = t->line;
        d->col = t->col;
        d->len = t->len;
        d->severity = 3;
        if (old)
            snprintf(d->msg, sizeof d->msg, "%s is not available in pTAL", tal_str(L, t->off));
        else
            snprintf(d->msg, sizeof d->msg, "%s is a pTAL keyword, not TAL", tal_str(L, t->off));
    }
}

static void tacl_analyze(lsp_store_t *S, lsp_doc_t *doc)
{
    tc_lex(S->lex, doc->src, doc->src_len);
    memset(S->prs, 0, sizeof(*S->prs));
    tc_pass(S->sym, S->lex, S->prs->diags, &S->prs->n_diags);
    publish_diagnostics_3(doc->uri, "tacl",
                          S->lex->diags, S->lex->n_diags,
                          S->prs->diags, S->prs->n_diags,
                          NULL, 0);
}

static void doc_analyze(lsp_store_t *S, lsp_doc_t *doc)
{
    if (!doc || !doc->src) return;
    S->cur = doc;
    if (doc->lang == LANG_TACL) { tacl_analyze(S, doc); return; }

    memset(S->lex, 0, sizeof(*S->lex));
    tal_lex(S->lex, doc->src, doc->src_len);

    memset(S->prs, 0, sizeof(*S->prs));
    tal_parse(S->prs, S->lex->toks, S->lex->n_toks, S->lex);

    memset(S->sym, 0, sizeof(*S->sym));
    tal_sema(S->sym, S->prs, S->lex);
    tal_sema_refs(S->sym, S->lex);

    tal_diag_t unused_diags[TAL_MAX_DIAGS];
    int n_unused = 0;

    for (uint32_t i = 0; i < S->sym->n_syms && n_unused < TAL_MAX_DIAGS; i++) {
        const tal_sym_t *s = &S->sym->syms[i];
        if (should_warn_unused(s)) {
            tal_diag_t *d = &unused_diags[n_unused++];
            d->line = s->def_line;
            d->col  = s->def_col;
            d->len  = (uint32_t)strlen(s->name);
            d->severity = 2;
            snprintf(d->msg, sizeof(d->msg),
                     "'%s' is declared but never used", s->name);
        }
    }

    dialect(S, doc->lang, unused_diags, &n_unused);

    publish_diagnostics_3(doc->uri, lang_src(doc->lang),
                          S->lex->diags, S->lex->n_diags,
                          S->prs->diags, S->prs->n_diags,
                          unused_diags, n_unused);
}

static const lsp_doc_t *ensure(lsp_server_t *srv, const jv_doc_t *msg)
{
    char uri[LSP_MAX_URI];
    if (jv_get_str(msg, "params.textDocument.uri", uri, sizeof uri) < 0) return NULL;
    lsp_doc_t *doc = doc_find(&srv->store, uri);
    if (!doc) return NULL;
    if (srv->store.cur != doc) doc_analyze(&srv->store, doc);
    return doc;
}

static const tal_tok_t *tok_at(const tal_lex_t *L, uint32_t line, uint32_t col)
{
    for (uint32_t i = 0; i < L->n_toks; i++) {
        const tal_tok_t *t = &L->toks[i];
        if (t->line == line && col >= t->col && col < t->col + t->len) return t;
    }
    return NULL;
}

static const tal_sym_t *tc_symat(const lsp_store_t *S, const tal_tok_t *t)
{
    if (!t || t->kind != TC_WORD) return NULL;
    const char *s = tal_str(S->lex, t->off);
    uint32_t n = 0;
    if (t->len && s[0] == ':') n = t->len;
    else while (n < t->len && s[n] != '.' && s[n] != ':' && s[n] != '(') n++;
    if (n == 0 || n >= TAL_MAX_IDENT) return NULL;
    char name[TAL_MAX_IDENT];
    memcpy(name, s, n);
    name[n] = '\0';
    return tal_sym_lookup(S->sym, name);
}

/* ---- LSP handlers ---- */

static void handle_initialize(lsp_server_t *srv, int64_t id,
                              const jv_doc_t *msg)
{
    (void)msg;

    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
      jw_key(&w, "capabilities");
      jw_obj_open(&w);

        jw_key(&w, "textDocumentSync");
        jw_int(&w, 1);

        jw_key(&w, "completionProvider");
        jw_obj_open(&w);
          jw_key(&w, "triggerCharacters");
          jw_arr_open(&w);
            jw_str(&w, "$");
            jw_str(&w, "#");
            jw_str(&w, "|");
            jw_str(&w, "?");
          jw_arr_close(&w);
        jw_obj_close(&w);

        jw_key(&w, "hoverProvider");
        jw_bool(&w, 1);

        jw_key(&w, "definitionProvider");
        jw_bool(&w, 1);

        jw_key(&w, "documentSymbolProvider");
        jw_bool(&w, 1);

      jw_obj_close(&w);
      jw_key(&w, "serverInfo");
      jw_obj_open(&w);
        jw_key(&w, "name");    jw_str(&w, "tal-lsp");
        jw_key(&w, "version"); jw_str(&w, "0.3.0");
      jw_obj_close(&w);
    jw_obj_close(&w);

    lsp_respond(id, &w);
    srv->state = LSP_RUNNING;
    lsp_log("initialized");
}

static void handle_shutdown(lsp_server_t *srv, int64_t id)
{
    for (int i = 0; i < srv->store.n_docs; i++) {
        if (srv->store.docs[i].is_open) {
            free(srv->store.docs[i].src);
            srv->store.docs[i].src = NULL;
            srv->store.docs[i].is_open = 0;
        }
    }

    lsp_respond_null(id);
    srv->state = LSP_SHUTDOWN;
    srv->exit_code = 0;
    lsp_log("shutdown");
}

static void handle_exit(lsp_server_t *srv)
{

    if (srv->state != LSP_SHUTDOWN)
        srv->exit_code = 1;
    srv->state = LSP_EXIT;
    lsp_log("exit (code %d)", srv->exit_code);
}

static void handle_did_open(lsp_server_t *srv, const jv_doc_t *msg)
{
    char uri[LSP_MAX_URI];
    if (jv_get_str(msg, "params.textDocument.uri", uri, sizeof(uri)) < 0) {
        lsp_log("didOpen: missing uri -- editor sent us a mystery document");
        return;
    }

    uint32_t raw_len;
    const char *raw = jv_find(msg, "params.textDocument.text", &raw_len);
    if (!raw) {
        lsp_log("didOpen: missing text -- editor opened a file but forgot to tell us what's in it");
        return;
    }

    char *text = (char *)malloc(raw_len + 1);
    if (!text) return;
    uint32_t text_len = jv_decode_str(raw, raw_len, text, raw_len + 1);

    int version = (int)jv_get_int(msg, "params.textDocument.version");

    char lid[32] = "tal";
    jv_get_str(msg, "params.textDocument.languageId", lid, sizeof lid);
    uint8_t lang = strcmp(lid, "tacl") == 0 ? LANG_TACL
                 : strcmp(lid, "ptal") == 0 ? LANG_PTAL : LANG_TAL;

    lsp_doc_t *doc = doc_open(&srv->store, uri, text, text_len, version, lang);
    free(text);

    if (doc) doc_analyze(&srv->store, doc);
}

static void handle_did_change(lsp_server_t *srv, const jv_doc_t *msg)
{
    char uri[LSP_MAX_URI];
    if (jv_get_str(msg, "params.textDocument.uri", uri, sizeof(uri)) < 0)
        return;

    lsp_doc_t *doc = doc_find(&srv->store, uri);
    if (!doc) {
        lsp_log("didChange for unknown document %s -- perhaps it was never opened?", uri);
        return;
    }

    uint32_t raw_len;
    const char *raw = jv_find(msg, "params.contentChanges.0.text", &raw_len);
    if (!raw) return;

    char *text = (char *)malloc(raw_len + 1);
    if (!text) return;
    uint32_t text_len = jv_decode_str(raw, raw_len, text, raw_len + 1);

    int version = (int)jv_get_int(msg, "params.textDocument.version");
    doc_update(doc, text, text_len, version);
    free(text);

    doc_analyze(&srv->store, doc);
}

static void handle_did_close(lsp_server_t *srv, const jv_doc_t *msg)
{
    char uri[LSP_MAX_URI];
    if (jv_get_str(msg, "params.textDocument.uri", uri, sizeof(uri)) < 0)
        return;

    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "uri"); jw_str(&w, uri);
    jw_key(&w, "diagnostics"); jw_arr_open(&w); jw_arr_close(&w);
    jw_obj_close(&w);
    lsp_notify("textDocument/publishDiagnostics", &w);

    doc_close(&srv->store, uri);
}

/* ---- Completion ---- */

static const char *tal_keywords[] = {
    "PROC", "SUBPROC", "MAIN", "FORWARD", "EXTERNAL",
    "BEGIN", "END",
    "INT", "INT(32)", "STRING", "REAL", "REAL(64)",
    "FIXED", "UNSIGNED",
    "STRUCT", "FILLER", "BIT_FILLER",
    "LITERAL", "DEFINE",
    "IF", "THEN", "ELSE",
    "WHILE", "DO", "FOR", "TO", "DOWNTO", "BY", "UNTIL",
    "CASE", "OF", "OTHERWISE",
    "CALL", "RETURN", "GOTO",
    "MOVE", "SCAN", "RSCAN",
    "CODE", "STORE", "USE", "DROP",
    "AND", "OR", "NOT", "LAND", "LOR", "XOR",
    "ASSERT",
    "CALLABLE", "PRIV", "VARIABLE", "EXTENSIBLE",
    "RESIDENT", "INTERRUPT", "ENTRY",
    "GLOBAL", "LOCAL", "SUBLOCAL",
    "NAME", "BLOCK",
    NULL
};

enum {
    SK_FILE = 1, SK_MODULE = 2, SK_NAMESPACE = 3,
    SK_PACKAGE = 4, SK_CLASS = 5, SK_METHOD = 6,
    SK_PROPERTY = 7, SK_FIELD = 8, SK_CONSTRUCTOR = 9,
    SK_ENUM = 10, SK_INTERFACE = 11, SK_FUNCTION = 12,
    SK_VARIABLE = 13, SK_CONSTANT = 14, SK_STRING = 15,
    SK_NUMBER = 16, SK_BOOLEAN = 17, SK_ARRAY = 18,
    SK_OBJECT = 19, SK_KEY = 20, SK_NULL = 21,
    SK_ENUMMEMBER = 22, SK_STRUCT = 23, SK_EVENT = 24,
    SK_OPERATOR = 25, SK_TYPEPARAM = 26
};

enum {
    CIK_TEXT = 1, CIK_METHOD = 2, CIK_FUNCTION = 3,
    CIK_CONSTRUCTOR = 4, CIK_FIELD = 5, CIK_VARIABLE = 6,
    CIK_CLASS = 7, CIK_INTERFACE = 8, CIK_MODULE = 9,
    CIK_PROPERTY = 10, CIK_UNIT = 11, CIK_VALUE = 12,
    CIK_ENUM = 13, CIK_KEYWORD = 14, CIK_SNIPPET = 15,
    CIK_COLOR = 16, CIK_FILE = 17, CIK_REFERENCE = 18,
    CIK_FOLDER = 19, CIK_ENUMMEMBER = 20, CIK_CONSTANT = 21,
    CIK_STRUCT = 22, CIK_EVENT = 23, CIK_OPERATOR = 24,
    CIK_TYPEPARAM = 25
};

static int sym_to_completion_kind(uint8_t sk)
{
    switch (sk) {
    case TS_PROC:    case TS_SUBPROC: return CIK_FUNCTION;
    case TS_VAR:     case TS_PARAM:   return CIK_VARIABLE;
    case TS_LITERAL:                  return CIK_CONSTANT;
    case TS_DEFINE:                   return CIK_VALUE;
    case TS_LABEL:                    return CIK_REFERENCE;
    case TS_STRUCT:                   return CIK_STRUCT;
    case TS_FIELD:                    return CIK_FIELD;
    case TS_ENTRY:                    return CIK_METHOD;
    default:                          return CIK_TEXT;
    }
}

static int sym_to_symbol_kind(uint8_t sk)
{
    switch (sk) {
    case TS_PROC:    case TS_SUBPROC: return SK_FUNCTION;
    case TS_VAR:     case TS_PARAM:   return SK_VARIABLE;
    case TS_LITERAL:                  return SK_CONSTANT;
    case TS_DEFINE:                   return SK_CONSTANT;
    case TS_LABEL:                    return SK_KEY;
    case TS_STRUCT:                   return SK_STRUCT;
    case TS_FIELD:                    return SK_FIELD;
    case TS_ENTRY:                    return SK_METHOD;
    default:                          return SK_VARIABLE;
    }
}

static const char *type_name(uint8_t type)
{
    switch (type) {
    case TT_STRING:   return "STRING";
    case TT_INT:      return "INT";
    case TT_INT32:    return "INT(32)";
    case TT_FIXED:    return "FIXED";
    case TT_REAL:     return "REAL";
    case TT_REAL64:   return "REAL(64)";
    case TT_UNSIGNED: return "UNSIGNED";
    case TT_STRUCT:   return "STRUCT";
    default:          return NULL;
    }
}

static const char *kind_name(uint8_t kind)
{
    switch (kind) {
    case TS_VAR:     return "variable";
    case TS_LITERAL: return "literal";
    case TS_DEFINE:  return "define";
    case TS_PROC:    return "proc";
    case TS_SUBPROC: return "subproc";
    case TS_LABEL:   return "label";
    case TS_ENTRY:   return "entry";
    case TS_STRUCT:  return "struct";
    case TS_FIELD:   return "field";
    case TS_PARAM:   return "parameter";
    default:         return "symbol";
    }
}

static const char *ptal_words[] = {
    "BADDR", "WADDR", "EXTADDR", "CBADDR", "CWADDR", "SGBADDR", "SGWADDR",
    "SGXBADDR", "SGXWADDR", "PROCADDR", "PROCPTR", "REFALIGNED",
    "FIELDALIGN", "VOLATILE", "LABEL", NULL
};

static const char *tacl_labels[] = {
    "|THEN|", "|ELSE|", "|OTHERWISE|", "|BODY|", "|WHILE|", "|DO|", "|UNTIL|", NULL
};

static const char *tacl_direct[] = { "?TACL", "?SECTION", "?FORMAT", "?BLANK", NULL };

static const char *tacl_types[] = {
    "TEXT", "ALIAS", "MACRO", "ROUTINE", "STRUCT", "DIRECTORY", "DELTA", NULL
};

static void item(jw_buf_t *w, const char *label, int kind, const char *detail)
{
    jw_obj_open(w);
    jw_key(w, "label"); jw_str(w, label);
    jw_key(w, "kind");  jw_int(w, kind);
    if (detail && detail[0]) { jw_key(w, "detail"); jw_str(w, detail); }
    jw_obj_close(w);
}

static void items(jw_buf_t *w, const char *const *list, int kind)
{
    for (int i = 0; i < 256 && list[i]; i++) item(w, list[i], kind, NULL);
}

static void itemsn(jw_buf_t *w, const char *const *list, uint32_t n, int kind)
{
    for (uint32_t i = 0; i < n; i++) item(w, list[i], kind, NULL);
}

static int tc_ckind(uint8_t kind)
{
    switch (kind) {
    case TC_S_MACRO: case TC_S_ROUTINE: return CIK_FUNCTION;
    case TC_S_STRUCT:                   return CIK_STRUCT;
    case TC_S_ITEM:                     return CIK_FIELD;
    case TC_S_DIRECTORY:                return CIK_MODULE;
    case TC_S_ALIAS:                    return CIK_REFERENCE;
    default:                            return CIK_VARIABLE;
    }
}

static void tacl_items(jw_buf_t *w, const lsp_store_t *S, tc_ctx_t c)
{
    if (c == TC_C_LABEL)   { items(w, tacl_labels, CIK_KEYWORD); return; }
    if (c == TC_C_DIRECT)  { items(w, tacl_direct, CIK_KEYWORD); return; }
    if (c == TC_C_SECTYPE) { items(w, tacl_types, CIK_KEYWORD); return; }
    if (c == TC_C_ARGTYPE) { itemsn(w, tc_argtab, tc_narg, CIK_ENUMMEMBER); return; }
    for (uint32_t i = 0; i < tc_nbl; i++)
        item(w, tc_bltab[i].name,
             tc_bltab[i].kind == TC_K_VAR ? CIK_VARIABLE : CIK_FUNCTION,
             tc_bltab[i].desc);
    if (c == TC_C_BUILTIN) return;
    for (uint32_t i = 0; i < tc_ncm; i++)
        item(w, tc_cmtab[i].name,
             tc_cmtab[i].kind == TC_K_CMD ? CIK_FUNCTION : CIK_MODULE,
             tc_cmtab[i].desc);
    for (uint32_t i = 0; i < S->sym->n_syms; i++) {
        const tal_sym_t *s = &S->sym->syms[i];
        item(w, s->name, tc_ckind(s->kind), tc_kname(s->kind));
    }
}

static void tal_items(jw_buf_t *w, const lsp_store_t *S, int ptal)
{
    items(w, tal_keywords, CIK_KEYWORD);
    if (ptal) items(w, ptal_words, CIK_KEYWORD);
    for (uint32_t i = 0; i < sf_n; i++)
        item(w, sf_tab[i].name, CIK_FUNCTION, sf_tab[i].desc);
    for (uint32_t i = 0; i < S->sym->n_syms; i++) {
        const tal_sym_t *s = &S->sym->syms[i];
        char detail[128];
        const char *tn = type_name(s->type);
        if (tn) snprintf(detail, sizeof detail, "%s %s", kind_name(s->kind), tn);
        else snprintf(detail, sizeof detail, "%s", kind_name(s->kind));
        item(w, s->name, sym_to_completion_kind(s->kind), detail);
    }
    for (uint32_t i = 0; i < gp_n; i++) {
        char detail[300];
        snprintf(detail, sizeof detail, "%s%s",
                 gp_tab[i].superseded ? "(superseded) " : "", gp_tab[i].summary);
        item(w, gp_tab[i].name, CIK_FUNCTION, detail);
    }
}

static void handle_completion(lsp_server_t *srv, int64_t id,
                              const jv_doc_t *msg)
{
    const lsp_doc_t *doc = ensure(srv, msg);
    jw_buf_t w;
    jw_init(&w);
    jw_arr_open(&w);
    if (doc && doc->lang == LANG_TACL) {
        int64_t line = jv_get_int(msg, "params.position.line") + 1;
        int64_t col  = jv_get_int(msg, "params.position.character") + 1;
        tacl_items(&w, &srv->store,
                   tc_ctx(doc->src, doc->src_len, (uint32_t)line, (uint32_t)col));
    } else {
        tal_items(&w, &srv->store, doc && doc->lang == LANG_PTAL);
    }
    jw_arr_close(&w);
    lsp_respond(id, &w);
}

/* ---- Hover ---- */

static int tacl_hover(const lsp_store_t *S, uint32_t line, uint32_t col,
                      char *out, size_t n)
{
    const tal_tok_t *t = tok_at(S->lex, line, col);
    if (!t) return 0;
    const char *s = tal_str(S->lex, t->off);
    if (t->kind == TC_BUILTIN) {
        const tc_ent_t *e = tc_bfind(s, t->len);
        if (!e) return 0;
        snprintf(out, n, "```tacl\n%s\n```\n%s%s", e->name, e->desc,
                 e->restricted ? " (super-group only)" : "");
        return 1;
    }
    const tal_sym_t *y = tc_symat(S, t);
    if (y) {
        snprintf(out, n, "```tacl\n%s %s\n```\nDeclared at line %u",
                 tc_kname(y->kind), y->name, y->def_line);
        return 1;
    }
    const tc_ent_t *e = tc_cfind(s, t->len);
    if (!e) return 0;
    snprintf(out, n, "```tacl\n%s\n```\n%s%s", e->name, e->desc,
             e->restricted ? " (restricted)" : "");
    return 1;
}

static int tal_ref(const lsp_store_t *S, uint32_t line, uint32_t col,
                   char *out, size_t n)
{
    const tal_tok_t *t = tok_at(S->lex, line, col);
    if (!t) return 0;
    const char *s = tal_str(S->lex, t->off);
    if (t->kind == TK_IDENT) {
        const gp_ent_t *g = gp_find(s, t->len);
        if (!g) return 0;
        snprintf(out, n, "```tal\n%s\n```\n%s%s\n\n*Guardian Procedure Calls Reference Manual*",
                 g->syntax[0] ? g->syntax : g->name, g->summary,
                 g->superseded ? "\n\nSuperseded, see the manual for the replacement." : "");
        return 1;
    }
    if (t->kind == TK_DOLLAR || t->kind == TK_DOLLAR_CARRY || t->kind == TK_DOLLAR_OVERFLOW) {
        const sf_ent_t *f = sf_find(s, t->len);
        if (!f) return 0;
        snprintf(out, n, "```tal\n%s\n```\n%s", f->syntax[0] ? f->syntax : f->name, f->desc);
        return 1;
    }
    return 0;
}

static void respond_md(int64_t id, const char *md)
{
    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "contents");
    jw_obj_open(&w);
      jw_key(&w, "kind");  jw_str(&w, "markdown");
      jw_key(&w, "value"); jw_str(&w, md);
    jw_obj_close(&w);
    jw_obj_close(&w);
    lsp_respond(id, &w);
}

static void handle_hover(lsp_server_t *srv, int64_t id,
                         const jv_doc_t *msg)
{

    int64_t line = jv_get_int(msg, "params.position.line") + 1;
    int64_t col  = jv_get_int(msg, "params.position.character") + 1;
    const lsp_doc_t *doc = ensure(srv, msg);
    char hover[2048];

    if (doc && doc->lang == LANG_TACL) {
        if (tacl_hover(&srv->store, (uint32_t)line, (uint32_t)col, hover, sizeof hover))
            respond_md(id, hover);
        else
            lsp_respond_null(id);
        return;
    }

    const tal_sym_t *sym = tal_sym_at(srv->store.sym,
                                       srv->store.lex, srv->store.prs,
                                       (uint32_t)line, (uint32_t)col);
    if (!sym) {
        if (tal_ref(&srv->store, (uint32_t)line, (uint32_t)col, hover, sizeof hover))
            respond_md(id, hover);
        else
            lsp_respond_null(id);
        return;
    }

    const char *tn = type_name(sym->type);

    if (sym->kind == TS_PROC || sym->kind == TS_SUBPROC) {
        snprintf(hover, sizeof(hover),
                 "```tal\n%s %s\n```\nDeclared at line %u",
                 sym->kind == TS_PROC ? "PROC" : "SUBPROC",
                 sym->name, sym->def_line);
    } else if (sym->kind == TS_DEFINE) {
        const char *body = tal_define_body(sym, srv->store.lex);
        if (body[0])
            snprintf(hover, sizeof(hover),
                     "```tal\nDEFINE %s = %s#\n```\nDeclared at line %u",
                     sym->name, body, sym->def_line);
        else
            snprintf(hover, sizeof(hover),
                     "```tal\nDEFINE %s\n```\nDeclared at line %u",
                     sym->name, sym->def_line);
    } else if (tn) {
        snprintf(hover, sizeof(hover),
                 "```tal\n%s %s%s%s\n```\n*%s* -- declared at line %u",
                 tn, sym->is_ptr ? "." : "",
                 sym->name,
                 sym->is_array ? "[]" : "",
                 kind_name(sym->kind), sym->def_line);
    } else {
        snprintf(hover, sizeof(hover),
                 "```tal\n%s\n```\n*%s* -- declared at line %u",
                 sym->name, kind_name(sym->kind), sym->def_line);
    }

    respond_md(id, hover);
}

/* ---- Go-to-definition ---- */

static void handle_definition(lsp_server_t *srv, int64_t id,
                              const jv_doc_t *msg)
{
    int64_t line = jv_get_int(msg, "params.position.line") + 1;
    int64_t col  = jv_get_int(msg, "params.position.character") + 1;

    char uri[LSP_MAX_URI];
    if (jv_get_str(msg, "params.textDocument.uri", uri, sizeof(uri)) < 0) {
        lsp_respond_null(id);
        return;
    }

    const lsp_doc_t *doc = ensure(srv, msg);
    const tal_sym_t *sym;
    if (doc && doc->lang == LANG_TACL)
        sym = tc_symat(&srv->store, tok_at(srv->store.lex, (uint32_t)line, (uint32_t)col));
    else
        sym = tal_sym_at(srv->store.sym, srv->store.lex, srv->store.prs,
                         (uint32_t)line, (uint32_t)col);
    if (!sym) {
        lsp_respond_null(id);
        return;
    }

    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "uri"); jw_str(&w, uri);
    jw_key(&w, "range");
    jw_obj_open(&w);
      jw_key(&w, "start");
      jw_obj_open(&w);
        jw_key(&w, "line");      jw_int(&w, (int64_t)(sym->def_line - 1));
        jw_key(&w, "character"); jw_int(&w, (int64_t)(sym->def_col - 1));
      jw_obj_close(&w);
      jw_key(&w, "end");
      jw_obj_open(&w);
        jw_key(&w, "line");      jw_int(&w, (int64_t)(sym->def_line - 1));
        jw_key(&w, "character"); jw_int(&w, (int64_t)(sym->def_col - 1 + strlen(sym->name)));
      jw_obj_close(&w);
    jw_obj_close(&w);
    jw_obj_close(&w);

    lsp_respond(id, &w);
}

/* ---- Document symbols ---- */

static int tc_skind(uint8_t kind)
{
    switch (kind) {
    case TC_S_MACRO: case TC_S_ROUTINE: return SK_FUNCTION;
    case TC_S_STRUCT:                   return SK_STRUCT;
    case TC_S_ITEM:                     return SK_FIELD;
    case TC_S_DIRECTORY:                return SK_NAMESPACE;
    default:                            return SK_VARIABLE;
    }
}

static int outline(const tal_sym_t *s, int tacl)
{
    if (tacl) return s->kind != TC_S_PUSHED && s->kind != TC_S_ITEM;
    if (s->kind == TS_FIELD || s->kind == TS_PARAM) return 0;
    if (s->kind == TS_VAR && s->scope > 0) return 0;
    return 1;
}

static void handle_document_symbols(lsp_server_t *srv, int64_t id,
                                    const jv_doc_t *msg)
{
    const lsp_doc_t *doc = ensure(srv, msg);
    int tacl = doc && doc->lang == LANG_TACL;

    const tal_symtab_t *sym = srv->store.sym;

    jw_buf_t w;
    jw_init(&w);
    jw_arr_open(&w);

    for (uint32_t i = 0; i < sym->n_syms; i++) {
        const tal_sym_t *s = &sym->syms[i];

        if (!outline(s, tacl)) continue;

        jw_obj_open(&w);
        jw_key(&w, "name"); jw_str(&w, s->name);
        jw_key(&w, "kind"); jw_int(&w, tacl ? tc_skind(s->kind) : sym_to_symbol_kind(s->kind));

        const char *tn = tacl ? tc_kname(s->kind) : type_name(s->type);
        if (tn) { jw_key(&w, "detail"); jw_str(&w, tn); }

        jw_key(&w, "range");
        jw_obj_open(&w);
          jw_key(&w, "start");
          jw_obj_open(&w);
            jw_key(&w, "line");      jw_int(&w, (int64_t)(s->def_line - 1));
            jw_key(&w, "character"); jw_int(&w, (int64_t)(s->def_col - 1));
          jw_obj_close(&w);
          jw_key(&w, "end");
          jw_obj_open(&w);
            jw_key(&w, "line");      jw_int(&w, (int64_t)(s->def_line - 1));
            jw_key(&w, "character"); jw_int(&w, (int64_t)(s->def_col - 1 + strlen(s->name)));
          jw_obj_close(&w);
        jw_obj_close(&w);

        jw_key(&w, "selectionRange");
        jw_obj_open(&w);
          jw_key(&w, "start");
          jw_obj_open(&w);
            jw_key(&w, "line");      jw_int(&w, (int64_t)(s->def_line - 1));
            jw_key(&w, "character"); jw_int(&w, (int64_t)(s->def_col - 1));
          jw_obj_close(&w);
          jw_key(&w, "end");
          jw_obj_open(&w);
            jw_key(&w, "line");      jw_int(&w, (int64_t)(s->def_line - 1));
            jw_key(&w, "character"); jw_int(&w, (int64_t)(s->def_col - 1 + strlen(s->name)));
          jw_obj_close(&w);
        jw_obj_close(&w);

        jw_obj_close(&w);
    }

    jw_arr_close(&w);
    lsp_respond(id, &w);
}

/* ---- Dispatch ---- */

static void lsp_dispatch(lsp_server_t *srv, const char *method,
                         int64_t id, int has_id, const jv_doc_t *doc)
{
    if (srv->state == LSP_UNINITIALIZED) {
        if (strcmp(method, "initialize") == 0) {
            handle_initialize(srv, id, doc);
            return;
        }
        if (has_id)
            lsp_respond_err(id, -32002, "Server not initialized");
        return;
    }

    if (srv->state == LSP_SHUTDOWN) {
        if (strcmp(method, "exit") == 0) {
            handle_exit(srv);
            return;
        }
        if (has_id)
            lsp_respond_err(id, -32600, "Server is shut down");
        return;
    }

    if (strcmp(method, "shutdown") == 0)
        handle_shutdown(srv, id);
    else if (strcmp(method, "initialized") == 0)
        {  }
    else if (strcmp(method, "textDocument/didOpen") == 0)
        handle_did_open(srv, doc);
    else if (strcmp(method, "textDocument/didChange") == 0)
        handle_did_change(srv, doc);
    else if (strcmp(method, "textDocument/didClose") == 0)
        handle_did_close(srv, doc);
    else if (strcmp(method, "textDocument/completion") == 0)
        handle_completion(srv, id, doc);
    else if (strcmp(method, "textDocument/hover") == 0)
        handle_hover(srv, id, doc);
    else if (strcmp(method, "textDocument/definition") == 0)
        handle_definition(srv, id, doc);
    else if (strcmp(method, "textDocument/documentSymbol") == 0)
        handle_document_symbols(srv, id, doc);
    else if (has_id)
        lsp_respond_err(id, -32601, "Method not found");
}

/* ---- Entry point ---- */

int main(void)
{

#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    lsp_server_t srv;
    memset(&srv, 0, sizeof(srv));
    srv.state = LSP_UNINITIALIZED;
    srv.store.lex = (tal_lex_t *)malloc(sizeof(tal_lex_t));
    srv.store.prs = (parse_ctx_t *)malloc(sizeof(parse_ctx_t));
    srv.store.sym = (tal_symtab_t *)malloc(sizeof(tal_symtab_t));

    if (!srv.store.lex || !srv.store.prs || !srv.store.sym) {
        lsp_log("failed to allocate analysis buffers -- out of memory at startup is a bad sign");
        return 1;
    }

    lsp_log("starting");

    lsp_msg_t msg;
    while (srv.state != LSP_EXIT && lsp_read_msg(&msg) == 0) {
        jv_doc_t doc = { msg.buf, msg.len };

        char method[LSP_MAX_METHOD] = {0};
        jv_get_str(&doc, "method", method, sizeof(method));

        int64_t id = jv_get_int(&doc, "id");
        int has_id = jv_has(&doc, "id");

        lsp_log("recv: %s (id=%lld)", method, (long long)id);
        lsp_dispatch(&srv, method, id, has_id, &doc);
    }

    free(srv.store.lex);
    free(srv.store.prs);
    free(srv.store.sym);
    lsp_log("exiting with code %d", srv.exit_code);
    return srv.exit_code;
}
