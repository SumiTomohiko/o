#if !defined(O_H_INCLUDED)
#define O_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "tcbdb.h"
#include "tcutil.h"

typedef int o_doc_id_t;

struct oDB {
    char* path;
    char msg[256];
    int lock_file;
    o_doc_id_t next_doc_id;
    TCBDB* index;
};

typedef struct oDB oDB;

int oDB_create(oDB* db, const char* path);
void oDB_init(oDB* db);
void oDB_fini(oDB* db);
int oDB_open_to_write(oDB* db, const char* path);
int oDB_close(oDB* db);
int oDB_put(oDB* db, const char* doc);

#define array_sizeof(a) (sizeof(a) / sizeof(a[0]))

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
