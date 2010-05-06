#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "tcutil.h"
#include "o.h"

static void
set_msg(oDB* db, const char* s, const char* t)
{
    snprintf(db->msg, array_sizeof(db->msg), "%s - %s", s, t);
}

static void
set_msg_of_errno(oDB* db, const char* s)
{
    set_msg(db, s, strerror(errno));
}

void
oDB_init(oDB* db)
{
    db->path= NULL;
    db->msg[0] = '\0';
    db->lock_file = -1;
    db->next_doc_id = 0;
    db->index = tcbdbnew();
}

void
oDB_fini(oDB* db)
{
    tcbdbdel(db->index);
}

static int
open_index(oDB* db, const char* path, int omode)
{
    char index[1024];
    snprintf(index, array_sizeof(index), "%s/index.tcb", path);
    if (!tcbdbopen(db->index, index, omode)) {
        set_msg(db, "Can't open index", tcbdberrmsg(tcbdbecode(db->index)));
        return 1;
    }
    return 0;
}

static FILE*
open_doc_id(oDB* db, const char* path, const char* mode)
{
    char filename[1024];
    snprintf(filename, array_sizeof(filename), "%s/doc_id", path);
    FILE* fp = fopen(filename, mode);
    if (fp == NULL) {
        set_msg_of_errno(db, "Can't open doc_id");
    }
    return fp;
}

static int
write_doc_id(oDB* db, const char* path, o_doc_id_t doc_id)
{
    FILE* fp = open_doc_id(db, path, "wb");
    if (fp == NULL) {
        return 1;
    }
    fwrite(&doc_id, sizeof(doc_id), 1, fp);
    if (fclose(fp) != 0) {
        set_msg_of_errno(db, "Can't close doc_id");
        return 1;
    }
    return 0;
}

static int
read_doc_id(oDB* db, const char* path, o_doc_id_t* doc_id)
{
    FILE* fp = open_doc_id(db, path, "rb");
    if (fp == NULL) {
        return 1;
    }
    fread(doc_id, sizeof(*doc_id), 1, fp);
    if (fclose(fp) != 0) {
        set_msg_of_errno(db, "Can't close doc_id");
        return 1;
    }
    return 0;
}

int
oDB_create(oDB* db, const char* path)
{
    if (mkdir(path, 0755) != 0) {
        set_msg_of_errno(db, "mkdir failed");
        return 1;
    }
    if (open_index(db, path, BDBOWRITER | BDBOCREAT) != 0) {
        return 1;
    }
    if (write_doc_id(db, path, 0) != 0) {
        return 1;
    }
    return 0;
}

int
oDB_close(oDB* db)
{
    int status = 0;
    if (write_doc_id(db, db->path, db->next_doc_id) != 0) {
        status = 1;
    }
    if (!tcbdbclose(db->index)) {
        set_msg(db, "Can't close index", tcbdberrmsg(tcbdbecode(db->index)));
        status = 1;
    }
    if (close(db->lock_file) != 0) {
        set_msg_of_errno(db, "close failed");
        status = 1;
    }
    free(db->path);
    return status;
}

static int
lock_db(oDB* db, const char* path, int operation)
{
    char lock_file[1024];
    snprintf(lock_file, array_sizeof(lock_file), "%s/lock", path);
    int fd = open(lock_file, O_RDONLY | O_CREAT, 0644);
    if (fd == -1) {
        set_msg_of_errno(db, "open failed");
        return 1;
    }
    if (flock(fd, operation) != 0) {
        set_msg_of_errno(db, "flock failed");
        close(fd);
        return 1;
    }
    db->lock_file = fd;
    return 0;
}

static int
copy_path(oDB* db, const char* path)
{
    size_t size = strlen(path);
    char* s = (char*)malloc(size + 1);
    if (s == NULL) {
        set_msg_of_errno(db, "malloc failed");
        return 1;
    }
    strcpy(s, path);
    db->path = s;
    return 0;
}

static int
open_db(oDB* db, const char* path, int lock_operation, int omode)
{
    if (copy_path(db, path) != 0) {
        return 1;
    }
    if (lock_db(db, path, lock_operation) != 0) {
        return 1;
    }
    if (open_index(db, path, omode) != 0) {
        return 1;
    }
    if (read_doc_id(db, path, &db->next_doc_id) != 0) {
        return 1;
    }
    return 0;
}

int
oDB_open_to_read(oDB* db, const char* path)
{
    return open_db(db, path, LOCK_SH, BDBOREADER);
}

int
oDB_open_to_write(oDB* db, const char* path)
{
    return open_db(db, path, LOCK_EX, BDBOWRITER);
}

static int
get_char_size(char c)
{
    unsigned char ch = (unsigned char)c;
    if ((0xc0 <= ch) && (ch <= 0xdf)) {
        return 2;
    }
    if ((0xe0 <= ch) && (ch <= 0xef)) {
        return 3;
    }
    if ((0xf0 <= ch) && (ch <= 0xf7)) {
        return 4;
    }
    if ((0xf8 <= ch) && (ch <= 0xfb)) {
        return 5;
    }
    if ((0xfc <= ch) && (ch <= 0xfd)) {
        return 6;
    }
    return 1;
}

static int
get_term_size(const char* pc)
{
    int first_char_size = get_char_size(*pc);
    char second_char = pc[first_char_size];
    if (second_char == '\0') {
        return first_char_size;
    }
    return first_char_size + get_char_size(second_char);
}

static void
compress_num(int n, char* p, int* size)
{
    int m = n;
    int i = 0;
    do {
        int higher = m >> 7;
        int lower = m & 0x7f;
        p[i] = higher == 0 ? lower : lower | 0x80;
        m = higher;
        i++;
    } while (m != 0);
    *size = i;
}

static void
encode_posting(o_doc_id_t doc_id, int* pos, int pos_num, char* data, int* data_size)
{
    char* p = data;
#define ENCODE(num) do { \
    int size; \
    compress_num((num), p, &size); \
    p += size; \
} while (0)
    ENCODE(doc_id);
    ENCODE(pos_num);

    int i;
    for (i = 0; i < pos_num; i++) {
        ENCODE(pos[i]);
    }
#undef ENCODE
    *data_size = p - data;
}

int
oDB_put(oDB* db, const char* doc)
{
    TCMAP* term2pos = tcmapnew();
    size_t size = strlen(doc);
    unsigned int pos = 0;
    while (pos < size) {
        const char* pc = &doc[pos];
        int term_size = get_term_size(pc);
        tcmapaddint(term2pos, pc, term_size, pos);
        pos += get_char_size(doc[pos]);
    }

    tcmapiterinit(term2pos);
    int key_size;
    const void* key;
    while ((key = tcmapiternext(term2pos, &key_size)) != NULL) {
        int val_size;
        const void* val = tcmapget(term2pos, key, key_size, &val_size);
        assert(val != NULL);
        assert(val_size % sizeof(int) == 0);
        int pos_num = val_size / sizeof(int);
        o_doc_id_t doc_id = db->next_doc_id;
        /**
         * 1 of (1 + pos_num) is for a document id. Each number in postings
         * needs 8 bytes at most.
         */
        char data[(1 + pos_num) * 8];
        int data_size;
        encode_posting(doc_id, (int*)val, pos_num, data, &data_size);
        if (!tcbdbputdup(db->index, key, key_size, data, data_size)) {
            set_msg(db, "Can't register any term", tcbdberrmsg(tcbdbecode(db->index)));
            return 1;
        }
    }

    tcmapdel(term2pos);
    db->next_doc_id++;

    return 0;
}

struct Posting {
    o_doc_id_t doc_id;
    int* offset;
    int offset_size;
};

typedef struct Posting Posting;

static int
decompress_num(const char* p, int* size)
{
    int n = 0;
    int i = 0;
    char m = 0;
    int base = 1;
    do {
        m = p[i];
        n += base * (m & 0x7f);
        base *= 128;
        i++;
    } while ((m & 0x80) != 0);
    *size = i;
    return n;
}

static Posting*
decompress_posting(oDB* db, const char* compressed_posting)
{
    Posting* posting = (Posting*)malloc(sizeof(Posting));
    if (posting == NULL) {
        set_msg_of_errno(db, "Can't allocate Posting");
        return NULL;
    }
    const char* p = compressed_posting;
#define DECODE(num) do { \
    int size; \
    num = decompress_num(p, &size); \
    p += size; \
} while (0)
    o_doc_id_t doc_id;
    DECODE(doc_id);
    posting->doc_id = doc_id;

    int offset_size;
    DECODE(offset_size);
    posting->offset_size = offset_size;
    int* offset = (int*)malloc(sizeof(int) * offset_size);
    if (offset == NULL) {
        set_msg_of_errno(db, "Can't allocate offset");
        return NULL;
    }
    posting->offset = offset;

    int i;
    for (i = 0; i < offset_size; i++) {
        int offset;
        DECODE(offset);
        posting->offset[i] = offset;
    }
#undef DECODE

    return posting;
}

static TCLIST*
search_posting_list(oDB* db, const char* term, int term_size)
{
    TCLIST* compressed_posting_list = tcbdbget4(db->index, term, term_size);
    TCLIST* posting_list = tclistnew();
    if (compressed_posting_list == NULL) {
        return posting_list;
    }

    int num = tclistnum(compressed_posting_list);
    int i;
    for (i = 0; i < num; i++) {
        const char* compressed_posting = tclistval2(compressed_posting_list, i);
        Posting* posting = decompress_posting(db, compressed_posting);
        if (posting == NULL) {
            return NULL;
        }
        tclistpush(posting_list, &posting, sizeof(posting));
    }
    tclistdel(compressed_posting_list);
    return posting_list;
}

int
oDB_search(oDB* db, const char* phrase)
{
    TCLIST* posting_lists = tclistnew();
    size_t size = strlen(phrase);
    unsigned int pos = 0;
    unsigned int prev_pos = 0;
    do {
        int char_size = get_char_size(phrase[pos]);
        int from = size <= pos + char_size ? prev_pos + get_char_size(phrase[prev_pos]) : pos;
        const char* term = &phrase[from];
        int term_size = get_term_size(term);
        TCLIST* posting_list = search_posting_list(db, term, term_size);
        if (posting_list == NULL) {
            return 1;
        }
        tclistpush(posting_lists, &posting_list, sizeof(posting_list));

        prev_pos = from;
        pos = from + term_size;
    } while (pos < size);

    int i;
    for (i = 0; i < tclistnum(posting_lists); i++) {
        int sp;
        TCLIST** p = (TCLIST**)tclistval(posting_lists, i, &sp);
        assert(sp == sizeof(*p));
        assert(p != NULL);
        tclistdel(*p);
    }
    tclistdel(posting_lists);

    return 0;
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
