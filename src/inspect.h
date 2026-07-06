#include "resp.h"

typedef struct {
    char      *index;        /* owned copies — we allocate */
    long long  k;
    char      *field;        /* the @field inside [KNN ...]  */
    char      *param_name;   /* the $param inside [KNN ...]  */
    const unsigned char *vec; /* BORROWED — points into cmd's tree */
    size_t     vec_len;
    long long  dialect;      /* -1 if no DIALECT clause */
} knn_query;

int  is_ft_search(const resp_value *cmd);   /* 1 iff argv[0] == FT.SEARCH, any case */

/* 0 = FT.SEARCH with a well-formed KNN clause, out is filled
 * 1 = FT.SEARCH but not KNN-shaped (plain text search etc.) — not ours
 * -1 = FT.SEARCH, KNN-shaped, but malformed (e.g. PARAMS missing the named param) */
int  knn_parse(const resp_value *cmd, knn_query *out);

void knn_free_fields(knn_query *q);
void knn_log(const knn_query *q);   /* one line: index, k, field, param,
                                       vec_len, dialect + hex of vec[0..15] */