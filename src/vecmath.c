// src/vecmath.c (continued)
#include "shadow_store.h"
#include "registry.h"
#include <stdlib.h>
#include <string.h>

#define MAX_DOCS 1000

typedef struct {
    const char *key;
    double distance;
} candidate_match;

// 1. The qsort comparison helper: Sort ascending by distance
static int compare_candidates(const void *a, const void *b) {
    double dist_a = ((candidate_match *)a)->distance;
    double dist_b = ((candidate_match *)b)->distance;
    
    if (dist_a < dist_b) return -1;
    if (dist_a > dist_b) return 1;
    return 0;
}

// Global references to your storage engines
extern shadow_document shadow_db[];
extern int shadow_doc_count;

int rank_topk(const double *query_vec, const index_schema *schema, 
              size_t k, candidate_match *out_results) {
    // Edge case protections
    if (shadow_doc_count == 0 || k == 0) return 0;

    // Allocate tracking array on the stack. Zero heap allocation!
    candidate_match candidates[MAX_DOCS];
    int candidate_count = 0;

    // Pre-allocate a safe stack buffer for the unpacked document vector.
    // 1024 dimensions * 8 bytes (double) = 8KB. Easily fits on the C stack.
    double doc_vec[1024]; 

    for (int i = 0; i < shadow_doc_count; i++) {
        shadow_document *doc = &shadow_db[i];
        
        // Step 1: Find the vector blob inside this document
        const unsigned char *blob = NULL;
        size_t blob_len = 0;
        
        for (size_t j = 0; j < doc->field_count; j++) {
            if (strcmp(doc->fields[j].field, schema->vec_field) == 0) {
                blob = (const unsigned char *)doc->fields[j].val_data;
                blob_len = doc->fields[j].val_len;
                break;
            }
        }

        // If this document doesn't have the vector field, silently skip it.
        if (blob == NULL) continue;

        // Step 2: Unpack the blob into our stack buffer
        // If the blob is corrupted or the wrong size, blob_to_floats returns 0.
        if (!blob_to_floats(blob, blob_len, schema, doc_vec)) {
            continue; // Protocol lie detected. Skip document.
        }

        // Step 3: Compute the Math
        double distance = dist_cosine(query_vec, doc_vec, schema->dim);

        // Step 4: Log the candidate
        candidates[candidate_count].key = doc->key;
        candidates[candidate_count].distance = distance;
        candidate_count++;
    }

    // Step 5: Sort all valid candidates
    qsort(candidates, candidate_count, sizeof(candidate_match), compare_candidates);

    // Step 6: Truncate and copy out
    // If the DB only has 2 documents, but the user asked for K=5, only return 2.
    int actual_k = (candidate_count < (int)k) ? candidate_count : (int)k;
    for (int i = 0; i < actual_k; i++) {
        out_results[i] = candidates[i];
    }

    return actual_k;
}