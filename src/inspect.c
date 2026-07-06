#include "inspect.h"
#include <string.h>

int is_ft_search(const resp_value *cmd){
    if(cmd->type == RESP_ARRAY && cmd->as.array.n > 0){

        resp_value *first_item = cmd -> as.array.items[0];

        if(first_item -> type != RESP_BULK) return 0;

        char* cmd_name = first_item -> as.str.data;
        size_t cmd_len = first_item -> as.str.len;

        if(cmd_len == 9 && strncasecmp(cmd_name,"FT.SEARCH",9) == 0){
            return 1;
        }
    }
    return 0;
}
