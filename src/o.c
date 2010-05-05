#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcutil.h"
#include "o.h"

static void
usage()
{
    printf("usage:\n");
    printf("  o create db\n");
    printf("  o put db\n");
}

static void
print_error(const char* msg, const char* reason)
{
    if (reason[0] == '\0') {
        fprintf(stderr, "%s\n", msg);
        return;
    }
    fprintf(stderr, "%s - %s\n", msg, reason);
}

static char*
read_file(oDB* db, FILE* fp)
{
    TCXSTR* xstr = tcxstrnew();
    char buf[4096];
    while (fgets(buf, array_sizeof(buf), fp) != NULL) {
        tcxstrcat2(xstr, buf);
    }
    char* s = (char*)malloc(tcxstrsize(xstr));
    if (s == NULL) {
        print_error("malloc failed", strerror(errno));
        tcxstrdel(xstr);
        return NULL;
    }
    strcpy(s, tcxstrptr(xstr));
    tcxstrdel(xstr);

    return s;
}

static int
open_db_to_write(oDB* db, const char* path)
{
    if (oDB_open_to_write(db, path) != 0) {
        print_error("Can't open database to read", db->msg);
        return 1;
    }
    return 0;
}

static int
close_db(oDB* db)
{
    if (oDB_close(db) != 0) {
        print_error("Can't close databse", db->msg);
        return 1;
    }
    return 0;
}

static int
put_doc(oDB* db, const char* path, const char* doc)
{
    if (open_db_to_write(db, path) != 0) {
        return 1;
    }
    if (oDB_put(db, doc) != 0) {
        print_error("Can't put document", db->msg);
        return 1;
    }
    if (close_db(db) != 0) {
        return 1;
    }
    return 0;
}

static int
put(oDB* db, int argc, const char* argv[])
{
    if (argc < 1) {
        usage();
        return 1;
    }
    char* doc = read_file(db, stdin);
    if (doc == NULL) {
        return 1;
    }
    int status = put_doc(db, argv[0], doc);
    free(doc);

    return status;
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
    if (strcmp(cmd, "put") == 0) {
        return put(db, cmd_argc, cmd_argv);
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
