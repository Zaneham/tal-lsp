/* tal_lsp.c -- TAL Language Server
 *
 * The bit that actually talks to editors. Single-threaded,
 * blocking stdin read loop, JSON-RPC over pipes. The way
 * God intended language servers to start before everyone
 * decided they needed async runtimes and fourteen layers
 * of abstraction.
 *
 * On each document open/change we run the full lex+parse
 * pipeline and push diagnostics. The analysis buffers are
 * shared (one tal_lex_t, one parse_ctx_t) because they're
 * enormous and we're single-threaded -- no point allocating
 * a megabyte per open tab.
 *
 * Steps covered here:
 *   1. JSON-RPC transport (Content-Length framing)
 *   2. initialize / shutdown / exit handshake
 *   3. textDocument/didOpen, didChange, didClose
 */

#include "tal_lsp.h"
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* ================================================================
 * Logging
 *
 * Everything goes to stderr, which is the one channel the
 * LSP spec leaves alone. stdout is sacred protocol territory.
 * ================================================================ */

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

/* ================================================================
 * JSON-RPC Transport
 *
 * The framing is HTTP-style headers followed by a JSON body:
 *
 *   Content-Length: 42\r\n
 *   \r\n
 *   {"jsonrpc":"2.0","method":"initialize",...}
 *
 * We only look at Content-Length. The spec allows Content-Type
 * but nobody sends it and we don't need it. Binary mode on
 * stdin/stdout is critical on Windows -- without it, \r\n
 * translation silently corrupts the Content-Length framing
 * and you spend a delightful afternoon wondering why your
 * server receives half a message.
 * ================================================================ */

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

    /* fread in a loop because buffered I/O on pipes can
     * return short reads even in blocking mode */
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

/* ================================================================
 * Response Helpers
 *
 * These wrap a result/error/notification in the JSON-RPC
 * envelope. The "splice result buffer directly into the
 * output" approach is crude but avoids a second serialisation
 * pass and we already have the data sitting in a jw_buf_t.
 * ================================================================ */

void lsp_respond(int64_t id, jw_buf_t *result)
{
    jw_buf_t w;
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

/* ================================================================
 * Document Store
 *
 * Flat array, linear scan. With a 32-slot limit this is
 * faster than a hash table and anyone with 32 TAL files
 * open simultaneously should probably close some tabs.
 * ================================================================ */

lsp_doc_t *doc_find(lsp_store_t *S, const char *uri)
{
    for (int i = 0; i < S->n_docs; i++) {
        if (S->docs[i].is_open && strcmp(S->docs[i].uri, uri) == 0)
            return &S->docs[i];
    }
    return NULL;
}

lsp_doc_t *doc_open(lsp_store_t *S, const char *uri,
                    const char *text, uint32_t len, int version)
{
    /* reclaim closed slots before growing */
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

    lsp_log("opened: %s (%u bytes, v%d)", uri, len, version);
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
        lsp_log("closed: %s", uri);
    }
}

/* ================================================================
 * Analysis Pipeline
 *
 * On every open or change, re-lex and re-parse the entire
 * document. This is the brute-force approach but TAL files
 * are small (NonStop programs are measured in pages, not
 * megabytes) and the lexer+parser are fast enough that
 * incremental analysis would be premature optimisation.
 *
 * Diagnostics from both phases are merged and published
 * in one notification. The severity values in tal_diag_t
 * (1=error, 2=warning, 3=info) happen to match the LSP
 * DiagnosticSeverity enum exactly, which is either good
 * planning or a happy coincidence. Line/column numbers
 * need converting from 1-based (TAL convention, matching
 * the reference manual) to 0-based (LSP convention,
 * matching the rest of 2020s tooling).
 * ================================================================ */

static void publish_diagnostics_3(const char *uri,
                                  const tal_diag_t *d1, int n1,
                                  const tal_diag_t *d2, int n2,
                                  const tal_diag_t *d3, int n3);

static void emit_diag(jw_buf_t *w, const tal_diag_t *d)
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
    jw_key(w, "source");   jw_str(w, "tal");
    jw_key(w, "message");  jw_str(w, d->msg);
    jw_obj_close(w);
}

static void publish_diagnostics_3(const char *uri,
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

    for (int i = 0; i < n1; i++) emit_diag(&w, &d1[i]);
    for (int i = 0; i < n2; i++) emit_diag(&w, &d2[i]);
    for (int i = 0; i < n3; i++) emit_diag(&w, &d3[i]);

    jw_arr_close(&w);
    jw_obj_close(&w);

    int total = n1 + n2 + n3;
    lsp_notify("textDocument/publishDiagnostics", &w);
    lsp_log("published %d diagnostics for %s", total, uri);
}

/* Unused symbols worth warning about. Procs and labels
 * might be called from external files we can't see, so
 * we only flag local variables and parameters -- things
 * that are definitively dead if unreferenced. */
static int should_warn_unused(const tal_sym_t *s)
{
    if (s->ref_count > 0) return 0;
    if (s->kind == TS_VAR && s->scope > 0) return 1;
    if (s->kind == TS_PARAM) return 1;
    return 0;
}

static void doc_analyze(lsp_store_t *S, lsp_doc_t *doc)
{
    if (!doc || !doc->src) return;

    memset(S->lex, 0, sizeof(*S->lex));
    tal_lex(S->lex, doc->src, doc->src_len);

    memset(S->prs, 0, sizeof(*S->prs));
    tal_parse(S->prs, S->lex->toks, S->lex->n_toks, S->lex);

    /* build the symbol table and count references */
    memset(S->sym, 0, sizeof(*S->sym));
    tal_sema(S->sym, S->prs, S->lex);
    tal_sema_refs(S->sym, S->lex);

    /* collect unused-symbol warnings into a temporary
     * diag array so we can publish them alongside the
     * lex/parse diagnostics */
    tal_diag_t unused_diags[TAL_MAX_DIAGS];
    int n_unused = 0;

    for (uint32_t i = 0; i < S->sym->n_syms && n_unused < TAL_MAX_DIAGS; i++) {
        const tal_sym_t *s = &S->sym->syms[i];
        if (should_warn_unused(s)) {
            tal_diag_t *d = &unused_diags[n_unused++];
            d->line = s->def_line;
            d->col  = s->def_col;
            d->len  = (uint32_t)strlen(s->name);
            d->severity = 2;  /* warning */
            snprintf(d->msg, sizeof(d->msg),
                     "'%s' is declared but never used", s->name);
        }
    }

    /* publish all diagnostics in one go */
    publish_diagnostics_3(doc->uri,
                          S->lex->diags, S->lex->n_diags,
                          S->prs->diags, S->prs->n_diags,
                          unused_diags, n_unused);
}

/* ================================================================
 * LSP Handlers
 *
 * The spec has a precise state machine: you start
 * uninitialized, receive "initialize", respond with
 * capabilities, receive "initialized" (a notification
 * with no response -- thanks Microsoft), and then you're
 * live. "shutdown" means stop doing work, "exit" means
 * actually die. Two messages to quit because one would
 * have been too simple.
 * ================================================================ */

static void handle_initialize(lsp_server_t *srv, int64_t id,
                              const jv_doc_t *msg)
{
    (void)msg;  /* rootUri etc. -- we'll care about these later */

    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
      jw_key(&w, "capabilities");
      jw_obj_open(&w);
        /* textDocumentSync = 1 means full document sync.
         * The client sends us the entire file on every
         * keystroke. Wasteful, but simple, and TAL files
         * aren't War and Peace. */
        jw_key(&w, "textDocumentSync");
        jw_int(&w, 1);

        /* completion: triggered by letters and $ (for
         * standard functions like $LEN, $CARRY etc.) */
        jw_key(&w, "completionProvider");
        jw_obj_open(&w);
          jw_key(&w, "triggerCharacters");
          jw_arr_open(&w);
            jw_str(&w, "$");
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
        jw_key(&w, "version"); jw_str(&w, "0.2.0");
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
    /* exit code 0 if we got a clean shutdown first, 1 if
     * the editor just pulled the plug without asking nicely */
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

    lsp_doc_t *doc = doc_open(&srv->store, uri, text, text_len, version);
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

    /* full sync means contentChanges[0].text is the entire document */
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

    /* clear diagnostics so the editor doesn't show stale
     * squiggles for a file that's no longer open */
    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "uri"); jw_str(&w, uri);
    jw_key(&w, "diagnostics"); jw_arr_open(&w); jw_arr_close(&w);
    jw_obj_close(&w);
    lsp_notify("textDocument/publishDiagnostics", &w);

    doc_close(&srv->store, uri);
}

/* ================================================================
 * Completion
 *
 * TAL has a small keyword set and most NonStop codebases have
 * a manageable number of symbols, so we can get away with
 * offering everything and letting the editor's fuzzy matcher
 * do the filtering. The alternative -- trying to be clever
 * about context -- would mean reimplementing half the parser
 * in the completion handler, which is the kind of mistake
 * you make once and regret for years.
 * ================================================================ */

/* TAL keywords in the order a programmer might reach for them.
 * Grouped by function rather than alphabetically because nobody
 * thinks in alphabetical order when they're writing code. */
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

/* LSP SymbolKind constants -- the spec numbers them and
 * we need to map our symbol kinds to theirs */
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

/* LSP CompletionItemKind */
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

static void handle_completion(lsp_server_t *srv, int64_t id,
                              const jv_doc_t *msg)
{
    (void)msg;  /* we offer everything regardless of position */

    jw_buf_t w;
    jw_init(&w);
    jw_arr_open(&w);

    /* keywords first -- the bread and butter */
    for (int i = 0; tal_keywords[i]; i++) {
        jw_obj_open(&w);
        jw_key(&w, "label"); jw_str(&w, tal_keywords[i]);
        jw_key(&w, "kind");  jw_int(&w, CIK_KEYWORD);
        jw_obj_close(&w);
    }

    /* then every symbol we know about */
    const tal_symtab_t *sym = srv->store.sym;
    for (uint32_t i = 0; i < sym->n_syms; i++) {
        const tal_sym_t *s = &sym->syms[i];
        jw_obj_open(&w);
        jw_key(&w, "label");  jw_str(&w, s->name);
        jw_key(&w, "kind");   jw_int(&w, sym_to_completion_kind(s->kind));

        /* detail shows the type/kind so you know what
         * you're completing into your code */
        char detail[128] = {0};
        const char *tn = type_name(s->type);
        if (tn)
            snprintf(detail, sizeof(detail), "%s %s", kind_name(s->kind), tn);
        else
            snprintf(detail, sizeof(detail), "%s", kind_name(s->kind));
        jw_key(&w, "detail"); jw_str(&w, detail);

        jw_obj_close(&w);
    }

    jw_arr_close(&w);
    lsp_respond(id, &w);
}

/* ================================================================
 * Hover
 *
 * Show what a symbol is and where it lives. This is the feature
 * that saves you from grepping through TACL scripts at 3am
 * trying to figure out what type MSG is.
 * ================================================================ */

static void handle_hover(lsp_server_t *srv, int64_t id,
                         const jv_doc_t *msg)
{
    /* LSP positions are 0-based, our internals are 1-based */
    int64_t line = jv_get_int(msg, "params.position.line") + 1;
    int64_t col  = jv_get_int(msg, "params.position.character") + 1;

    const tal_sym_t *sym = tal_sym_at(srv->store.sym,
                                       srv->store.lex, srv->store.prs,
                                       (uint32_t)line, (uint32_t)col);
    if (!sym) {
        lsp_respond_null(id);
        return;
    }

    /* build a markdown hover card */
    char hover[512];
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

    jw_buf_t w;
    jw_init(&w);
    jw_obj_open(&w);
    jw_key(&w, "contents");
    jw_obj_open(&w);
      jw_key(&w, "kind");  jw_str(&w, "markdown");
      jw_key(&w, "value"); jw_str(&w, hover);
    jw_obj_close(&w);
    jw_obj_close(&w);

    lsp_respond(id, &w);
}

/* ================================================================
 * Go-to-Definition
 *
 * Click a name, jump to where it was declared. The feature
 * that every TAL developer has been doing manually with
 * CTRL-F since the dawn of EDIT. No longer.
 * ================================================================ */

static void handle_definition(lsp_server_t *srv, int64_t id,
                              const jv_doc_t *msg)
{
    int64_t line = jv_get_int(msg, "params.position.line") + 1;
    int64_t col  = jv_get_int(msg, "params.position.character") + 1;

    /* we need the URI to send back a Location */
    char uri[LSP_MAX_URI];
    if (jv_get_str(msg, "params.textDocument.uri", uri, sizeof(uri)) < 0) {
        lsp_respond_null(id);
        return;
    }

    const tal_sym_t *sym = tal_sym_at(srv->store.sym,
                                       srv->store.lex, srv->store.prs,
                                       (uint32_t)line, (uint32_t)col);
    if (!sym) {
        lsp_respond_null(id);
        return;
    }

    /* respond with a Location pointing to the declaration */
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

/* ================================================================
 * Document Symbols
 *
 * The outline view. Shows PROCs, STRUCTs, LITERALs, DEFINEs
 * at a glance so you can navigate a TAL file without scrolling
 * through 2000 lines of MOVE statements and praying you
 * recognise a procedure boundary by indentation alone.
 * ================================================================ */

static void handle_document_symbols(lsp_server_t *srv, int64_t id,
                                    const jv_doc_t *msg)
{
    (void)msg;

    const tal_symtab_t *sym = srv->store.sym;

    jw_buf_t w;
    jw_init(&w);
    jw_arr_open(&w);

    for (uint32_t i = 0; i < sym->n_syms; i++) {
        const tal_sym_t *s = &sym->syms[i];

        /* skip fields, params, and local variables -- the outline
         * should show the big picture (procs, structs, globals),
         * not every INT count in every BEGIN block. */
        if (s->kind == TS_FIELD || s->kind == TS_PARAM)
            continue;
        if (s->kind == TS_VAR && s->scope > 0)
            continue;

        jw_obj_open(&w);
        jw_key(&w, "name"); jw_str(&w, s->name);
        jw_key(&w, "kind"); jw_int(&w, sym_to_symbol_kind(s->kind));

        /* detail: type info if available */
        const char *tn = type_name(s->type);
        if (tn) { jw_key(&w, "detail"); jw_str(&w, tn); }

        /* range and selectionRange both point at the declaration.
         * ideally range would span the whole PROC body, but
         * that requires tracking END positions which we don't
         * do yet. Good enough for now. */
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

/* ================================================================
 * Dispatch
 *
 * The state machine is simple:
 *   UNINITIALIZED -> only "initialize" accepted
 *   RUNNING       -> normal operation
 *   SHUTDOWN      -> only "exit" accepted
 *
 * Unknown requests get an error response. Unknown
 * notifications are silently ignored, per the spec.
 * This is one of the few places the LSP specification
 * shows restraint.
 * ================================================================ */

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
        { /* the spec's way of saying "I acknowledge your acknowledgement" */ }
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

/* ================================================================
 * Entry Point
 * ================================================================ */

int main(void)
{
    /* Binary mode is non-negotiable on Windows. Without it,
     * the C runtime helpfully translates \n to \r\n on output
     * and \r\n to \n on input, which completely destroys the
     * Content-Length framing. Every Windows LSP server learns
     * this lesson exactly once. */
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
