/* Minimal JSON reader/writer for the chat protocol. C99, no malloc: the
 * caller hands a parse an arena (a flat buffer) that holds the node tree
 * and the decoded strings. Good for packets up to PROTO_MAX_PACKET. */
#ifndef AC_JSON_H
#define AC_JSON_H

#include <stddef.h>

typedef enum { J_NULL, J_BOOL, J_NUMBER, J_STRING, J_ARRAY, J_OBJECT } json_type;

typedef struct json_val {
    json_type type;
    const char *key;        /* member name when inside an object, else NULL */
    const char *str;        /* J_STRING: decoded UTF-8, NUL-terminated */
    size_t len;             /* J_STRING: byte length; J_ARRAY/J_OBJECT: child count */
    double num;             /* J_NUMBER */
    long long inum;         /* J_NUMBER when it is an integer that fits */
    int is_int;
    int boolean;            /* J_BOOL */
    struct json_val *first; /* first child (arrays and objects) */
    struct json_val *next;  /* next sibling */
} json_val;

typedef struct {
    char *mem;
    size_t cap;
    size_t used;
} json_arena;

void json_arena_init(json_arena *a, char *mem, size_t cap);

/* Parse text[0..n). Returns the root or NULL on a syntax error or when the
 * arena is too small. Strings are copied, decoded, into the arena. */
json_val *json_parse(const char *text, size_t n, json_arena *a);

/* Lookups that tolerate NULL and wrong types. */
const json_val *json_get(const json_val *obj, const char *key);
const json_val *json_index(const json_val *arr, size_t i);
const char *json_str(const json_val *v, const char *dflt);
long long json_int(const json_val *v, long long dflt);

/* Writer helpers. Each returns the number of bytes it wanted to write, like
 * snprintf, and never writes past cap. json_escape writes the quoted,
 * escaped string. The input is UTF-8; invalid bytes become '?'. */
size_t json_escape(const char *s, size_t n, char *out, size_t cap);

#endif
