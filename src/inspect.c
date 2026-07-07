#include "inspect.h"
#include <string.h>
int is_ft_search(const resp_value *cmd);
int is_knn(const resp_value *cmd);

int is_ft_search(const resp_value *cmd){
    if(cmd->type == RESP_ARRAY && cmd->as.array.n > 0){

        resp_value *first_item = cmd -> as.array.items[0];

        if(first_item -> type != RESP_BULK) return 0;

        char* cmd_name = first_item -> as.str.data;
        size_t cmd_len = first_item -> as.str.len;

        if(cmd_len == 9 && strncasecmp(cmd_name,"FT.SEARCH",9) == 0){
            is_knn(cmd);
            return 1;
        }
    }
    return 0;
}

// FULL PARSER: Extracts name AND hunts for the binary payload in PARAMS
int knn_parse(const resp_value *cmd, const char **out_param_name, size_t *out_param_len, const char **out_vec_data, size_t *out_vec_len) {
    if (cmd->type != RESP_ARRAY || cmd->as.array.n < 3) return 1; 

    resp_value *query = cmd->as.array.items[2];
    if (query->type != RESP_BULK) return 1;

    char *q_data = query->as.str.data;
    size_t q_len = query->as.str.len;

    const char *param_name = NULL;
    size_t param_len = 0;
    int found_knn = 0;

    // PHASE 1: Sliding Scanner to find "KNN"
    for (size_t i = 0; i <= q_len - 3; i++) {
        if (strncasecmp(&q_data[i], "KNN", 3) == 0) {
            found_knn = 1;
            // PHASE 2: Scan forward for '$' to extract variable name
            for (size_t j = i + 3; j < q_len; j++) {
                if (q_data[j] == '$') {
                    param_name = &q_data[j + 1];
                    for (size_t k = j + 1; k < q_len; k++) {
                        if (q_data[k] == ' ' || q_data[k] == ']') {
                            param_len = k - (j + 1);
                            break;
                        }
                    }
                    break;
                }
            }
            break;
        }
    }

    if (!found_knn) return 1;                     // Plain-text query
    if (!param_name || param_len == 0) return -1; // Malformed KNN syntax

    *out_param_name = param_name;
    *out_param_len = param_len;

    // PHASE 3: The PARAMS Hunt
    // Scan all arguments after the query looking for "PARAMS"
    for (size_t i = 3; i < cmd->as.array.n; i++) {
        resp_value *arg = cmd->as.array.items[i];
        
        if (arg->type == RESP_BULK && arg->as.str.len == 6 && strncasecmp(arg->as.str.data, "PARAMS", 6) == 0) {
            // Found PARAMS. The layout is: PARAMS <count> <name1> <value1> ...
            // We start checking pairs at i + 2 (skipping the count)
            if (i + 1 < cmd->as.array.n) {
                for (size_t p = i + 2; p + 1 < cmd->as.array.n; p += 2) {
                    resp_value *pname = cmd->as.array.items[p];
                    resp_value *pblob = cmd->as.array.items[p + 1];
                    
                    // Does this param name match the one we extracted?
                    if (pname->type == RESP_BULK && pname->as.str.len == param_len &&
                        strncmp(pname->as.str.data, param_name, param_len) == 0) {
                        
                        if (pblob->type == RESP_BULK) {
                            if (out_vec_data) *out_vec_data = pblob->as.str.data;
                            if (out_vec_len) *out_vec_len = pblob->as.str.len;
                            return 0; // Success! We have the binary payload.
                        }
                    }
                }
            }
            break; // Found PARAMS, but the matching variable wasn't in it.
        }
    }

    return -1; // Missing PARAMS block entirely
}