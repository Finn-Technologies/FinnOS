/*
 * The browser wire's own rules: a frame header is bounded before anything
 * is allocated for it, free text cannot break a line, and the address bar
 * never hands the engine a scheme it should not load.
 */
#include "finnbrowser.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_frame_parse(void) {
    int tab = 0, w = 0, h = 0;
    assert(finn_browser_frame_parse("FRAME 1 800 600", &tab, &w, &h) == 0);
    assert(tab == 1 && w == 800 && h == 600);
    assert(finn_browser_frame_bytes(w, h) == (size_t)800 * 600 * 4);

    /* Malformed or out of bounds: never a size to allocate. */
    assert(finn_browser_frame_parse("FRAME 0 800 600", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 0 600", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 -5 600", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 8193 10", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 8192 8192", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 4096 4096", &tab, &w, &h) == 0);
    assert(finn_browser_frame_parse("FRAME 1 99999999999999999999 2", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 800 600 ", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME 1 800", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAMES 1 800 600", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse("FRAME +1 800 600", &tab, &w, &h) < 0);
    assert(finn_browser_frame_parse(NULL, &tab, &w, &h) < 0);
    assert(finn_browser_frame_bytes(0, 10) == 0);
    assert(finn_browser_frame_bytes(9000, 10) == 0);

    char line[64];
    int n = finn_browser_frame_header(line, sizeof line, 3, 1280, 720);
    assert(n == (int)strlen("FRAME 3 1280 720\n"));
    assert(strcmp(line, "FRAME 3 1280 720\n") == 0);
    line[n - 1] = '\0';
    assert(finn_browser_frame_parse(line, &tab, &w, &h) == 0);
    assert(tab == 3 && w == 1280 && h == 720);
    assert(finn_browser_frame_header(line, sizeof line, 1, 9000, 9000) < 0);
    assert(finn_browser_frame_header(line, 8, 1, 10, 10) < 0);
}

static void test_text_clean(void) {
    char out[32];
    finn_browser_text_clean(out, sizeof out, "a\nb\rc\td\x7f" "e");
    assert(strcmp(out, "a b c d e") == 0);

    /* A cut never splits a UTF-8 sequence: "é" is two bytes. */
    finn_browser_text_clean(out, 4, "ab\xc3\xa9");
    assert(strcmp(out, "ab") == 0);
    finn_browser_text_clean(out, 5, "ab\xc3\xa9");
    assert(strcmp(out, "ab\xc3\xa9") == 0);

    char same[16] = "x\ny";
    finn_browser_text_clean(same, sizeof same, same);
    assert(strcmp(same, "x y") == 0);

    finn_browser_text_clean(out, sizeof out, NULL);
    assert(out[0] == '\0');
}

static void expect(const char *typed, int want, const char *url) {
    char out[FINN_BROWSER_URL_MAX];
    int got = finn_browser_address(typed, out, sizeof out);
    if (got != want || (url && strcmp(out, url) != 0)) {
        fprintf(stderr, "address(\"%s\") = %d \"%s\", want %d \"%s\"\n",
                typed, got, out, want, url ? url : "");
        assert(0);
    }
}

static void test_address(void) {
    expect("example.com", FINN_BROWSER_ADDR_URL, "https://example.com/");
    expect("  example.com/path?q=1  ", FINN_BROWSER_ADDR_URL,
           "https://example.com/path?q=1");
    expect("example.com:8443", FINN_BROWSER_ADDR_URL, "https://example.com:8443/");
    expect("https://example.com", FINN_BROWSER_ADDR_URL, "https://example.com");
    expect("HTTP://example.com/x", FINN_BROWSER_ADDR_URL, "http://example.com/x");
    expect("localhost:8080/x", FINN_BROWSER_ADDR_URL, "http://localhost:8080/x");
    expect("192.168.1.10", FINN_BROWSER_ADDR_URL, "http://192.168.1.10/");
    expect("about:blank", FINN_BROWSER_ADDR_URL, "about:blank");
    expect("file:///home/finn/a.html", FINN_BROWSER_ADDR_URL,
           "file:///home/finn/a.html");

    /* Not an address: there is no search provider to pretend to use. */
    expect("hello world", FINN_BROWSER_ADDR_NOT_URL, NULL);
    expect("hello", FINN_BROWSER_ADDR_NOT_URL, NULL);
    expect("example.", FINN_BROWSER_ADDR_NOT_URL, NULL);
    expect("example.com:http", FINN_BROWSER_ADDR_INVALID, NULL);

    /* Refused outright. */
    expect("", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("   ", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("javascript:alert(1)", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("data:text/html,hi", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("chrome://settings", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("ftp://example.com", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("https://", FINN_BROWSER_ADDR_INVALID, NULL);
    expect("exa\nmple.com", FINN_BROWSER_ADDR_INVALID, NULL);

    char long_text[FINN_BROWSER_URL_MAX + 8];
    memset(long_text, 'a', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = '\0';
    expect(long_text, FINN_BROWSER_ADDR_INVALID, NULL);

    char small[8];
    assert(finn_browser_address("example.com", small, sizeof small) ==
           FINN_BROWSER_ADDR_INVALID);
}

int main(void) {
    test_frame_parse();
    test_text_clean();
    test_address();
    printf("test_finnbrowser: ok\n");
    return 0;
}
