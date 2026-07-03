#include "resp.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

static const char *read_line(const char *buf, size_t len, size_t *line_len){
    const char *cr_ptr = (char *)memchr(buf, '\r', len);
    if(!cr_ptr) return NULL;
    if(cr_ptr + 1 >= buf + len || *(cr_ptr + 1) != '\n') return NULL;
    *line_len = (cr_ptr - buf);
    return buf;
}
/*
 * Extracts a length-prefixed binary payload.
 *
 * buf: pointer to the start of the raw data (immediately after the $N\r\n)
 * remaining_len: total bytes left in the entire network buffer
 * payload_len: the number N we parsed from the prefix (can be -1 for Null string)
 * out_data: where to store the heap-allocated copy of the payload
 * bytes_consumed: how many bytes this function stepped over
 */
static resp_status extract_bulk(const char *buf, size_t remaining_len, long long payload_len, 
                                char **out_data, size_t *bytes_consumed) {
    // 1. Handle the Null Bulk String (payload_len == -1). 
    //    If so, set *out_data to NULL, *bytes_consumed to 0, and return RESP_OK.
    if(payload_len == -1) {
        *out_data = NULL;
        *bytes_consumed = 0;
        return RESP_OK;
    }

    // 2. The binary safety check: Do we have enough bytes in 'buf' to read 
    //    the entire payload PLUS the trailing \r\n? 
    //    If remaining_len < payload_len + 2, we must return RESP_NEED_MORE.
    if(remaining_len < (size_t)(payload_len + 2)) {
        return RESP_NEED_MORE;
    }
    // 3. Allocate memory for the payload. 
    //    Pro-tip: Allocate (payload_len + 1) and set the last byte to '\0'. 
    //    RESP doesn't require null-terminators because it tracks length, but adding 
    //    a hidden one makes debugging with printf() massively easier later.
    *out_data = (char *)malloc(payload_len + 1);
    if(!*out_data) {
        return RESP_PROTO_ERR; // or handle memory allocation failure as needed
    }
    (*out_data)[payload_len] = '\0'; // Null-terminate for safety

    // 4. Use memcpy() to blindly copy exactly 'payload_len' bytes from 'buf' to your new memory.
    memcpy(*out_data, buf, payload_len);

    // 5. Protocol Verification: Check if buf[payload_len] is '\r' and buf[payload_len + 1] is '\n'.
    //    If they are not, the client lied about the length. Return RESP_PROTO_ERR.
    if(buf[payload_len] != '\r' || buf[payload_len + 1] != '\n') {
        free(*out_data); // Free allocated memory before returning
        *out_data = NULL; // Avoid dangling pointer
        return RESP_PROTO_ERR;
    }

    // 6. Set *bytes_consumed to (payload_len + 2) and return RESP_OK.
    *bytes_consumed = payload_len + 2;
    return RESP_OK;
}

resp_status resp_parse(const char *buf, size_t len, size_t *consumed, resp_value **out) {
    if (len == 0) return RESP_NEED_MORE;

    char prefix = buf[0];

    switch (prefix) {
        case '+': { // Simple String
            // TODO: Implement Simple String logic using read_line
            size_t line_len;

            const char* text_start = read_line(buf + 1 , len - 1 , &line_len);
            if(text_start == NULL) return RESP_NEED_MORE;

            *out = malloc(sizeof(resp_value));
            if(!*out) return RESP_PROTO_ERR;

            (*out)->type = RESP_SIMPLE;
            (*out)->as.str.len = line_len + 1;
            (*out)->as.str.data = malloc(line_len+1);

            memcpy((*out)->as.str.data, text_start , line_len);

            (*out)->as.str.data[line_len] = '\0';
            *consumed = 1 + line_len + 2;
            return RESP_OK;
        }
            
        case '-': { // Error
            // TODO: Implement Error logic (identical to Simple String)
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
            // TODO: Implement Integer logic
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

            // FIX 1: Use long long so -1 doesn't become 18 quintillion
            long long payload_len = strtoll(text_start, NULL, 10);
            size_t header_bytes = 1 + line_len + 2; // $N\r\n

            // FIX 2: Exact check for Null String
            if(payload_len == -1) {
                (*out)->type = RESP_NULL;
                *consumed = header_bytes;
                return RESP_OK;
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
            
            // Do the final math combining the header and the payload steps
            *consumed = header_bytes + extracted_bytes;
            return RESP_OK;
        }
        case '*': { // Array
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