#if !defined(O_H_INCLUDED)
#define O_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "tcbdb.h"
#include "tchdb.h"
#include "tcutil.h"

typedef int o_doc_id_t;
typedef int o_attr_id_t;

#define MAX_ATTRS 32

struct oDB {
    char* path;
    char msg[256];
    int lock_file;
    o_doc_id_t next_doc_id;
    TCBDB* index;
    TCHDB* doc;
    TCHDB* attr2id;
    TCHDB* attrs[MAX_ATTRS];
};

typedef struct oDB oDB;

struct oHits {
    int num;
    o_doc_id_t doc_id[1];
};

typedef struct oHits oHits;

struct oAttr {
    const char* name;
    const char* val;
};

typedef struct oAttr oAttr;

int oDB_create(oDB* db, const char* path, const char* attrs[], int attrs_num);
void oDB_init(oDB* db);
void oDB_fini(oDB* db);
int oDB_open_to_read(oDB* db, const char* path);
int oDB_open_to_write(oDB* db, const char* path);
int oDB_close(oDB* db);
int oDB_put(oDB* db, const char* doc, oAttr attrs[], int attrs_num);
char* oDB_get(oDB* db, o_doc_id_t doc_id);
char* oDB_get_attr(oDB* db, o_doc_id_t doc_id, const char* attr);
int oDB_search(oDB* db, const char* phrase, oHits** hits);
void oDB_set_msg_of_errno(oDB* db, const char* msg);

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
