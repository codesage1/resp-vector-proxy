// src/shadow_store.h
#include "stdlib.h"

typedef struct {
    char *field;       // The name of the field (e.g., "category" or "doc_embedding")
    char *val_data;    // Pointer to the raw data blob on the heap
    size_t val_len;    // The absolute byte length (Crucial because vector blobs contain null bytes!)
} document_field;

typedef struct {
    char *key;                 // The unique document key (e.g., "docs:1")
    document_field *fields;    // Dynamic array of field-value pairs
    size_t field_count;        // How many fields this document currently holds
} shadow_document;