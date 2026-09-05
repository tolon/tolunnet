/*
 * Tolunnet — HTTP URL and Header Parser Implementation
 *
 * Pure C unit without AmigaOS NDK dependencies.
 */

#include "http_url.h"
#include <string.h>

static int is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static int to_lower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int ci_strncmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int c1 = to_lower((unsigned char)s1[i]);
        int c2 = to_lower((unsigned char)s2[i]);
        if (c1 != c2 || s1[i] == '\0' || s2[i] == '\0') {
            return c1 - c2;
        }
    }
    return 0;
}

int tn_http_parse_url(const char *url_str, struct TnUrl *out)
{
    const char *p;
    size_t hlen = 0;
    size_t plen = 0;

    if (!url_str || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->port = 80;
    out->path[0] = '/';
    out->path[1] = '\0';

    p = url_str;
    while (*p && is_space(*p)) p++;

    if (ci_strncmp(p, "https://", 8) == 0) {
        out->scheme = TN_SCHEME_HTTPS;
        p += 8;
    } else if (ci_strncmp(p, "http://", 7) == 0) {
        out->scheme = TN_SCHEME_HTTP;
        p += 7;
    } else {
        out->scheme = TN_SCHEME_HTTP;
    }

    while (*p && *p != ':' && *p != '/') {
        if (hlen >= sizeof(out->host) - 1) return -1;
        out->host[hlen++] = *p++;
    }
    out->host[hlen] = '\0';
    if (hlen == 0) return -1;

    if (*p == ':') {
        uint32_t port = 0;
        p++;
        if (*p < '0' || *p > '9') return -1;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            if (port > 65535) return -1;
            p++;
        }
        out->port = port;
    }

    if (*p == '/') {
        while (*p && !is_space(*p)) {
            if (plen >= sizeof(out->path) - 1) return -1;
            out->path[plen++] = *p++;
        }
        out->path[plen] = '\0';
    } else if (*p == '\0') {
        out->path[0] = '/';
        out->path[1] = '\0';
    } else {
        return -1;
    }

    return 0;
}

int tn_http_resolve_redirect(const struct TnUrl *current, const char *location, struct TnUrl *out)
{
    const char *loc;

    if (!current || !location || !out) return -1;

    loc = location;
    while (*loc && is_space(*loc)) loc++;

    if (ci_strncmp(loc, "http://", 7) == 0 || ci_strncmp(loc, "https://", 8) == 0) {
        return tn_http_parse_url(loc, out);
    }

    memcpy(out, current, sizeof(*out));

    if (*loc == '/') {
        size_t plen = 0;
        while (*loc && !is_space(*loc)) {
            if (plen >= sizeof(out->path) - 1) return -1;
            out->path[plen++] = *loc++;
        }
        out->path[plen] = '\0';
    } else {
        char tmp_path[TN_HTTP_MAX_PATH];
        const char *last_slash = strrchr(current->path, '/');
        size_t base_len = 0;

        if (last_slash != NULL) {
            base_len = (size_t)(last_slash - current->path + 1);
            if (base_len >= sizeof(tmp_path)) return -1;
            memcpy(tmp_path, current->path, base_len);
        } else {
            tmp_path[0] = '/';
            base_len = 1;
        }

        size_t loc_len = 0;
        while (loc[loc_len] && !is_space(loc[loc_len])) loc_len++;

        if (base_len + loc_len >= sizeof(out->path)) return -1;
        memcpy(out->path, tmp_path, base_len);
        memcpy(out->path + base_len, loc, loc_len);
        out->path[base_len + loc_len] = '\0';
    }

    return 0;
}

int tn_http_parse_headers(const char *hdr_buf, int len, struct TnHdrInfo *out)
{
    const char *p = hdr_buf;
    int end_offset = -1;

    if (!hdr_buf || len <= 0 || !out) return -1;

    memset(out, 0, sizeof(*out));
    out->status_code = -1;
    out->content_length = -1;

    for (int i = 0; i <= len - 4; i++) {
        if (hdr_buf[i] == '\r' && hdr_buf[i+1] == '\n' &&
            hdr_buf[i+2] == '\r' && hdr_buf[i+3] == '\n') {
            end_offset = i + 4;
            break;
        }
    }
    if (end_offset < 0) {
        for (int i = 0; i <= len - 2; i++) {
            if (hdr_buf[i] == '\n' && hdr_buf[i+1] == '\n') {
                end_offset = i + 2;
                break;
            }
        }
    }
    if (end_offset < 0) return -1;

    if (ci_strncmp(p, "HTTP/", 5) == 0) {
        const char *sp = strchr(p, ' ');
        if (sp != NULL && sp < hdr_buf + end_offset) {
            while (*sp == ' ') sp++;
            if (sp[0] >= '0' && sp[0] <= '9' &&
                sp[1] >= '0' && sp[1] <= '9' &&
                sp[2] >= '0' && sp[2] <= '9') {
                out->status_code = (sp[0] - '0') * 100 + (sp[1] - '0') * 10 + (sp[2] - '0');
            }
        }
    }

    const char *line = hdr_buf;
    while (line < hdr_buf + end_offset) {
        const char *eol = strchr(line, '\n');
        if (!eol || eol >= hdr_buf + end_offset) break;

        size_t line_len = (size_t)(eol - line);
        if (line_len > 0 && line[line_len - 1] == '\r') line_len--;

        if (line_len >= 15 && ci_strncmp(line, "content-length:", 15) == 0) {
            const char *val = line + 15;
            while (*val == ' ' || *val == '\t') val++;
            int32_t clen = 0;
            while (*val >= '0' && *val <= '9') {
                clen = clen * 10 + (*val - '0');
                val++;
            }
            out->content_length = clen;
        } else if (line_len >= 18 && ci_strncmp(line, "transfer-encoding:", 18) == 0) {
            const char *val = line + 18;
            while (*val == ' ' || *val == '\t') val++;
            if (ci_strncmp(val, "chunked", 7) == 0) {
                out->is_chunked = 1;
            }
        } else if (line_len >= 9 && ci_strncmp(line, "location:", 9) == 0) {
            const char *val = line + 9;
            while (*val == ' ' || *val == '\t') val++;
            size_t loc_len = 0;
            while (val + loc_len < line + line_len && !is_space(val[loc_len])) {
                if (loc_len < sizeof(out->location) - 1) {
                    out->location[loc_len] = val[loc_len];
                }
                loc_len++;
            }
            out->location[loc_len < sizeof(out->location) ? loc_len : sizeof(out->location) - 1] = '\0';
        } else if (line_len >= 14 && ci_strncmp(line, "accept-ranges:", 14) == 0) {
            const char *val = line + 14;
            while (*val == ' ' || *val == '\t') val++;
            if (ci_strncmp(val, "bytes", 5) == 0) {
                out->accept_ranges = 1;
            }
        }

        line = eol + 1;
    }

    return end_offset;
}

void tn_chunk_init(struct TnChunkState *st)
{
    if (st) {
        st->state = TN_CHUNK_STATE_SIZE;
        st->chunk_size = 0;
        st->chunk_rem = 0;
    }
}

enum TnChunkStateEnum tn_chunk_feed(struct TnChunkState *st,
                                    const char *in, int in_len,
                                    int *consumed,
                                    const char **out_data, int *out_len)
{
    int pos = 0;

    if (!st || !consumed || !out_data || !out_len) return TN_CHUNK_STATE_ERROR;

    *consumed = 0;
    *out_data = NULL;
    *out_len = 0;

    if (in_len <= 0) return st->state;

    while (pos < in_len) {
        if (st->state == TN_CHUNK_STATE_SIZE) {
            char c = in[pos++];
            if (c >= '0' && c <= '9') {
                st->chunk_size = (st->chunk_size << 4) | (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                st->chunk_size = (st->chunk_size << 4) | (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                st->chunk_size = (st->chunk_size << 4) | (c - 'A' + 10);
            } else if (c == '\n') {
                if (st->chunk_size == 0) {
                    st->state = TN_CHUNK_STATE_DONE;
                    *consumed = pos;
                    return st->state;
                }
                st->chunk_rem = st->chunk_size;
                st->state = TN_CHUNK_STATE_DATA;
                break;
            }
        } else if (st->state == TN_CHUNK_STATE_DATA) {
            int avail = in_len - pos;
            int take = (avail < st->chunk_rem) ? avail : st->chunk_rem;

            *out_data = in + pos;
            *out_len = take;
            st->chunk_rem -= take;
            pos += take;

            if (st->chunk_rem == 0) {
                st->state = TN_CHUNK_STATE_CRLF;
            }
            *consumed = pos;
            return st->state;
        } else if (st->state == TN_CHUNK_STATE_CRLF) {
            char c = in[pos++];
            if (c == '\n') {
                st->chunk_size = 0;
                st->state = TN_CHUNK_STATE_SIZE;
            }
        } else if (st->state == TN_CHUNK_STATE_DONE) {
            pos = in_len;
            break;
        } else {
            return TN_CHUNK_STATE_ERROR;
        }
    }

    *consumed = pos;
    return st->state;
}
