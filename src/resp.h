#ifndef RESP_H
#define RESP_H

#include <stddef.h> // For size_t

typedef enum {
    RESP_SIMPLE, RESP_ERROR, RESP_INTEGER, RESP_BULK, RESP_ARRAY, RESP_NULL
} resp_type;

typedef struct resp_value {
    resp_type type;
    union {
        struct { char *data; size_t len; } str;
        long long integer;
        struct { struct resp_value **items; size_t n; } array;
    } as;
} resp_value;

typedef enum { RESP_OK, RESP_NEED_MORE, RESP_PROTO_ERR } resp_status;

resp_status resp_parse(const char *buf, size_t len, size_t *consumed, resp_value **out);
void resp_free(resp_value *v);

#endif