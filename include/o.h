#if !defined(O_H_INCLUDED)
#define O_H_INCLUDED

#define MSG_SIZE 256
struct oDB {
    char msg[MSG_SIZE];
    int lock_file;
};

typedef struct oDB oDB;

typedef int o_docid_t;

int oDB_create(oDB* db, const char* path);
void oDB_init(oDB* db);
void oDB_fini(oDB* db);
int oDB_open_to_write(oDB* db, const char* path);
int oDB_close(oDB* db);

#define array_sizeof(a) (sizeof(a) / sizeof(a[0]))

#endif
/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
