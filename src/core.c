#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "o.h"

static void
set_msg(oDB* db, const char* s, const char* t)
{
    snprintf(db->msg, MSG_SIZE, "%s - %s", s, t);
}

static void
set_msg_of_errno(oDB* db, const char* s)
{
    set_msg(db, s, strerror(errno));
}

void
oDB_init(oDB* db)
{
    db->msg[0] = '\0';
    db->lock_file = -1;
}

void
oDB_fini(oDB* db)
{
}

int
oDB_create(oDB* db, const char* path)
{
    if (mkdir(path, 0755) != 0) {
        set_msg_of_errno(db, "mkdir failed");
        return 1;
    }
    return 0;
}

int
oDB_close(oDB* db)
{
    if (close(db->lock_file) != 0) {
        set_msg_of_errno(db, "close failed");
        return 1;
    }
    return 0;
}

int
oDB_open_to_write(oDB* db, const char* path)
{
    char lock_file[1024];
    snprintf(lock_file, array_sizeof(lock_file), "%s/lock", path);
    int fd = open(lock_file, O_RDONLY | O_CREAT, 0644);
    if (fd == -1) {
        set_msg_of_errno(db, "open failed");
        return 1;
    }
    if (flock(fd, LOCK_EX) != 0) {
        set_msg_of_errno(db, "flock failed");
        close(fd);
        return 1;
    }
    db->lock_file = fd;

    return 0;
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
