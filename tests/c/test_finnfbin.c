#include "finnfbin.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t data[4096];
    size_t len;
} bytes;

static void put(bytes *b, uint8_t value) {
    assert(b->len < sizeof b->data);
    b->data[b->len++] = value;
}

static void put_u16(bytes *b, uint16_t value) {
    put(b, (uint8_t)value);
    put(b, (uint8_t)(value >> 8));
}

static void put_u32(bytes *b, uint32_t value) {
    for (int i = 0; i < 4; i++) put(b, (uint8_t)(value >> (i * 8)));
}

static void put_string(bytes *b, const char *value) {
    size_t len = strlen(value);
    assert(len <= UINT16_MAX);
    put_u16(b, (uint16_t)len);
    for (size_t i = 0; i < len; i++) put(b, (uint8_t)value[i]);
}

static void put_node(bytes *b, uint8_t kind, const char *name,
                     uint16_t args, uint16_t children) {
    put(b, kind);
    put_string(b, name);
    put_u16(b, 0);             /* properties */
    put_u16(b, args);
    if (args) {
        put(b, FINN_FBIN_STRING);
        put_string(b, "Hello FinnOS");
    }
    put_u16(b, children);
}

static bytes sample(void) {
    bytes node = {0}, body = {0}, file = {0};
    put_node(&node, 1, "Notes", 0, 1);
    put_node(&node, 2, "Column", 0, 1);
    put_node(&node, 2, "Text", 1, 0);

    put_u16(&body, 1);
    put(&body, 6);
    for (const char *p = "screen"; *p; p++) put(&body, (uint8_t)*p);
    put_u32(&body, (uint32_t)node.len);
    for (size_t i = 0; i < node.len; i++) put(&body, node.data[i]);

    put(&file, 'F'); put(&file, 'B'); put(&file, 'I'); put(&file, 'N');
    put_u16(&file, 1);
    put_u16(&file, 0);
    put_u32(&file, (uint32_t)body.len);
    for (size_t i = 0; i < body.len; i++) put(&file, body.data[i]);
    return file;
}

static void rejects(const uint8_t *data, size_t len, const char *message) {
    finn_fbin_document doc;
    char error[128];
    assert(finn_fbin_parse(data, len, &doc, error, sizeof error) != 0);
    assert(error[0] != '\0');
    if (message) assert(strstr(error, message) != NULL);
    finn_fbin_free(&doc);
}

static void accepts_compiler_output(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    assert(size >= 0 && size <= (long)FINN_FBIN_MAX_BYTES);
    assert(fseek(f, 0, SEEK_SET) == 0);
    uint8_t *data = malloc((size_t)size);
    assert(data != NULL);
    assert(fread(data, 1, (size_t)size, f) == (size_t)size);
    assert(fclose(f) == 0);

    finn_fbin_document doc;
    char error[128];
    if (finn_fbin_parse(data, (size_t)size, &doc, error, sizeof error) != 0) {
        fprintf(stderr, "compiler output rejected: %s\\n", error);
        abort();
    }
    const finn_fbin_section *screen = finn_fbin_find(&doc, "screen");
    assert(screen != NULL && screen->node != NULL && screen->node->kind == 1);
    finn_fbin_free(&doc);
    free(data);
    puts("finnfbin: FinnUI compiler output accepted");
}

int main(int argc, char **argv) {
    bytes file = sample();
    finn_fbin_document doc;
    char error[128];
    assert(finn_fbin_parse(file.data, file.len, &doc, error, sizeof error) == 0);
    assert(doc.length == file.len);
    assert(doc.section_count == 1);

    const finn_fbin_section *screen = finn_fbin_find(&doc, "screen");
    assert(screen && screen->node);
    assert(screen->node->kind == 1);
    assert(screen->node->name.len == 5);
    assert(memcmp(screen->node->name.data, "Notes", 5) == 0);
    assert(screen->node->child_count == 1);
    const finn_fbin_node *column = screen->node->children[0];
    assert(column->kind == 2 && column->child_count == 1);
    const finn_fbin_node *text = column->children[0];
    assert(text->argument_count == 1);
    assert(text->arguments[0]->kind == FINN_FBIN_STRING);
    assert(text->arguments[0]->as.string.len == 12);
    assert(memcmp(text->arguments[0]->as.string.data, "Hello FinnOS", 12) == 0);
    assert(finn_fbin_property(text, "style") == NULL);
    finn_fbin_free(&doc);

    uint8_t bad[4096];
    memcpy(bad, file.data, file.len);
    bad[0] = 'X';
    rejects(bad, file.len, "not an FBIN");

    memcpy(bad, file.data, file.len);
    bad[4] = 2;
    rejects(bad, file.len, "unsupported FBIN version");

    memcpy(bad, file.data, file.len);
    bad[6] = 1;
    rejects(bad, file.len, "unsupported FBIN flags");

    memcpy(bad, file.data, file.len);
    bad[8]++;
    rejects(bad, file.len, "body length");

    rejects(file.data, file.len - 1, "body length");

    memcpy(bad, file.data, file.len);
    size_t text_offset = 0;
    for (size_t i = 0; i + 12 <= file.len; i++) {
        if (memcmp(file.data + i, "Hello FinnOS", 12) == 0) {
            text_offset = i;
            break;
        }
    }
    assert(text_offset >= 3);
    bad[text_offset - 3] = 0xFF; /* unknown value kind before the string */
    rejects(bad, file.len, "unknown FBIN value kind");

    memcpy(bad, file.data, file.len);
    bad[text_offset] = 0xC0;     /* overlong UTF-8 lead byte */
    rejects(bad, file.len, "invalid UTF-8");

    uint8_t trailing[4096];
    memcpy(trailing, file.data, file.len);
    trailing[file.len] = 0;
    trailing[8]++;
    rejects(trailing, file.len + 1, "trailing data");

    finn_fbin_document empty;
    assert(finn_fbin_parse(NULL, 0, &empty, error, sizeof error) != 0);
    assert(error[0] != '\0');

    puts("finnfbin: bounded screen decoding and malformed-input checks passed");
    if (argc > 1) accepts_compiler_output(argv[1]);
    return 0;
}
