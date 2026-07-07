#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../src/resp.h"
#include "../src/inspect.h"

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