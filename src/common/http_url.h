/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Tolunnet — HTTP URL and Header Parser Interface
 *
 * Pure C unit for URL parsing, redirect resolution, header analysis,
 * and chunked transfer decoding. Host-compilable and strictly typed.
 */

#ifndef TOLUNNET_HTTP_URL_H
#define TOLUNNET_HTTP_URL_H

#include <stdint.h>

#define TN_HTTP_MAX_HOST  128
#define TN_HTTP_MAX_PATH  256
#define TN_HTTP_MAX_LOC   256

#define TN_SCHEME_NONE   0
#define TN_SCHEME_HTTP   1
#define TN_SCHEME_HTTPS  2

struct TnUrl {
    int      scheme;                      /* TN_SCHEME_HTTP, TN_SCHEME_HTTPS, or TN_SCHEME_NONE */
    char     host[TN_HTTP_MAX_HOST];
    uint32_t port;                        /* default 80 */
    char     path[TN_HTTP_MAX_PATH];      /* default "/" */
};

struct TnHdrInfo {
    int32_t  status_code;                 /* 200, 301, 302, 404, etc. -1 if not parsed */
    int32_t  content_length;              /* -1 if not specified */
    int      is_chunked;                  /* 1 if Transfer-Encoding: chunked, else 0 */
    int      accept_ranges;               /* 1 if Accept-Ranges: bytes, else 0 */
    char     location[TN_HTTP_MAX_LOC];   /* extracted Location header or empty */
};

enum TnChunkStateEnum {
    TN_CHUNK_STATE_SIZE = 0,
    TN_CHUNK_STATE_DATA = 1,
    TN_CHUNK_STATE_CRLF = 2,
    TN_CHUNK_STATE_DONE = 3,
    TN_CHUNK_STATE_ERROR = 4
};

struct TnChunkState {
    enum TnChunkStateEnum state;
    int32_t chunk_size;
    int32_t chunk_rem;
};

int tn_http_parse_url(const char *url_str, struct TnUrl *out);
int tn_http_resolve_redirect(const struct TnUrl *current, const char *location, struct TnUrl *out);
int tn_http_parse_headers(const char *hdr_buf, int len, struct TnHdrInfo *out);
void tn_chunk_init(struct TnChunkState *st);
enum TnChunkStateEnum tn_chunk_feed(struct TnChunkState *st,
                                    const char *in, int in_len,
                                    int *consumed,
                                    const char **out_data, int *out_len);

#endif /* TOLUNNET_HTTP_URL_H */
