#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/resp.h"

/* Helper to read an exact binary file from disk into RAM */
char *load_fixture(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Failed to open %s\n", path);
        return NULL;
    }
    
    // Find the exact byte size of the file
    fseek(f, 0, SEEK_END);
    *out_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(*out_len);
    fread(buf, 1, *out_len, f);
    fclose(f);
    
    return buf;
}

void run_torture_loop(const char *fixture_path) {
    size_t file_len;
    char *buf = load_fixture(fixture_path, &file_len);
    assert(buf != NULL);

    printf("Testing %s (%zu bytes)...\n", fixture_path, file_len);

    // TODO: The Torture Loop Logic Goes Here
    size_t consumed = 0;
    resp_value *out = NULL;
    resp_status status;
    for(size_t k = 1; k < file_len; k++){
        status = resp_parse(buf, k, &consumed, &out);
        assert(status == RESP_NEED_MORE);
    }
    status = resp_parse(buf, file_len, &consumed, &out);
    assert(status == RESP_OK);
    assert(consumed == file_len);
    assert(out != NULL);
    resp_free(out);
    free(buf);
}

int main() {
    run_torture_loop("fixtures/cmd_ping.bin");
    run_torture_loop("fixtures/cmd_ftknn.bin"); // The Poison Boss
    run_torture_loop("fixtures/reply_nested.bin");
    
    printf("All Torture Loops Passed!\n");
    return 0;
}

