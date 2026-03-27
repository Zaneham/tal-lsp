/* tal_json.c -- Minimal JSON for LSP
 *
 * The LSP spec demands JSON-RPC, and JSON-RPC demands
 * a JSON parser. Rather than dragging in cJSON or jansson
 * for what amounts to plucking five fields from a known
 * message shape, we roll our own.
 *
 * The reader never allocates. It walks the raw JSON buffer
 * with a dot-path query ("params.textDocument.uri") and
 * returns a pointer into the original text. This works
 * because LSP messages are small, predictable, and we only
 * ever need a handful of fields from each one.
 *
 * The writer emits into a fixed buffer with a comma-tracking
 * depth stack, because life is too short to debug off-by-one
 * comma errors in hand-assembled JSON strings.
 */

#include "tal_lsp.h"
#include <ctype.h>

/* ================================================================
 * Scanner Primitives
 *
 * These walk a position cursor forward over JSON structure
 * without extracting anything. The skip functions are the
 * workhorses that let jv_find hop over values it doesn't
 * care about -- which is most of them.
 * ================================================================ */

static void skip_ws(const char *s, uint32_t *p, uint32_t len)
{
    while (*p < len && (s[*p] == ' ' || s[*p] == '\t' ||
                        s[*p] == '\r' || s[*p] == '\n'))
        (*p)++;
}

/* Strings need special handling because \" is not a closing
 * quote and editors love sending paths full of backslashes. */
static void skip_string(const char *s, uint32_t *p, uint32_t len)
{
    if (*p >= len || s[*p] != '"') return;
    (*p)++;
    while (*p < len) {
        if (s[*p] == '\\') { (*p) += 2; continue; }
        if (s[*p] == '"') { (*p)++; return; }
        (*p)++;
    }
}

/* Skip any JSON value by its opening character. Objects and
 * arrays are handled with a depth counter rather than recursion
 * because TAL files are not going to blow the stack but our
 * paranoia about stack depth is justified by experience. */
static void skip_value(const char *s, uint32_t *p, uint32_t len)
{
    skip_ws(s, p, len);
    if (*p >= len) return;

    char c = s[*p];
    if (c == '"') {
        skip_string(s, p, len);
    } else if (c == '{') {
        (*p)++;
        int depth = 1;
        while (*p < len && depth > 0) {
            if (s[*p] == '"') { skip_string(s, p, len); continue; }
            if (s[*p] == '{') depth++;
            else if (s[*p] == '}') depth--;
            (*p)++;
        }
    } else if (c == '[') {
        (*p)++;
        int depth = 1;
        while (*p < len && depth > 0) {
            if (s[*p] == '"') { skip_string(s, p, len); continue; }
            if (s[*p] == '[') depth++;
            else if (s[*p] == ']') depth--;
            (*p)++;
        }
    } else if (c == 't') { *p += 4;       /* true  */
    } else if (c == 'f') { *p += 5;       /* false */
    } else if (c == 'n') { *p += 4;       /* null  */
    } else {
        /* numbers: just eat digits, signs, dots, exponents */
        while (*p < len && (isdigit((unsigned char)s[*p]) ||
               s[*p] == '-' || s[*p] == '+' ||
               s[*p] == '.' || s[*p] == 'e' || s[*p] == 'E'))
            (*p)++;
    }
}

/* Check whether the quoted string at pos matches a key name.
 * We compare inside the quotes so "method" matches method. */
static int match_key(const char *s, uint32_t pos, uint32_t len,
                     const char *key, int keylen)
{
    if (pos >= len || s[pos] != '"') return 0;
    pos++;
    if (pos + (uint32_t)keylen + 1 > len) return 0;
    if (memcmp(s + pos, key, (size_t)keylen) != 0) return 0;
    return s[pos + keylen] == '"';
}

/* ================================================================
 * Path-Based Query
 *
 * The core trick: split "params.textDocument.uri" on dots,
 * then at each level scan the JSON object for the matching
 * key and descend into its value. Numeric segments index
 * arrays (e.g. "contentChanges.0.text" for full-sync where
 * we only ever care about the first element anyway).
 *
 * No allocation, no recursion, no tree. Just a cursor
 * walking forward through text it mostly ignores.
 * ================================================================ */

const char *jv_find(const jv_doc_t *doc, const char *path, uint32_t *out_len)
{
    const char *s = doc->json;
    uint32_t len = doc->len;
    uint32_t pos = 0;

    const char *seg = path;

    while (*seg) {
        const char *dot = seg;
        while (*dot && *dot != '.') dot++;
        int seglen = (int)(dot - seg);

        /* numeric segment = array index */
        int is_index = 1;
        for (int i = 0; i < seglen; i++) {
            if (!isdigit((unsigned char)seg[i])) { is_index = 0; break; }
        }

        skip_ws(s, &pos, len);
        if (pos >= len) return NULL;

        if (is_index) {
            if (s[pos] != '[') return NULL;
            pos++;
            int target = 0;
            for (int i = 0; i < seglen; i++)
                target = target * 10 + (seg[i] - '0');

            for (int idx = 0; idx <= target; idx++) {
                skip_ws(s, &pos, len);
                if (pos >= len || s[pos] == ']') return NULL;
                if (idx == target) break;
                skip_value(s, &pos, len);
                skip_ws(s, &pos, len);
                if (pos < len && s[pos] == ',') pos++;
            }
        } else {
            if (s[pos] != '{') return NULL;
            pos++;

            int found = 0;
            while (pos < len) {
                skip_ws(s, &pos, len);
                if (pos >= len || s[pos] == '}') break;
                if (s[pos] == ',') { pos++; continue; }

                if (match_key(s, pos, len, seg, seglen)) {
                    skip_string(s, &pos, len);
                    skip_ws(s, &pos, len);
                    if (pos < len && s[pos] == ':') pos++;
                    skip_ws(s, &pos, len);
                    found = 1;
                    break;
                }

                /* not our key -- skip it and its value */
                skip_string(s, &pos, len);
                skip_ws(s, &pos, len);
                if (pos < len && s[pos] == ':') pos++;
                skip_value(s, &pos, len);
            }
            if (!found) return NULL;
        }

        seg = *dot ? dot + 1 : dot;
    }

    skip_ws(s, &pos, len);
    if (pos >= len) return NULL;

    uint32_t start = pos;
    skip_value(s, &pos, len);
    if (out_len) *out_len = pos - start;
    return s + start;
}

int jv_has(const jv_doc_t *doc, const char *path)
{
    uint32_t len;
    return jv_find(doc, path, &len) != NULL;
}

/* ================================================================
 * String Decoding
 *
 * JSON strings are the main annoyance here. The LSP client
 * sends entire documents as JSON string values, so every
 * backslash, quote, and newline in the TAL source arrives
 * escaped. We need to undo all of that before feeding the
 * text to the lexer.
 *
 * \uXXXX decodes to UTF-8 for BMP characters. Surrogate
 * pairs are left as-is because TAL source is ASCII and if
 * someone has astral plane characters in their PROC names
 * they deserve whatever happens.
 * ================================================================ */

uint32_t jv_decode_str(const char *src, uint32_t src_len,
                       char *dst, uint32_t dst_sz)
{
    uint32_t si = 0, di = 0;

    if (si < src_len && src[si] == '"') si++;

    while (si < src_len && di < dst_sz - 1) {
        char c = src[si];
        if (c == '"') break;

        if (c == '\\' && si + 1 < src_len) {
            si++;
            switch (src[si]) {
            case '"':  dst[di++] = '"';  break;
            case '\\': dst[di++] = '\\'; break;
            case '/':  dst[di++] = '/';  break;
            case 'n':  dst[di++] = '\n'; break;
            case 'r':  dst[di++] = '\r'; break;
            case 't':  dst[di++] = '\t'; break;
            case 'b':  dst[di++] = '\b'; break;
            case 'f':  dst[di++] = '\f'; break;
            case 'u': {
                if (si + 4 < src_len) {
                    unsigned cp = 0;
                    for (int k = 1; k <= 4; k++) {
                        char h = src[si + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    }
                    si += 4;
                    if (cp < 0x80) {
                        dst[di++] = (char)cp;
                    } else if (cp < 0x800) {
                        if (di + 1 < dst_sz - 1) {
                            dst[di++] = (char)(0xC0 | (cp >> 6));
                            dst[di++] = (char)(0x80 | (cp & 0x3F));
                        }
                    } else {
                        if (di + 2 < dst_sz - 1) {
                            dst[di++] = (char)(0xE0 | (cp >> 12));
                            dst[di++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            dst[di++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                }
                break;
            }
            default: dst[di++] = src[si]; break;
            }
            si++;
        } else {
            dst[di++] = c;
            si++;
        }
    }

    dst[di] = '\0';
    return di;
}

/* ---- Convenience extractors ---- */

int jv_get_str(const jv_doc_t *doc, const char *path,
               char *buf, uint32_t buf_sz)
{
    uint32_t raw_len;
    const char *raw = jv_find(doc, path, &raw_len);
    if (!raw || raw[0] != '"') return -1;
    return (int)jv_decode_str(raw, raw_len, buf, buf_sz);
}

int64_t jv_get_int(const jv_doc_t *doc, const char *path)
{
    uint32_t raw_len;
    const char *raw = jv_find(doc, path, &raw_len);
    if (!raw) return 0;

    int neg = 0;
    uint32_t i = 0;
    if (raw[i] == '-') { neg = 1; i++; }

    int64_t val = 0;
    while (i < raw_len && isdigit((unsigned char)raw[i])) {
        val = val * 10 + (raw[i] - '0');
        i++;
    }
    return neg ? -val : val;
}

/* ================================================================
 * JSON Writer
 *
 * Fixed-buffer emitter with a comma-tracking depth stack.
 * Every open brace/bracket pushes a level, every close pops.
 * The need_comma flag at each level handles the eternal
 * "do I need a comma before this element" question that has
 * plagued every hand-written JSON emitter since 2001.
 * ================================================================ */

static void jw_put(jw_buf_t *w, char c)
{
    if (w->pos < LSP_OUT_MAX - 1)
        w->buf[w->pos++] = c;
}

static void jw_puts(jw_buf_t *w, const char *s)
{
    while (*s && w->pos < LSP_OUT_MAX - 1)
        w->buf[w->pos++] = *s++;
}

static void jw_comma(jw_buf_t *w)
{
    if (w->depth >= 0 && w->need_comma[w->depth])
        jw_put(w, ',');
    if (w->depth >= 0)
        w->need_comma[w->depth] = 1;
}

void jw_init(jw_buf_t *w)
{
    memset(w, 0, sizeof(*w));
    w->depth = -1;
}

void jw_obj_open(jw_buf_t *w)
{
    jw_comma(w);
    jw_put(w, '{');
    w->depth++;
    w->need_comma[w->depth] = 0;
}

void jw_obj_close(jw_buf_t *w)
{
    jw_put(w, '}');
    w->depth--;
}

void jw_arr_open(jw_buf_t *w)
{
    jw_comma(w);
    jw_put(w, '[');
    w->depth++;
    w->need_comma[w->depth] = 0;
}

void jw_arr_close(jw_buf_t *w)
{
    jw_put(w, ']');
    w->depth--;
}

void jw_key(jw_buf_t *w, const char *k)
{
    jw_comma(w);
    jw_put(w, '"');
    jw_puts(w, k);
    jw_put(w, '"');
    jw_put(w, ':');
    /* the value that follows the key should not get
     * a leading comma -- it's part of the same pair */
    w->need_comma[w->depth] = 0;
}

void jw_str(jw_buf_t *w, const char *v)
{
    jw_comma(w);
    jw_put(w, '"');
    while (*v) {
        switch (*v) {
        case '"':  jw_puts(w, "\\\""); break;
        case '\\': jw_puts(w, "\\\\"); break;
        case '\n': jw_puts(w, "\\n");  break;
        case '\r': jw_puts(w, "\\r");  break;
        case '\t': jw_puts(w, "\\t");  break;
        default:   jw_put(w, *v);      break;
        }
        v++;
    }
    jw_put(w, '"');
}

void jw_int(jw_buf_t *w, int64_t v)
{
    jw_comma(w);
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v);
    for (int i = 0; i < n; i++)
        jw_put(w, tmp[i]);
}

void jw_bool(jw_buf_t *w, int v)
{
    jw_comma(w);
    jw_puts(w, v ? "true" : "false");
}

void jw_null(jw_buf_t *w)
{
    jw_comma(w);
    jw_puts(w, "null");
}
