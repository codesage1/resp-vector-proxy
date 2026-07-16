// src/registry.c
#include "registry.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_INDEXES 100
index_schema registry[MAX_INDEXES];
int registry_count = 0;

int registry_observe_ftcreate(const resp_value *cmd) {
    if (cmd->type != RESP_ARRAY || cmd->as.array.n < 4) return 0;
    if (registry_count >= MAX_INDEXES) return 0; // Protect array bounds

    // Extract Index Name (items[1])
    resp_value *idx_val = cmd->as.array.items[1];
    if (idx_val->type != RESP_BULK) return 0;

    // Create a new schema slot in our fixed array
    index_schema *schema = &registry[registry_count];

    // Heap allocation: Copy the index name safely off the TCP buffer
    schema->index = strndup(idx_val->as.str.data, idx_val->as.str.len);

    // The Stateful Scanner
    for (size_t i = 2; i < cmd->as.array.n; i++) {
        resp_value *item = cmd->as.array.items[i];
        if (item->type != RESP_BULK) continue; // Always type-check!

        char *val = item->as.str.data;
        size_t len = item->as.str.len;

        // 1. Check for "PREFIX" (Needs i+2 bounds check! PREFIX 1 docs:)
        if (len == 6 && strncasecmp("PREFIX", val, 6) == 0) {
            if (i + 2 < cmd->as.array.n) {
                resp_value *prefix_val = cmd->as.array.items[i+2];
                if (prefix_val->type == RESP_BULK) {
                    // PHOTOCOPY off the TCP buffer
                    schema->prefix = strndup(prefix_val->as.str.data, prefix_val->as.str.len);
                }
            }
        }
        
        // 2. Check for "SCHEMA" (Marker only)
        if (len == 6 && strncasecmp("SCHEMA", val, 6) == 0) {
            continue; 
        }

        // 3. Check for "VECTOR" (Field name is at i-1)
        if (len == 6 && strncasecmp("VECTOR", val, 6) == 0) {
            if (i >= 1) { // Ensure we don't underflow backwards
                resp_value *vec_val = cmd->as.array.items[i-1];
                if (vec_val->type == RESP_BULK) {
                    schema->vec_field = strndup(vec_val->as.str.data, vec_val->as.str.len);
                }
            }
        }

        // 4. Check for "TYPE" -> Map to enum
        if (len == 4 && strncasecmp("TYPE", val, 4) == 0) {
            if (i + 1 < cmd->as.array.n) {
                resp_value *type_val = cmd->as.array.items[i+1];
                if (type_val->type == RESP_BULK) {
                    if (strncasecmp("FLOAT32", type_val->as.str.data, 7) == 0) {
                        schema->type = VT_FLOAT32;
                    } else if (strncasecmp("FLOAT64", type_val->as.str.data, 7) == 0) {
                        schema->type = VT_FLOAT64;
                    }
                }
            }
        }

        // 5. Check for "DIM" -> Convert to integer
        if (len == 3 && strncasecmp("DIM", val, 3) == 0) {
            if (i + 1 < cmd->as.array.n) {
                resp_value *dim_val = cmd->as.array.items[i+1];
                if (dim_val->type == RESP_BULK) {
                    // Safely isolate the string, convert, and free
                    char *tmp = strndup(dim_val->as.str.data, dim_val->as.str.len);
                    schema->dim = strtoll(tmp, NULL, 10);
                    free(tmp);
                }
            }
        }

        // 6. Check for "DISTANCE_METRIC" -> Map to enum
        if (len == 15 && strncasecmp("DISTANCE_METRIC", val, 15) == 0) {
            if (i + 1 < cmd->as.array.n) {
                resp_value *metric_val = cmd->as.array.items[i+1];
                if (metric_val->type == RESP_BULK) {
                    if (strncasecmp("COSINE", metric_val->as.str.data, 6) == 0) {
                        schema->metric = M_COSINE;
                    } else if (strncasecmp("L2", metric_val->as.str.data, 2) == 0) {
                        schema->metric = M_L2;
                    }
                }
            }
        }
    }

    registry_count++;
    printf("✅ Registry learned index: %s\n", schema->index);
    return 1;
}