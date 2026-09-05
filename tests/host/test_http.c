/*
 * test_http.c — Host-side HTTP URL and Header Parser Tests (TNET-075).
 * Unit: src/common/http_url.c
 */

#include "tn_test.h"
#include "../../src/common/http_url.h"
#include <string.h>

TN_TEST(url_basic_http)
{
    struct TnUrl u;
    TN_ASSERT_EQ(tn_http_parse_url("http://aminet.net/recent.txt", &u), 0);
    TN_ASSERT_EQ(u.scheme, TN_SCHEME_HTTP);
    TN_ASSERT_STREQ(u.host, "aminet.net");
    TN_ASSERT_EQ_U(u.port, 80u);
    TN_ASSERT_STREQ(u.path, "/recent.txt");
}

TN_TEST(url_custom_port_and_path)
{
    struct TnUrl u;
    TN_ASSERT_EQ(tn_http_parse_url("http://10.0.2.2:8080/path/to/test.dat", &u), 0);
    TN_ASSERT_EQ(u.scheme, TN_SCHEME_HTTP);
    TN_ASSERT_STREQ(u.host, "10.0.2.2");
    TN_ASSERT_EQ_U(u.port, 8080u);
    TN_ASSERT_STREQ(u.path, "/path/to/test.dat");
}

TN_TEST(url_https_detect)
{
    struct TnUrl u;
    TN_ASSERT_EQ(tn_http_parse_url("https://aminet.net/index.html", &u), 0);
    TN_ASSERT_EQ(u.scheme, TN_SCHEME_HTTPS);
    TN_ASSERT_STREQ(u.host, "aminet.net");
}

TN_TEST(url_no_scheme_default)
{
    struct TnUrl u;
    TN_ASSERT_EQ(tn_http_parse_url("aminet.net", &u), 0);
    TN_ASSERT_EQ(u.scheme, TN_SCHEME_HTTP);
    TN_ASSERT_STREQ(u.host, "aminet.net");
    TN_ASSERT_EQ_U(u.port, 80u);
    TN_ASSERT_STREQ(u.path, "/");

    TN_ASSERT_EQ(tn_http_parse_url("192.168.1.1:8000/api", &u), 0);
    TN_ASSERT_STREQ(u.host, "192.168.1.1");
    TN_ASSERT_EQ_U(u.port, 8000u);
    TN_ASSERT_STREQ(u.path, "/api");
}

TN_TEST(redirect_resolutions)
{
    struct TnUrl curr, next;
    TN_ASSERT_EQ(tn_http_parse_url("http://aminet.net/mods/index.html", &curr), 0);

    /* Absolute URL redirect */
    TN_ASSERT_EQ(tn_http_resolve_redirect(&curr, "http://mirror.aminet.net/mods/index.html", &next), 0);
    TN_ASSERT_STREQ(next.host, "mirror.aminet.net");
    TN_ASSERT_STREQ(next.path, "/mods/index.html");

    /* Absolute path redirect */
    TN_ASSERT_EQ(tn_http_resolve_redirect(&curr, "/recent.txt", &next), 0);
    TN_ASSERT_STREQ(next.host, "aminet.net");
    TN_ASSERT_STREQ(next.path, "/recent.txt");

    /* Relative path redirect */
    TN_ASSERT_EQ(tn_http_resolve_redirect(&curr, "new_mods.html", &next), 0);
    TN_ASSERT_STREQ(next.host, "aminet.net");
    TN_ASSERT_STREQ(next.path, "/mods/new_mods.html");

    /* HTTPS redirect */
    TN_ASSERT_EQ(tn_http_resolve_redirect(&curr, "https://aminet.net/secure", &next), 0);
    TN_ASSERT_EQ(next.scheme, TN_SCHEME_HTTPS);
}

TN_TEST(headers_200_ok)
{
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Date: Sun, 06 Sep 2026 00:00:00 GMT\r\n"
        "Server: Apache\r\n"
        "Content-Length: 1048576\r\n"
        "Accept-Ranges: bytes\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Body starts here";

    struct TnHdrInfo info;
    int body_offset = tn_http_parse_headers(resp, (int)strlen(resp), &info);

    TN_ASSERT_TRUE(body_offset > 0);
    TN_ASSERT_EQ(info.status_code, 200);
    TN_ASSERT_EQ(info.content_length, 1048576);
    TN_ASSERT_EQ(info.accept_ranges, 1);
    TN_ASSERT_EQ(info.is_chunked, 0);
    TN_ASSERT_STREQ(resp + body_offset, "Body starts here");
}

TN_TEST(headers_302_redirect)
{
    const char *resp =
        "HTTP/1.1 302 Found\r\n"
        "Location: http://aminet.net/recent.txt\r\n"
        "Content-Length: 0\r\n"
        "\r\n";

    struct TnHdrInfo info;
    int body_offset = tn_http_parse_headers(resp, (int)strlen(resp), &info);

    TN_ASSERT_TRUE(body_offset > 0);
    TN_ASSERT_EQ(info.status_code, 302);
    TN_ASSERT_STREQ(info.location, "http://aminet.net/recent.txt");
}

TN_TEST(headers_chunked)
{
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n";

    struct TnHdrInfo info;
    int body_offset = tn_http_parse_headers(resp, (int)strlen(resp), &info);

    TN_ASSERT_TRUE(body_offset > 0);
    TN_ASSERT_EQ(info.is_chunked, 1);
}

TN_TEST(chunked_decoding)
{
    const char *stream = "4\r\nWiki\r\n5\r\npedia\r\n0\r\n\r\n";
    struct TnChunkState st;
    tn_chunk_init(&st);

    char decoded[32];
    int dec_len = 0;
    int pos = 0;
    int stream_len = (int)strlen(stream);

    while (pos < stream_len && st.state != TN_CHUNK_STATE_DONE) {
        int consumed = 0;
        const char *data = NULL;
        int len = 0;

        enum TnChunkStateEnum s = tn_chunk_feed(&st, stream + pos, stream_len - pos,
                                                &consumed, &data, &len);
        pos += consumed;
        if (len > 0) {
            memcpy(decoded + dec_len, data, len);
            dec_len += len;
        }
        if (s == TN_CHUNK_STATE_ERROR) {
            TN_ASSERT_TRUE(0);
        }
    }

    decoded[dec_len] = '\0';
    TN_ASSERT_EQ((int)st.state, (int)TN_CHUNK_STATE_DONE);
    TN_ASSERT_STREQ(decoded, "Wikipedia");
}

int main(void)
{
    TN_TEST_RUN(url_basic_http);
    TN_TEST_RUN(url_custom_port_and_path);
    TN_TEST_RUN(url_https_detect);
    TN_TEST_RUN(url_no_scheme_default);
    TN_TEST_RUN(redirect_resolutions);
    TN_TEST_RUN(headers_200_ok);
    TN_TEST_RUN(headers_302_redirect);
    TN_TEST_RUN(headers_chunked);
    TN_TEST_RUN(chunked_decoding);
    TN_TEST_PLAN();
    return tn_test_failures();
}
