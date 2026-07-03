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
    // --- NEW: Round-Trip Test ---
    resp_writer w = {0}; // Initialize empty writer
    serialize_tree(&w, out);
    
    // Assert the forged buffer is exactly the same length and has the exact same bytes!
    assert(w.len == file_len);
    assert(memcmp(w.buf, buf, file_len) == 0);
    free(w.buf); // Free the writer's internal buffer
    // ----------------------------
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