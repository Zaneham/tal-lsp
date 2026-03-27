/* tal_lsp.h -- TAL Language Server Protocol types
 *
 * Everything the LSP server needs to talk to editors,
 * wrangle documents, and pretend to be a well-behaved
 * member of the LSP specification family.
 *
 * The protocol is JSON-RPC over stdin/stdout with
 * Content-Length framing -- Microsoft's answer to the
 * question nobody asked: "what if HTTP headers, but
 * for everything?"
 */

#ifndef TAL_LSP_H
#define TAL_LSP_H

#include "tal.h"

/* ---- Limits ----
 * 512KB per message because TAL files on NonStop are
 * measured in kilobytes, not megabytes. If someone is
 * editing a 512KB TAL file they have bigger problems
 * than buffer limits. */

#define LSP_MSG_MAX     (512 * 1024)
#define LSP_OUT_MAX     (64 * 1024)
#define LSP_MAX_DOCS    32
#define LSP_MAX_URI     512
#define LSP_MAX_METHOD  128

/* ---- JSON Reader ----
 *
 * No DOM, no allocations, no nonsense. The reader works
 * as a cursor into the raw JSON text using dot-path
 * queries. We only ever need a handful of specific fields
 * from any LSP message, so building a full parse tree
 * would be like hiring a crane to hang a picture frame. */

typedef struct {
    const char *json;
    uint32_t    len;
} jv_doc_t;

/* Navigate to a value by dot-path ("params.textDocument.uri").
 * Numeric segments index into arrays ("contentChanges.0.text").
 * Returns pointer into the raw JSON buffer, NULL if not found. */
const char *jv_find(const jv_doc_t *doc, const char *path, uint32_t *out_len);

/* Pull a string value out, unescaping as we go. Returns length or -1. */
int jv_get_str(const jv_doc_t *doc, const char *path,
               char *buf, uint32_t buf_sz);

int64_t jv_get_int(const jv_doc_t *doc, const char *path);
int jv_has(const jv_doc_t *doc, const char *path);

/* Decode a JSON string: strip quotes, unescape \" \\ \n etc.
 * src should point at the opening quote. */
uint32_t jv_decode_str(const char *src, uint32_t src_len,
                       char *dst, uint32_t dst_sz);

/* ---- JSON Writer ----
 *
 * Emits JSON into a fixed buffer. Tracks comma state
 * with a depth stack so callers can just open/close
 * objects without counting their children. */

typedef struct {
    char     buf[LSP_OUT_MAX];
    uint32_t pos;
    int      need_comma[32];    /* one flag per nesting level */
    int      depth;
} jw_buf_t;

void jw_init(jw_buf_t *w);
void jw_obj_open(jw_buf_t *w);
void jw_obj_close(jw_buf_t *w);
void jw_arr_open(jw_buf_t *w);
void jw_arr_close(jw_buf_t *w);
void jw_key(jw_buf_t *w, const char *k);
void jw_str(jw_buf_t *w, const char *v);
void jw_int(jw_buf_t *w, int64_t v);
void jw_bool(jw_buf_t *w, int v);
void jw_null(jw_buf_t *w);

/* ---- Transport ----
 *
 * Content-Length framed messages on stdin/stdout.
 * The spec says "Content-Type" is optional and we
 * take that at its word -- we never send it and we
 * never read it. */

typedef struct {
    char     buf[LSP_MSG_MAX];
    uint32_t len;
} lsp_msg_t;

int  lsp_read_msg(lsp_msg_t *msg);
void lsp_write_msg(const char *json, uint32_t len);

/* ---- Document Store ----
 *
 * One slot per open file. Source text is heap-allocated
 * because it arrives as a JSON string of unpredictable
 * length, but everything else stays in static arrays.
 *
 * The lex and prs pointers are shared analysis buffers --
 * one set for the whole server, re-used for each document.
 * tal_lex_t alone is ~1MB, so one-per-document would get
 * expensive fast and we're single-threaded anyway. */

typedef struct {
    char     uri[LSP_MAX_URI];
    char    *src;
    uint32_t src_len;
    int      version;
    int      is_open;
} lsp_doc_t;

typedef struct {
    lsp_doc_t    docs[LSP_MAX_DOCS];
    int          n_docs;
    tal_lex_t   *lex;
    parse_ctx_t *prs;
    tal_symtab_t *sym;
} lsp_store_t;

lsp_doc_t *doc_open(lsp_store_t *S, const char *uri,
                    const char *text, uint32_t len, int version);
lsp_doc_t *doc_find(lsp_store_t *S, const char *uri);
void       doc_update(lsp_doc_t *doc, const char *text,
                      uint32_t len, int version);
void       doc_close(lsp_store_t *S, const char *uri);

/* ---- Server State ---- */

typedef enum {
    LSP_UNINITIALIZED,
    LSP_RUNNING,
    LSP_SHUTDOWN,
    LSP_EXIT
} lsp_state_t;

typedef struct {
    lsp_state_t  state;
    lsp_store_t  store;
    int          exit_code;
} lsp_server_t;

/* ---- Protocol Helpers ---- */

void lsp_respond(int64_t id, jw_buf_t *result);
void lsp_respond_null(int64_t id);
void lsp_respond_err(int64_t id, int code, const char *message);
void lsp_notify(const char *method, jw_buf_t *params);
void lsp_log(const char *fmt, ...);

#endif /* TAL_LSP_H */
