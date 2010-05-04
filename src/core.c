#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "o.h"

void
oDB_init(oDB* db)
{
    db->msg[0] = '\0';
}

void
oDB_fini(oDB* db)
{
}

int
oDB_create(oDB* db, const char* path)
{
    if (mkdir(path, 0755) != 0) {
        snprintf(db->msg, MSG_SIZE, "mkdir failed - %s", strerror(errno));
        return 1;
    }
    return 0;
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
