/* tal_lsp.h -- LSP server types: limits, JSON reader and writer, documents.
 *
 * (c) 2026 ZH. Apache 2.0.
 */

#ifndef TAL_LSP_H
#define TAL_LSP_H

#include "tal.h"
#include "tacl.h"

/* ---- Limits ---- */

#define LSP_MSG_MAX     (512 * 1024)
#define LSP_OUT_MAX     (256 * 1024)
#define LSP_MAX_DOCS    32
#define LSP_MAX_URI     512
#define LSP_MAX_METHOD  128

/* ---- JSON Reader ---- */

typedef struct {
    const char *json;
    uint32_t    len;
} jv_doc_t;

const char *jv_find(const jv_doc_t *doc, const char *path, uint32_t *out_len);

int jv_get_str(const jv_doc_t *doc, const char *path,
               char *buf, uint32_t buf_sz);

int64_t jv_get_int(const jv_doc_t *doc, const char *path);
int jv_has(const jv_doc_t *doc, const char *path);

uint32_t jv_decode_str(const char *src, uint32_t src_len,
                       char *dst, uint32_t dst_sz);

/* ---- JSON Writer ---- */

typedef struct {
    char     buf[LSP_OUT_MAX];
    uint32_t pos;
    int      need_comma[32];
    int      depth;
    int      full;
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

/* ---- Transport ---- */

typedef struct {
    char     buf[LSP_MSG_MAX];
    uint32_t len;
} lsp_msg_t;

int  lsp_read_msg(lsp_msg_t *msg);
void lsp_write_msg(const char *json, uint32_t len);

/* ---- Document Store ---- */

typedef enum { LANG_TAL, LANG_PTAL, LANG_TACL } lsp_lang_t;

typedef struct {
    char     uri[LSP_MAX_URI];
    char    *src;
    uint32_t src_len;
    int      version;
    int      is_open;
    uint8_t  lang;
} lsp_doc_t;

typedef struct {
    lsp_doc_t    docs[LSP_MAX_DOCS];
    int          n_docs;
    tal_lex_t   *lex;
    parse_ctx_t *prs;
    tal_symtab_t *sym;
    const lsp_doc_t *cur;
} lsp_store_t;

lsp_doc_t *doc_open(lsp_store_t *S, const char *uri,
                    const char *text, uint32_t len, int version,
                    uint8_t lang);
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

#endif
