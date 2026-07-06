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

int is_knn(const resp_value *cmd){
    if(cmd->type != RESP_ARRAY || cmd->as.array.n < 4) return 0;
    for(size_t i = 0; i < cmd->as.array.n - 3; i++){
        resp_value *item = cmd->as.array.items[i];
        if(item->type == RESP_BULK){
            char* item_data = item->as.str.data;
            size_t item_len = item->as.str.len;
            if(item_len >= 6 && strncasecmp(item_data,"KNN",3) == 0){
                return 1;
            }
        }
    }
    return 0;
}