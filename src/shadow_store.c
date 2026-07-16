// src/shadow_store.c
#include "shadow_store.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "resp.h"

#define MAX_DOCS 1000
shadow_document shadow_db[MAX_DOCS];
int shadow_doc_count = 0;

int shadow_store_observe_hset(const resp_value *cmd) {
    if (cmd->type != RESP_ARRAY || cmd->as.array.n < 4) return 0;
    if (strncasecmp("HSET", cmd->as.array.items[0]->as.str.data, 4) != 0) return 0;

    // 1. Extract Key
    resp_value *key_val = cmd->as.array.items[1];
    if (key_val->type != RESP_BULK) return 0;

    // 2. Find or Create Document
    shadow_document *doc = NULL;
    for (int i = 0; i < shadow_doc_count; i++) {
        // Compare lengths first (faster), then memory
        if (strlen(shadow_db[i].key) == key_val->as.str.len &&
            strncmp(shadow_db[i].key, key_val->as.str.data, key_val->as.str.len) == 0) {
            doc = &shadow_db[i];
            break;
        }
    }

    if (doc == NULL) {
        if (shadow_doc_count >= MAX_DOCS) return 0; // DB Full
        doc = &shadow_db[shadow_doc_count++];
        doc->key = strndup(key_val->as.str.data, key_val->as.str.len);
        doc->fields = NULL;
        doc->field_count = 0;
    }

    // 3. Process Field-Value Pairs (Stride by 2)
    for (size_t i = 2; i + 1 < cmd->as.array.n; i += 2) {
        resp_value *f_item = cmd->as.array.items[i];
        resp_value *v_item = cmd->as.array.items[i+1];

        if (f_item->type != RESP_BULK || v_item->type != RESP_BULK) continue;

        // Photocopy the incoming field and value safely
        char *incoming_field = strndup(f_item->as.str.data, f_item->as.str.len);
        char *incoming_val = malloc(v_item->as.str.len);
        memcpy(incoming_val, v_item->as.str.data, v_item->as.str.len);
        size_t incoming_len = v_item->as.str.len;

        int field_found = 0;
        for (size_t j = 0; j < doc->field_count; j++) {
            if (strcmp(doc->fields[j].field, incoming_field) == 0) {
                // UPDATE EXISTING FIELD
                free(doc->fields[j].val_data); // Free the old value
                doc->fields[j].val_data = incoming_val;
                doc->fields[j].val_len = incoming_len;
                free(incoming_field); // We don't need the duplicate string
                field_found = 1;
                break;
            }
        }

        if (!field_found) {
            // INSERT NEW FIELD
            document_field *fields = realloc(
                doc->fields, (doc->field_count + 1) * sizeof(*doc->fields));
            if (fields == NULL) {
                free(incoming_field);
                free(incoming_val);
                return 0;
            }

            doc->fields = fields;
            document_field *new_field = &fields[doc->field_count++];
            new_field->field = incoming_field;
            new_field->val_data = incoming_val;
            new_field->val_len = incoming_len;
        }
    }

    return 1;
}
