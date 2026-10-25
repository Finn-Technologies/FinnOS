#ifndef FINNFBIN_H
#define FINNFBIN_H

#include <stddef.h>
#include <stdint.h>

/*
 * FinnOS Compose's compiled-screen reader. It owns and validates the FBIN
 * bytes and exposes the decoded scene tree to the compositor; it does not
 * start another display server or hand pixels to X11/Wayland.
 */
#define FINN_FBIN_MAX_BYTES (16u * 1024u * 1024u)
#define FINN_FBIN_MAX_NODES 4096u
#define FINN_FBIN_MAX_VALUES 16384u
#define FINN_FBIN_MAX_DEPTH 64u
#define FINN_FBIN_MAX_SECTIONS 256u

typedef struct {
    const uint8_t *data;
    uint16_t len;
} finn_fbin_string;

typedef enum {
    FINN_FBIN_NULL = 0,
    FINN_FBIN_STRING = 1,
    FINN_FBIN_INT = 2,
    FINN_FBIN_NUMBER = 3,
    FINN_FBIN_BOOL = 4,
    FINN_FBIN_LIST = 5,
    FINN_FBIN_MAP = 6,
    FINN_FBIN_COLOUR = 7,
    FINN_FBIN_THEMED = 8,
    FINN_FBIN_ARROW = 9,
    FINN_FBIN_PHRASE = 10,
    FINN_FBIN_IDENT = 11,
    FINN_FBIN_COMBINE = 12,
    FINN_FBIN_CALL = 13
} finn_fbin_value_kind;

typedef struct finn_fbin_value finn_fbin_value;
typedef struct finn_fbin_node finn_fbin_node;

typedef struct {
    finn_fbin_string key;
    finn_fbin_value *value;
} finn_fbin_pair;

struct finn_fbin_value {
    finn_fbin_value_kind kind;
    union {
        finn_fbin_string string;
        int64_t integer;
        double number;
        int boolean;
        uint32_t colour;
        struct { size_t count; finn_fbin_value **items; } list;
        struct { size_t count; finn_fbin_pair *items; } map;
        struct { finn_fbin_string light, dark; } themed;
        struct { finn_fbin_value *left, *right; } pair;
        struct { finn_fbin_string name; finn_fbin_value *args; } call;
    } as;
};

typedef struct {
    finn_fbin_string key;
    finn_fbin_value *value;
} finn_fbin_property_entry;

struct finn_fbin_node {
    uint8_t kind;                 /* 1 screen, 2 element */
    finn_fbin_string name;
    size_t property_count;
    finn_fbin_property_entry *properties;
    size_t argument_count;
    finn_fbin_value **arguments;
    size_t child_count;
    finn_fbin_node **children;
};

typedef struct {
    char name[256];
    const uint8_t *payload;       /* points into document-owned bytes */
    size_t payload_len;
    finn_fbin_node *node;         /* populated for screen/target sections */
} finn_fbin_section;

typedef struct {
    uint8_t *bytes;
    size_t length;
    size_t section_count;
    finn_fbin_section *sections;
} finn_fbin_document;

/* Copies the input; all strings and payloads remain valid until free().
 * Returns 0 on success, -1 on invalid input, allocation failure, or limits.
 */
int finn_fbin_parse(const void *bytes, size_t length, finn_fbin_document *out,
                    char *error, size_t error_cap);
void finn_fbin_free(finn_fbin_document *document);
const finn_fbin_section *finn_fbin_find(const finn_fbin_document *document,
                                        const char *name);
const finn_fbin_value *finn_fbin_property(const finn_fbin_node *node,
                                          const char *key);

#endif
