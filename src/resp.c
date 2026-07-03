#include "resp.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>

#define RESP_MAX_BULK_SIZE (512LL * 1024 * 1024) // 512 MB

static const char *read_line(const char *buf, size_t len, size_t *line_len){
    const char *cr_ptr = (char *)memchr(buf, '\r', len);
    if(!cr_ptr) return NULL;
    if(cr_ptr + 1 >= buf + len || *(cr_ptr + 1) != '\n') return NULL;
    *line_len = (cr_ptr - buf);
    return buf;
}

static resp_status extract_bulk(const char *buf, size_t remaining_len, long long payload_len, char **out_data, size_t *bytes_consumed) {

    if(payload_len == -1) {
        *out_data = NULL;
        *bytes_consumed = 0;
        return RESP_OK;
    }

    if(remaining_len < (size_t)(payload_len + 2)) {
        return RESP_NEED_MORE;
    }
   
    *out_data = (char *)malloc(payload_len + 1);
    if(!*out_data) {
        return RESP_PROTO_ERR; // or handle memory allocation failure as needed
    }
    (*out_data)[payload_len] = '\0'; // Null-terminate for safety

    
    memcpy(*out_data, buf, payload_len);

   
    if(buf[payload_len] != '\r' || buf[payload_len + 1] != '\n') {
        free(*out_data); // Free allocated memory before returning
        *out_data = NULL; // Avoid dangling pointer
        return RESP_PROTO_ERR;
    }

    
    *bytes_consumed = payload_len + 2;
    return RESP_OK;
}

resp_status resp_parse(const char *buf, size_t len, size_t *consumed, resp_value **out) {
    if (len == 0) return RESP_NEED_MORE;

    char prefix = buf[0];

    switch (prefix) {
        case '+': { // Simple String
            size_t line_len;

            const char* text_start = read_line(buf + 1 , len - 1 , &line_len);
            if(text_start == NULL) return RESP_NEED_MORE;

            *out = malloc(sizeof(resp_value));
            if(!*out) return RESP_PROTO_ERR;

            (*out)->type = RESP_SIMPLE;
            (*out)->as.str.len = line_len;
            (*out)->as.str.data = malloc(line_len+1);

            memcpy((*out)->as.str.data, text_start , line_len);

            (*out)->as.str.data[line_len] = '\0';
            *consumed = 1 + line_len + 2;
            return RESP_OK;
        }
            
        case '-': { // Error
            
            size_t line_len;

            const char* text_start = read_line(buf+1 , len - 1, &line_len);
            if(!text_start) return RESP_NEED_MORE;
            *out = malloc(sizeof(resp_value));
            if (!*out) return RESP_PROTO_ERR; // Standard safety check

            (*out)-> type = RESP_ERROR;
            (*out)->as.str.len = line_len;
            (*out)->as.str.data = malloc(line_len + 1);

            memcpy((*out)->as.str.data , text_start , line_len);
            (*out)->as.str.data[line_len] = '\0';
            *consumed = 1 + line_len + 2;
            return RESP_OK;
        }
            
        case ':': { // Integer
            
            size_t line_len;
            const char* text_start = read_line(buf+1 , len - 1, &line_len);
            if(!text_start) return RESP_NEED_MORE;
            *out = malloc(sizeof(resp_value));
            if (!*out) return RESP_PROTO_ERR; // Standard safety check
            (*out) -> type = RESP_INTEGER;
            (*out)->as.integer = strtoll(text_start , NULL , 10);

            *consumed = 1 + line_len + 2;
            return RESP_OK;

            break;
            }
        case '$': // Bulk String
        {
            size_t line_len;
            const char* text_start = read_line(buf + 1, len - 1, &line_len);
            
            if(!text_start) return RESP_NEED_MORE;
            
            *out = malloc(sizeof(resp_value));
            if(*out == NULL) return RESP_PROTO_ERR;
            
            size_t header_bytes = 1 + line_len + 2; // $N\r\n

            // FIX 1: Use long long so -1 doesn't become 18 quintillion
            long long payload_len = strtoll(text_start, NULL, 10);
            
            // FIX 2: Exact check for Null String
            if(payload_len == -1) {
                (*out)->type = RESP_NULL;
                *consumed = header_bytes;
                return RESP_OK;
            }
            if (payload_len < 0 || payload_len > RESP_MAX_BULK_SIZE) {
                free(*out);
                *out = NULL;
                return RESP_PROTO_ERR;
            }
            
            (*out)->type = RESP_BULK;
            
            // FIX 3: Use a temporary variable for the scoop math
            size_t extracted_bytes = 0;
            resp_status status = extract_bulk(
                buf + header_bytes, 
                len - header_bytes, 
                payload_len, 
                &((*out)->as.str.data), 
                &extracted_bytes
            );
            
            if(status != RESP_OK) {
                free(*out);
                *out = NULL; // Prevent dangling pointers
                return status;
            }
            
            (*out)->as.str.len = payload_len;
            
           
            *consumed = header_bytes + extracted_bytes;
            return RESP_OK;
        }
        case '*': { 
            size_t array_line_len;
            const char *line = read_line(buf + 1, len - 1, &array_line_len);
            if(!line) return RESP_NEED_MORE;

            const char *p = line;
            const char *line_end = line + array_line_len;
            int negative = 0;
            long long count = 0;

            if(p == line_end) return RESP_PROTO_ERR;
            if(*p == '-') {
                negative = 1;
                p++;
                if(p == line_end) return RESP_PROTO_ERR;
            }

            while(p < line_end) {
                unsigned char digit = (unsigned char)(*p++ - '0');
                if(digit > 9) return RESP_PROTO_ERR;
                if(count > (LLONG_MAX - digit) / 10) return RESP_PROTO_ERR;
                count = count * 10 + digit;
            }

            if(negative) count = -count;
            if(count < -1) return RESP_PROTO_ERR;

            resp_value *value = malloc(sizeof(*value));
            if(!value) return RESP_PROTO_ERR;

            size_t offset = 1 + array_line_len + 2;
            if(count == -1) {
                value->type = RESP_NULL;
                value->as.array.items = NULL;
                value->as.array.n = 0;
                *out = value;
                *consumed = offset;
                return RESP_OK;
            }

            size_t n = (size_t)count;
            if((long long)n != count || n > ((size_t)-1) / sizeof(resp_value *)) {
                free(value);
                return RESP_PROTO_ERR;
            }

            resp_value **items = n ? malloc(n * sizeof(*items)) : NULL;

            if(n && !items) {
                free(value);
                return RESP_PROTO_ERR;
            }

            for(size_t i = 0; i < n; i++) {
                resp_value *child = NULL;
                size_t child_consumed = 0;
                resp_status status = resp_parse(buf + offset, len - offset, &child_consumed, &child);

                if(status != RESP_OK) {
                    for(size_t j = 0; j < i; j++) resp_free(items[j]);
                    free(items);
                    free(value);
                    return status;
                }
                if(!child || child_consumed == 0 || child_consumed > len - offset) {
                    resp_free(child);
                    for(size_t j = 0; j < i; j++) resp_free(items[j]);
                    free(items);
                    free(value);
                    return RESP_PROTO_ERR;
                }

                items[i] = child;
                offset += child_consumed;
            }

            value->type = RESP_ARRAY;
            value->as.array.items = items;
            value->as.array.n = n;
            *out = value;
            *consumed = offset;
            return RESP_OK;
        }
            
        default:
            return RESP_PROTO_ERR; // Unknown data type
    }
    
    return RESP_OK;
}

void resp_free(resp_value *v){
    if(!v) return;
    if(v -> type == RESP_SIMPLE || v->type == RESP_ERROR || v->type == RESP_BULK){
        free(v->as.str.data);
    } else if(v->type == RESP_ARRAY){
        for(size_t i = 0; i < v->as.array.n; i++){
            resp_free(v->as.array.items[i]);
        }
        free(v->as.array.items);
    }
    free(v);
}

static void writer_append(resp_writer *w, const char *data, size_t len) {
    if (w->len + len > w->cap) {
        w->cap = (w->cap == 0) ? 64 : w->cap;
        while (w->len + len > w->cap) w->cap *= 2;
        void* temp = realloc(w->buf, w->cap);
        if (!temp) {
            // Handle memory allocation failure
            fprintf(stderr, "Memory allocation failed in writer_append\n");
            exit(EXIT_FAILURE);
        }
        w->buf = temp;
    }
    memcpy(w->buf + w->len, data, len);
    w->len += len;
}


int resp_write_simple(resp_writer *w, const char *s) {
    writer_append(w, "+", 1);
    writer_append(w, s, strlen(s));
    writer_append(w, "\r\n", 2);
    return RESP_OK;
}

int resp_write_error(resp_writer *w, const char *msg) {
    writer_append(w, "-", 1);
    writer_append(w, msg, strlen(msg));
    writer_append(w, "\r\n", 2);
    return RESP_OK;
}

int resp_write_integer(resp_writer *w, long long v) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), ":%lld\r\n", v);
    writer_append(w, buf, len);
    return RESP_OK;
}

int resp_write_null(resp_writer *w) {
    writer_append(w, "$-1\r\n", 5);
    return RESP_OK;
}

int resp_write_bulk(resp_writer *w, const char *data, size_t len) {
    char header[64];
    int len_header = snprintf(header, sizeof(header), "$%zu\r\n", len);
    writer_append(w, header, len_header);
    writer_append(w, data, len);
    writer_append(w, "\r\n", 2);
    return RESP_OK;
}

int resp_write_array_header(resp_writer *w, size_t n) {
    char header[64];
    int len_header = snprintf(header, sizeof(header), "*%zu\r\n", n);
    writer_append(w, header, len_header);
    return RESP_OK;
}

