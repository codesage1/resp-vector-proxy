#include "resp.h"

typedef struct {
    char *index;        /* Name of the index, e.g., "idx" */
    char *prefix;       /* Tracked key prefix, e.g., "docs:" */
    char *vec_field;    /* The VECTOR field name, e.g., "doc_embedding" */
    enum { VT_FLOAT32, VT_FLOAT64 } type;
    long long dim;
    enum { M_COSINE, M_L2 } metric;
} index_schema;

int registry_observe_ftcreate(const resp_value *cmd);
const index_schema *registry_lookup(const char *index);