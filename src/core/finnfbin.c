/* finnfbin — bounded reader for FinnOS Compose's compiled scene files. */
#include "finnfbin.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FBIN_HEADER_SIZE 12u
#define FBIN_VERSION 1u
#define FBIN_VALUE_LIMIT 4096u

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
    size_t nodes;
    size_t values;
    char *error;
    size_t error_cap;
} reader;

static int fail(reader *r, const char *message) {
    if (r->error && r->error_cap && !r->error[0])
        snprintf(r->error, r->error_cap, "%s", message);
    return -1;
}

static int take(reader *r, size_t count, const uint8_t **out) {
    if (count > r->length - r->offset) return fail(r, "truncated FBIN data");
    if (out) *out = r->data + r->offset;
    r->offset += count;
    return 0;
}

static int read_u8(reader *r, uint8_t *out) {
    const uint8_t *p;
    if (take(r, 1, &p) != 0) return -1;
    *out = p[0];
    return 0;
}

static int read_u16(reader *r, uint16_t *out) {
    const uint8_t *p;
    if (take(r, 2, &p) != 0) return -1;
    *out = (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
    return 0;
}

static int read_u32(reader *r, uint32_t *out) {
    const uint8_t *p;
    if (take(r, 4, &p) != 0) return -1;
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 0;
}

static int read_u64(reader *r, uint64_t *out) {
    const uint8_t *p;
    if (take(r, 8, &p) != 0) return -1;
    *out = (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    return 0;
}

static int valid_utf8(const uint8_t *s, size_t length) {
    for (size_t i = 0; i < length;) {
        uint8_t a = s[i++];
        if (a < 0x80) continue;

        size_t extra;
        uint32_t code;
        if (a >= 0xC2 && a <= 0xDF) { extra = 1; code = a & 0x1Fu; }
        else if (a >= 0xE0 && a <= 0xEF) { extra = 2; code = a & 0x0Fu; }
        else if (a >= 0xF0 && a <= 0xF4) { extra = 3; code = a & 0x07u; }
        else return 0;
        if (extra > length - i) return 0;

        for (size_t k = 0; k < extra; k++) {
            uint8_t b = s[i++];
            if ((b & 0xC0u) != 0x80u) return 0;
            code = (code << 6) | (uint32_t)(b & 0x3Fu);
        }
        if ((extra == 2 && code < 0x800u) ||
            (extra == 3 && code < 0x10000u) ||
            (code >= 0xD800u && code <= 0xDFFFu) || code > 0x10FFFFu)
            return 0;
    }
    return 1;
}

static int read_string(reader *r, finn_fbin_string *out) {
    uint16_t len;
    const uint8_t *p;
    if (read_u16(r, &len) != 0 || take(r, len, &p) != 0) return -1;
    if (!valid_utf8(p, len)) return fail(r, "invalid UTF-8 in FBIN string");
    out->data = p;
    out->len = len;
    return 0;
}

static int read_count(reader *r, uint16_t *out) {
    if (read_u16(r, out) != 0) return -1;
    if (*out > FBIN_VALUE_LIMIT) return fail(r, "FBIN collection exceeds limit");
    return 0;
}

static void free_value(finn_fbin_value *v);

static void free_node(finn_fbin_node *node) {
    if (!node) return;
    for (size_t i = 0; i < node->property_count; i++)
        free_value(node->properties[i].value);
    for (size_t i = 0; i < node->argument_count; i++)
        free_value(node->arguments[i]);
    for (size_t i = 0; i < node->child_count; i++)
        free_node(node->children[i]);
    free(node->properties);
    free(node->arguments);
    free(node->children);
    free(node);
}

static void free_value(finn_fbin_value *v) {
    if (!v) return;
    switch (v->kind) {
    case FINN_FBIN_LIST:
    case FINN_FBIN_PHRASE:
        for (size_t i = 0; i < v->as.list.count; i++)
            free_value(v->as.list.items[i]);
        free(v->as.list.items);
        break;
    case FINN_FBIN_MAP:
        for (size_t i = 0; i < v->as.map.count; i++)
            free_value(v->as.map.items[i].value);
        free(v->as.map.items);
        break;
    case FINN_FBIN_ARROW:
    case FINN_FBIN_COMBINE:
        free_value(v->as.pair.left);
        free_value(v->as.pair.right);
        break;
    case FINN_FBIN_CALL:
        free_value(v->as.call.args);
        break;
    default:
        break;
    }
    free(v);
}

static int parse_value(reader *r, finn_fbin_value **out, size_t depth) {
    uint8_t kind;
    if (depth > FINN_FBIN_MAX_DEPTH) return fail(r, "FBIN value nesting exceeds limit");
    if (r->values >= FINN_FBIN_MAX_VALUES) return fail(r, "FBIN value count exceeds limit");
    if (read_u8(r, &kind) != 0) return -1;

    finn_fbin_value *v = calloc(1, sizeof *v);
    if (!v) return fail(r, "out of memory reading FBIN value");
    v->kind = (finn_fbin_value_kind)kind;
    r->values++;

    int status = 0;
    uint16_t count = 0;
    uint8_t byte = 0;
    uint32_t colour = 0;
    uint64_t bits = 0;
    switch (kind) {
    case FINN_FBIN_NULL:
        break;
    case FINN_FBIN_STRING:
    case FINN_FBIN_IDENT:
        status = read_string(r, &v->as.string);
        break;
    case FINN_FBIN_INT:
        status = read_u64(r, &bits);
        if (status == 0) {
            if (bits <= INT64_MAX) v->as.integer = (int64_t)bits;
            else v->as.integer = -1 - (int64_t)(UINT64_MAX - bits);
        }
        break;
    case FINN_FBIN_NUMBER:
        status = read_u64(r, &bits);
        if (status == 0) {
            memcpy(&v->as.number, &bits, sizeof bits);
            if (!isfinite(v->as.number)) status = fail(r, "non-finite FBIN number");
        }
        break;
    case FINN_FBIN_BOOL:
        status = read_u8(r, &byte);
        if (status == 0) v->as.boolean = byte != 0;
        break;
    case FINN_FBIN_COLOUR:
        status = read_u32(r, &colour);
        if (status == 0) v->as.colour = colour;
        break;
    case FINN_FBIN_THEMED:
        status = read_string(r, &v->as.themed.light);
        if (status == 0) status = read_string(r, &v->as.themed.dark);
        break;
    case FINN_FBIN_LIST:
    case FINN_FBIN_PHRASE:
        status = read_count(r, &count);
        if (status == 0 && count) {
            v->as.list.items = calloc(count, sizeof *v->as.list.items);
            if (!v->as.list.items) status = fail(r, "out of memory reading FBIN list");
        }
        if (status == 0) {
            v->as.list.count = count;
            for (size_t i = 0; i < count && status == 0; i++)
                status = parse_value(r, &v->as.list.items[i], depth + 1);
        }
        break;
    case FINN_FBIN_MAP:
        status = read_count(r, &count);
        if (status == 0 && count) {
            v->as.map.items = calloc(count, sizeof *v->as.map.items);
            if (!v->as.map.items) status = fail(r, "out of memory reading FBIN map");
        }
        if (status == 0) {
            v->as.map.count = count;
            for (size_t i = 0; i < count && status == 0; i++) {
                status = read_string(r, &v->as.map.items[i].key);
                if (status == 0)
                    status = parse_value(r, &v->as.map.items[i].value, depth + 1);
            }
        }
        break;
    case FINN_FBIN_ARROW:
    case FINN_FBIN_COMBINE:
        status = parse_value(r, &v->as.pair.left, depth + 1);
        if (status == 0) status = parse_value(r, &v->as.pair.right, depth + 1);
        break;
    case FINN_FBIN_CALL:
        status = read_string(r, &v->as.call.name);
        if (status == 0) status = parse_value(r, &v->as.call.args, depth + 1);
        break;
    default:
        status = fail(r, "unknown FBIN value kind");
        break;
    }

    if (status != 0) {
        free_value(v);
        return -1;
    }
    *out = v;
    return 0;
}

static int parse_node(reader *r, finn_fbin_node **out, size_t depth) {
    uint16_t count;
    uint8_t kind;
    if (depth > FINN_FBIN_MAX_DEPTH) return fail(r, "FBIN tree nesting exceeds limit");
    if (r->nodes >= FINN_FBIN_MAX_NODES) return fail(r, "FBIN node count exceeds limit");
    if (read_u8(r, &kind) != 0) return -1;
    if (kind != 1 && kind != 2) return fail(r, "unknown FBIN node kind");

    finn_fbin_node *node = calloc(1, sizeof *node);
    if (!node) return fail(r, "out of memory reading FBIN node");
    node->kind = kind;
    r->nodes++;

    int status = read_string(r, &node->name);
    if (status == 0) status = read_count(r, &count);
    if (status == 0 && count) {
        node->properties = calloc(count, sizeof *node->properties);
        if (!node->properties) status = fail(r, "out of memory reading FBIN properties");
    }
    if (status == 0) {
        node->property_count = count;
        for (size_t i = 0; i < count && status == 0; i++) {
            status = read_string(r, &node->properties[i].key);
            if (status == 0)
                status = parse_value(r, &node->properties[i].value, depth + 1);
        }
    }

    if (status == 0) status = read_count(r, &count);
    if (status == 0 && count) {
        node->arguments = calloc(count, sizeof *node->arguments);
        if (!node->arguments) status = fail(r, "out of memory reading FBIN arguments");
    }
    if (status == 0) {
        node->argument_count = count;
        for (size_t i = 0; i < count && status == 0; i++)
            status = parse_value(r, &node->arguments[i], depth + 1);
    }

    if (status == 0) status = read_count(r, &count);
    if (status == 0 && count) {
        node->children = calloc(count, sizeof *node->children);
        if (!node->children) status = fail(r, "out of memory reading FBIN children");
    }
    if (status == 0) {
        node->child_count = count;
        for (size_t i = 0; i < count && status == 0; i++)
            status = parse_node(r, &node->children[i], depth + 1);
    }

    if (status != 0) {
        free_node(node);
        return -1;
    }
    *out = node;
    return 0;
}

void finn_fbin_free(finn_fbin_document *document) {
    if (!document) return;
    for (size_t i = 0; i < document->section_count; i++)
        free_node(document->sections[i].node);
    free(document->sections);
    free(document->bytes);
    memset(document, 0, sizeof *document);
}

int finn_fbin_parse(const void *bytes, size_t length, finn_fbin_document *out,
                    char *error, size_t error_cap) {
    if (error && error_cap) error[0] = '\0';
    if (!out) {
        if (error && error_cap) snprintf(error, error_cap, "missing FBIN output");
        return -1;
    }
    memset(out, 0, sizeof *out);
    reader r = { .error = error, .error_cap = error_cap };
    if (!bytes || length < FBIN_HEADER_SIZE || length > FINN_FBIN_MAX_BYTES) {
        if (error && error_cap) snprintf(error, error_cap, "FBIN length is invalid");
        return -1;
    }

    out->bytes = malloc(length);
    if (!out->bytes) {
        if (error && error_cap) snprintf(error, error_cap, "out of memory reading FBIN");
        return -1;
    }
    memcpy(out->bytes, bytes, length);
    out->length = length;
    r.data = out->bytes;
    r.length = length;

    const uint8_t *magic;
    uint16_t version, flags, section_count;
    uint32_t body_length;
    if (take(&r, 4, &magic) != 0 || memcmp(magic, "FBIN", 4) != 0) {
        fail(&r, "not an FBIN file");
        goto bad;
    }
    if (read_u16(&r, &version) != 0 || version != FBIN_VERSION) {
        fail(&r, "unsupported FBIN version");
        goto bad;
    }
    if (read_u16(&r, &flags) != 0 || flags != 0) {
        fail(&r, "unsupported FBIN flags");
        goto bad;
    }
    if (read_u32(&r, &body_length) != 0 || body_length != length - FBIN_HEADER_SIZE) {
        fail(&r, "FBIN body length does not match file");
        goto bad;
    }
    if (read_u16(&r, &section_count) != 0 || section_count > FINN_FBIN_MAX_SECTIONS) {
        fail(&r, "FBIN section count exceeds limit");
        goto bad;
    }
    if (section_count) {
        out->sections = calloc(section_count, sizeof *out->sections);
        if (!out->sections) {
            fail(&r, "out of memory reading FBIN sections");
            goto bad;
        }
    }
    out->section_count = section_count;

    for (size_t i = 0; i < section_count; i++) {
        uint8_t name_len;
        uint32_t payload_len;
        const uint8_t *name, *payload;
        if (read_u8(&r, &name_len) != 0 || name_len == 0 ||
            take(&r, name_len, &name) != 0 || memchr(name, '\0', name_len) ||
            !valid_utf8(name, name_len) || read_u32(&r, &payload_len) != 0 ||
            take(&r, payload_len, &payload) != 0) {
            if (!error || !error_cap || !error[0]) fail(&r, "invalid FBIN section");
            goto bad;
        }
        finn_fbin_section *section = &out->sections[i];
        memcpy(section->name, name, name_len);
        section->name[name_len] = '\0';
        section->payload = payload;
        section->payload_len = payload_len;

        if (!strcmp(section->name, "screen") ||
            !strncmp(section->name, "screen:", 7) ||
            !strncmp(section->name, "target:", 7)) {
            reader node_reader = { .data = payload, .length = payload_len,
                                   .nodes = r.nodes, .values = r.values,
                                   .error = error, .error_cap = error_cap };
            if (!payload_len || parse_node(&node_reader, &section->node, 0) != 0 ||
                node_reader.offset != node_reader.length) {
                if (error && error_cap && !error[0])
                    snprintf(error, error_cap, "invalid FBIN scene section");
                goto bad;
            }
            r.nodes = node_reader.nodes;
            r.values = node_reader.values;
        }
    }
    if (r.offset != r.length) {
        fail(&r, "trailing data after FBIN sections");
        goto bad;
    }
    return 0;

bad:
    finn_fbin_free(out);
    return -1;
}

const finn_fbin_section *finn_fbin_find(const finn_fbin_document *document,
                                        const char *name) {
    if (!document || !name) return NULL;
    for (size_t i = 0; i < document->section_count; i++)
        if (!strcmp(document->sections[i].name, name)) return &document->sections[i];
    return NULL;
}

const finn_fbin_value *finn_fbin_property(const finn_fbin_node *node,
                                          const char *key) {
    if (!node || !key) return NULL;
    size_t length = strlen(key);
    for (size_t i = 0; i < node->property_count; i++) {
        const finn_fbin_string *candidate = &node->properties[i].key;
        if (candidate->len == length && !memcmp(candidate->data, key, length))
            return node->properties[i].value;
    }
    return NULL;
}
