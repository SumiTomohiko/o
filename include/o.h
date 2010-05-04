#if !defined(O_H_INCLUDED)
#define O_H_INCLUDED

#define MSG_SIZE 256
struct oDB {
    char msg[MSG_SIZE];
};

typedef struct oDB oDB;

typedef int o_docid_t;

int oDB_create(oDB* db, const char* path);
void oDB_init(oDB* db);
void oDB_fini(oDB* db);

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
