#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../src/resp.h"

typedef struct {
    const char *test_name;
    const char *raw_resp;
    int expected_return_code; // 0 = KNN Found, 1 = Plain Text, -1 = Malformed/Missing
    size_t expected_vec_len;  // Length of the binary blob (if successful)
} knn_test_case;

knn_test_case tests[] = {
    {
        .test_name = "1. Well-formed KNN",
        .raw_resp = "*7\r\n$9\r\nFT.SEARCH\r\n$3\r\nidx\r\n$19\r\n*=>[KNN 10 @v $vec]\r\n$6\r\nPARAMS\r\n$1\r\n2\r\n$3\r\nvec\r\n$4\r\n\xde\xad\xbe\xef\r\n",
        .expected_return_code = 0,
        .expected_vec_len = 4
    },
    {
        .test_name = "2. Extra clauses (RETURN, SORTBY)",
        .raw_resp = "*12\r\n$9\r\nFT.SEARCH\r\n$3\r\nidx\r\n$19\r\n*=>[KNN 10 @v $vec]\r\n$6\r\nRETURN\r\n$1\r\n1\r\n$2\r\nid\r\n$6\r\nSORTBY\r\n$5\r\nscore\r\n$6\r\nPARAMS\r\n$1\r\n2\r\n$3\r\nvec\r\n$4\r\n\xde\xad\xbe\xef\r\n",
        .expected_return_code = 0,
        .expected_vec_len = 4
    },
    {
        .test_name = "3. Plain-text query",
        .raw_resp = "*3\r\n$9\r\nFT.SEARCH\r\n$3\r\nidx\r\n$11\r\nhello world\r\n",
        .expected_return_code = 1,
        .expected_vec_len = 0
    },
    {
        .test_name = "4. PARAMS missing",
        .raw_resp = "*6\r\n$9\r\nFT.SEARCH\r\n$3\r\nidx\r\n$19\r\n*=>[KNN 10 @v $vec]\r\n$6\r\nRETURN\r\n$1\r\n1\r\n$2\r\nid\r\n",
        .expected_return_code = -1,
        .expected_vec_len = 0
    },
    {
        .test_name = "5. Case variations",
        .raw_resp = "*7\r\n$9\r\nft.search\r\n$3\r\nidx\r\n$19\r\n*=>[knn 10 @v $vec]\r\n$6\r\nParams\r\n$1\r\n2\r\n$3\r\nvec\r\n$4\r\n\xde\xad\xbe\xef\r\n",
        .expected_return_code = 0,
        .expected_vec_len = 4
    }
};

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
            // PHASE 4: Binary Extraction
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

int main() {
    int passed = 0;
    int num_tests = sizeof(tests) / sizeof(tests[0]);

    printf("Starting M3 Vector DPI Tests...\n");

    for (int i = 0; i < num_tests; i++) {
        resp_value *cmd = NULL;
        size_t consumed = 0;
        
        // Pass the raw RESP string through your Rung 1 Parser
        resp_status status = resp_parse(tests[i].raw_resp, strlen(tests[i].raw_resp), &consumed, &cmd);
        
        if (status != RESP_OK) {
            printf("❌ [%s] Parser failed to process RESP\n", tests[i].test_name);
            continue;
        }

        const char *p_name = NULL;
        size_t p_len = 0;
        const char *v_data = NULL;
        size_t v_len = 0;

        // Run the Deep Packet Inspection
        int rc = knn_parse(cmd, &p_name, &p_len, &v_data, &v_len);
        
        if (rc != tests[i].expected_return_code) {
            printf("❌ [%s] FAIL: Expected return %d, got %d\n", tests[i].test_name, tests[i].expected_return_code, rc);
        } else if (rc == 0 && v_len != tests[i].expected_vec_len) {
            printf("❌ [%s] FAIL: Expected vector length %zu, got %zu\n", tests[i].test_name, tests[i].expected_vec_len, v_len);
        } else {
            printf("✅ [%s] PASS\n", tests[i].test_name);
            passed++;
        }
        
        resp_free(cmd);
    }

    printf("----------------------------------------\n");
    printf("Score: %d/%d passed\n", passed, num_tests);
    return passed == num_tests ? 0 : 1;
}