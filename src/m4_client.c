#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>

int main() {
    // 1. Connect to the Proxy (NOT the real Redis)
    redisContext *c = redisConnect("127.0.0.1", 7000);
    if (c == NULL || c->err) {
        if (c) {
            printf("❌ Connection Error: %s\n", c->errstr);
            redisFree(c);
        } else {
            printf("❌ Can't allocate redis context\n");
        }
        return 1;
    }
    printf("✅ Connected to Proxy on port 7000\n");

    // 2. Build the Poison Payload
    // This 4-byte array contains 0x0d 0x0a (\r\n). 
    // If your proxy breaks on this, it fails M3/M4.
    char poison_vec[4] = {0xde, 0x0d, 0x0a, 0xef};

    printf("Sending FT.SEARCH with binary poison payload...\n");
    
    // 3. Send the Command
    // Hiredis requires '%b' to safely send binary data with a specific length
    redisReply *reply = redisCommand(c, 
        "FT.SEARCH idx *=>[KNN 10 @v $vec] PARAMS 2 vec %b", 
        poison_vec, (size_t)4
    );

    if (reply == NULL) {
        printf("❌ Command failed, proxy likely closed connection: %s\n", c->errstr);
        redisFree(c);
        return 1;
    }

    // 4. Print the Result
    // We expect the real Redis to throw an error since "idx" doesn't exist,
    // which proves the proxy successfully forwarded the whole binary blob!
    printf(" Reply Type: %d\n", reply->type);
    if (reply->type == REDIS_REPLY_ERROR) {
        printf("✅ Real Redis Response (Fail-Open Success): %s\n", reply->str);
    } else {
        printf("   Response: %s\n", reply->str);
    }

    freeReplyObject(reply);
    redisFree(c);
    printf(" Client disconnected cleanly.\n");
    return 0;
}