#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tcutil.h"
#include "o.h"
#include "o/private.h"

static void
usage()
{
    printf("usage:\n");
    printf("  o create [--attr=name] db\n");
    printf("  o get [--attr=name] db doc_id\n");
    printf("  o put [--attr=name:value] db\n");
    printf("  o search db phrase\n");
    printf("  o words db\n");
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
open_db_to_read(oDB* db, const char* path)
{
    if (oDB_open_to_read(db, path) != 0) {
        print_error("Can't open database to read", db->msg);
        return 1;
    }
    return 0;
}

static int
open_db_to_write(oDB* db, const char* path)
{
    if (oDB_open_to_write(db, path) != 0) {
        print_error("Can't open database to write", db->msg);
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
search(oDB* db, int argc, char* argv[])
{
    if (argc < 3) {
        usage();
        return 1;
    }

    int optind = 1;
    const char* path = argv[optind];
    if (open_db_to_read(db, path) != 0) {
        return 1;
    }
    const char* phrase = argv[optind + 1];
    oHits* hits = NULL;
    if (oDB_search(db, phrase, &hits) != 0) {
        print_error("Can't search document", db->msg);
        return 1;
    }
    if (close_db(db) != 0) {
        return 1;
    }

    int i;
    for (i = 0; i < hits->num; i++) {
        printf("%d\n", hits->doc_id[i]);
    }
    free(hits);
    return 0;
}

static int
put(oDB* db, int argc, char* argv[])
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
create(oDB* db, int argc, char* argv[])
{
#define MAX_ATTRS 32
    const char* attrs[MAX_ATTRS];
    int attrs_num = 0;

    struct option options[] = {
        { "attr", required_argument, NULL, 'a' },
        { 0, 0, 0, 0 } };
    int opt;
    while ((opt = getopt_long(argc, argv, "", options, NULL)) != -1) {
        switch (opt) {
        case 'a':
            if (MAX_ATTRS <= attrs_num) {
                fprintf(stderr, "Maximum attributes number is %d", MAX_ATTRS);
                return 1;
            }
            attrs[attrs_num] = optarg;
            attrs_num++;
            break;
        case '?':
        default:
            return 1;
            break;
        }
    }
    if (argc <= optind) {
        usage();
        return 1;
    }

    if (oDB_create(db, argv[optind], attrs, attrs_num) != 0) {
        print_error("Can't create database", db->msg);
        return 1;
    }
    return 0;
#undef MAX_ATTRS
}

static int
get(oDB* db, int argc, char* argv[])
{
    if (argc < 2) {
        usage();
        return 1;
    }
    if (open_db_to_read(db, argv[0]) != 0) {
        return 1;
    }

    int size;
    char* doc = oDB_get(db, atoi(argv[1]), &size);
    if (doc == NULL) {
        print_error("Can't get document", db->msg);
        close_db(db);
        return 1;
    }
    printf("%s", doc);
    free(doc);

    if (close_db(db) != 0) {
        return 1;
    }
    return 0;
}

static int
words(oDB* db, int argc, char* argv[])
{
    if (argc < 2) {
        usage();
        return 1;
    }
    if (open_db_to_read(db, argv[1]) != 0) {
        return 1;
    }

    BDBCUR* cur = tcbdbcurnew(db->index);
    tcbdbcurfirst(cur);
    char* term;
    while ((term = tcbdbcurkey2(cur)) != NULL) {
        printf("%s\n", term);
        free(term);
        tcbdbcurnext(cur);
    };
    tcbdbcurdel(cur);

    if (close_db(db) != 0) {
        return 1;
    }
    return 0;
}

static int
do_cmd(oDB* db, int argc, char* argv[])
{
    if (argc < 1) {
        usage();
        return 1;
    }

    const char* cmd = argv[0];
    if (strcmp(cmd, "create") == 0) {
        return create(db, argc, argv);
    }
    if (strcmp(cmd, "put") == 0) {
        return put(db, argc, argv);
    }
    if (strcmp(cmd, "search") == 0) {
        return search(db, argc, argv);
    }
    if (strcmp(cmd, "words") == 0) {
        return words(db, argc, argv);
    }
    if (strcmp(cmd, "get") == 0) {
        return get(db, argc, argv);
    }
    usage();
    return 1;
}

int
main(int argc, char* argv[])
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
