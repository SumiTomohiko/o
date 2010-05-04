#include <string.h>
#include <stdio.h>
#include "o.h"

static void
usage()
{
    printf("usage:\n");
    printf("  o create db\n");
}

static void
print_error(const char* s, const char* msg)
{
    fprintf(stderr, "%s - %s\n", s, msg);
}

static int
create(oDB* db, int argc, const char* argv[])
{
    if (argc < 1) {
        usage();
        return 1;
    }
    if (oDB_create(db, argv[0]) != 0) {
        print_error("Can't create database", db->msg);
        return 1;
    }
    return 0;
}

static int
do_cmd(oDB* db, int argc, const char* argv[])
{
    if (argc < 1) {
        usage();
        return 1;
    }

    const char* cmd = argv[0];
    int cmd_argc = argc - 1;
    const char** cmd_argv = argv + 1;
    if (strcmp(cmd, "create") == 0) {
        return create(db, cmd_argc, cmd_argv);
    }
    usage();
    return 1;
}

int
main(int argc, const char* argv[])
{
    oDB db;
    oDB_init(&db);
    int status = do_cmd(&db, argc - 1, argv + 1);
    oDB_fini(&db);

    return status;
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
