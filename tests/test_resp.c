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

void serialize_tree(resp_writer *w, resp_value *v);

void run_torture_loop(const char *fixture_path) {
    size_t file_len;
    char *buf = load_fixture(fixture_path, &file_len);
    if (!buf) return; // Skip gracefully if fixture doesn't exist yet

    printf("Testing Torture %s (%zu bytes)...\n", fixture_path, file_len);

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
    
    // --- Round-Trip Test ---
    resp_writer w = {0}; 
    serialize_tree(&w, out);
    
    assert(w.len == file_len);
    assert(memcmp(w.buf, buf, file_len) == 0);
    
    free(w.buf); 
    resp_free(out);
    free(buf);
}

void run_pipeline_test(const char *fixture_path) {
    size_t file_len;
    char *buf = load_fixture(fixture_path, &file_len);
    if (!buf) return;

    printf("Testing Pipeline %s (%zu bytes)...\n", fixture_path, file_len);

    size_t offset = 0;
    while(offset < file_len) {
        size_t consumed = 0;
        resp_value *out = NULL;
        
        // Pass the remaining buffer from the current offset
        resp_status status = resp_parse(buf + offset, file_len - offset, &consumed, &out);
        
        assert(status == RESP_OK);
        assert(consumed > 0);
        assert(out != NULL);
        
        offset += consumed;
        resp_free(out);
    }
    
    // Assert that the loop consumed exactly the whole file, no more, no less.
    assert(offset == file_len);
    free(buf);
}

/* Recursively write a parsed tree back into a writer */
void serialize_tree(resp_writer *w, resp_value *v) {
    if (!v) return;
    switch (v->type) {
        case RESP_SIMPLE:  resp_write_simple(w, v->as.str.data); break;
        case RESP_ERROR:   resp_write_error(w, v->as.str.data); break;
        case RESP_INTEGER: resp_write_integer(w, v->as.integer); break;
        case RESP_NULL:    resp_write_null(w); break;
        case RESP_BULK:    resp_write_bulk(w, v->as.str.data, v->as.str.len); break;
        case RESP_ARRAY:
            resp_write_array_header(w, v->as.array.n);
            for (size_t i = 0; i < v->as.array.n; i++) {
                serialize_tree(w, v->as.array.items[i]);
            }
            break;
    }
}

int main() {
    // 1. The Standard Fixtures
    run_torture_loop("fixtures/cmd_ping.bin");
    run_torture_loop("fixtures/cmd_ftknn.bin"); // The Poison Boss
    run_torture_loop("fixtures/reply_nested.bin");
    run_torture_loop("fixtures/reply_bulk.bin");
    run_torture_loop("fixtures/reply_array.bin");
    run_torture_loop("fixtures/reply_integer.bin");
    run_torture_loop("fixtures/reply_simple.bin");
    run_torture_loop("fixtures/reply_error.bin");
    run_torture_loop("fixtures/reply_null.bin");
    run_torture_loop("fixtures/reply_empty_array.bin");
    run_torture_loop("fixtures/reply_empty_bulk.bin");
    run_torture_loop("fixtures/reply_empty_simple.bin");
    
    // 2. The Pipelining Fixture
    // (Make sure you generate this file using printf "+PING\r\n+PONG\r\n" > fixtures/pipeline.bin)
    run_pipeline_test("fixtures/pipeline.bin");
    
    printf("All Tests Passed!\n");
    return 0;
}