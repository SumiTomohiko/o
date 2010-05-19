#if !defined(O_H_INCLUDED)
#define O_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "tcbdb.h"
#include "tchdb.h"
#include "tcutil.h"

typedef int o_doc_id_t;

struct oDB {
    char* path;
    char msg[256];
    int lock_file;
    o_doc_id_t next_doc_id;
    TCBDB* index;
    TCHDB* doc;
};

typedef struct oDB oDB;

int oDB_create(oDB* db, const char* path);
void oDB_init(oDB* db);
void oDB_fini(oDB* db);
int oDB_open_to_read(oDB* db, const char* path);
int oDB_open_to_write(oDB* db, const char* path);
int oDB_close(oDB* db);
int oDB_put(oDB* db, const char* doc);
char* oDB_get(oDB* db, o_doc_id_t doc_id, int* size);
int oDB_search(oDB* db, const char* phrase, o_doc_id_t** doc_ids, int* doc_ids_size);
int oDB_search_fuzzily(oDB* db, const char* phrase, o_doc_id_t** doc_ids, int* doc_ids_size);
void oDB_set_msg_of_errno(oDB* db, const char* msg);

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
