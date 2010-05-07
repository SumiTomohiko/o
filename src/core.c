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
#include <zlib.h>
#include "tcutil.h"
#include "o.h"

/**
 * -Werror option stops compile if unused functions found. So I implement this
 * as a macro.
 */
#define DUMP_POSTING_LIST(list) do { \
    printf("%s:%u <", __FILE__, __LINE__); \
    int num = tclistnum((list)); \
    int i; \
    for (i = 0; i < num; i++) { \
        Posting* posting = *((Posting**)tclistval2((list), i)); \
        printf("<%d:", posting->doc_id); \
        int j; \
        for (j = 0; j < posting->offset_size; j++) { \
            printf("%d", posting->offset[j]); \
        } \
        printf(">"); \
    } \
    printf(">\n"); \
} while (0)

static void
set_msg(oDB* db, const char* s, const char* t)
{
    if (t == NULL) {
        snprintf(db->msg, array_sizeof(db->msg), "%s", s);
        return;
    }
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
    db->doc = tchdbnew();
}

void
oDB_fini(oDB* db)
{
    tchdbdel(db->doc);
    tcbdbdel(db->index);
}

static int
open_doc(oDB* db, const char* path, int omode)
{
    char doc[1024];
    snprintf(doc, array_sizeof(doc), "%s/doc.tch", path);
    if (!tchdbopen(db->doc, doc, omode)) {
        set_msg(db, "Can't open doc", tchdberrmsg(tchdbecode(db->doc)));
        return 1;
    }
    return 0;
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

static int
close_doc(oDB* db)
{
    if (!tchdbclose(db->doc)) {
        set_msg(db, "Can't close doc", tchdberrmsg(tchdbecode(db->doc)));
        return 1;
    }
    return 0;
}

static int
close_index(oDB* db)
{
    if (!tcbdbclose(db->index)) {
        set_msg(db, "Can't close index", tcbdberrmsg(tcbdbecode(db->index)));
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
    if (close_index(db) != 0) {
        return 1;
    }
    if (write_doc_id(db, path, 0) != 0) {
        return 1;
    }
    if (open_doc(db, path, HDBOWRITER | HDBOCREAT) != 0) {
        return 1;
    }
    if (close_doc(db) != 0) {
        return 1;
    }
    return 0;
}

int
oDB_close(oDB* db)
{
    int status = 0;
    if (close_doc(db) != 0) {
        status = 1;
    }
    if (write_doc_id(db, db->path, db->next_doc_id) != 0) {
        status = 1;
    }
    if (close_index(db) != 0) {
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
open_db(oDB* db, const char* path, int lock_operation, int index_mode, int doc_mode)
{
    if (copy_path(db, path) != 0) {
        return 1;
    }
    if (lock_db(db, path, lock_operation) != 0) {
        return 1;
    }
    if (open_index(db, path, index_mode) != 0) {
        return 1;
    }
    if (read_doc_id(db, path, &db->next_doc_id) != 0) {
        return 1;
    }
    if (open_doc(db, path, doc_mode) != 0) {
        return 1;
    }
    return 0;
}

int
oDB_open_to_read(oDB* db, const char* path)
{
    return open_db(db, path, LOCK_SH, BDBOREADER, HDBOREADER);
}

int
oDB_open_to_write(oDB* db, const char* path)
{
    return open_db(db, path, LOCK_EX, BDBOWRITER, HDBOWRITER);
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

static TCXSTR*
compress_doc(oDB* db, const char* doc, size_t size)
{
    z_stream z;
    z.zalloc = Z_NULL;
    z.zfree = Z_NULL;
    z.opaque = Z_NULL;
    if (deflateInit(&z, Z_DEFAULT_COMPRESSION) != Z_OK) {
        set_msg(db, "deflateInit failed", z.msg);
        return NULL;
    }
    TCXSTR* compressed = tcxstrnew();
    z.avail_in = size;
    z.next_in = (Bytef*)doc;
#define CONCAT  tcxstrcat(compressed, out_buf, sizeof(out_buf) - z.avail_out)
    while (0 < z.avail_in) {
        char out_buf[1024];
        z.next_out = (Bytef*)out_buf;
        z.avail_out = sizeof(out_buf);
        if (deflate(&z, Z_NO_FLUSH) != Z_OK) {
            deflateEnd(&z);
            set_msg(db, "deflate failed for Z_NO_FLUSH", z.msg);
            tcxstrdel(compressed);
            return NULL;
        }
        CONCAT;
    }
    while (1) {
        char out_buf[1024];
        z.next_out = (Bytef*)out_buf;
        z.avail_out = sizeof(out_buf);
        int retval = deflate(&z, Z_FINISH);
        if (retval == Z_STREAM_END) {
            CONCAT;
            break;
        }
        if (retval != Z_OK) {
            deflateEnd(&z);
            set_msg(db, "deflate failed for Z_FINISH", z.msg);
            tcxstrdel(compressed);
            return NULL;
        }
        CONCAT;
    }
#undef CONCAT

    if (deflateEnd(&z) != Z_OK) {
        set_msg(db, "deflateEnd failed", z.msg);
        tcxstrdel(compressed);
        return NULL;
    }
    return compressed;
}

char*
oDB_get(oDB* db, o_doc_id_t doc_id, int* size)
{
    int sp;
    char* compressed = (char*)tchdbget(db->doc, &doc_id, sizeof(doc_id), &sp);
    if (compressed == NULL) {
        set_msg(db, "Document not found", NULL);
        return NULL;
    }

    z_stream z;
    z.zalloc = Z_NULL;
    z.zfree = Z_NULL;
    z.opaque = Z_NULL;
    z.next_in = Z_NULL;
    z.avail_in = 0;
    if (inflateInit(&z) != Z_OK) {
        set_msg(db, "inflateInit failed", z.msg);
        return NULL;
    }

    TCXSTR* doc = tcxstrnew();
    z.avail_in = sp;
    z.next_in = (Bytef*)compressed;
    while (0 < z.avail_in) {
#define CONCAT  tcxstrcat(doc, buf, sizeof(buf) - z.avail_out)
        char buf[1024];
        z.next_out = (Bytef*)buf;
        z.avail_out = sizeof(buf);
        int retval = inflate(&z, Z_NO_FLUSH);
        if (retval == Z_STREAM_END) {
            CONCAT;
            break;
        }
        if (retval != Z_OK) {
            set_msg(db, "inflate failed", z.msg);
            inflateEnd(&z);
            tcxstrdel(doc);
            return NULL;
        }
        CONCAT;
#undef CONCAT
    }

    if (inflateEnd(&z) != Z_OK) {
        set_msg(db, "inflateEnd failed", z.msg);
        tcxstrdel(doc);
        return NULL;
    }
    free(compressed);

    *size = tcxstrsize(doc) + 1;
    char* s = (char*)malloc(*size);
    if (s == NULL) {
        set_msg_of_errno(db, "Can't allocate string");
        tcxstrdel(doc);
        return NULL;
    }
    memcpy(s, tcxstrptr(doc), *size - 1);
    s[*size - 1] = '\0';
    tcxstrdel(doc);

    return s;
}

static int
put_doc(oDB* db, o_doc_id_t doc_id, const char* doc, size_t size)
{
    TCXSTR* compressed = compress_doc(db, doc, size);
    if (compressed == NULL) {
        return 1;
    }
    if (!tchdbput(db->doc, &doc_id, sizeof(doc_id), tcxstrptr(compressed), tcxstrsize(compressed))) {
        set_msg(db, "Can't register doc", tchdberrmsg(tchdbecode(db->doc)));
        return 1;
    }
    tcxstrdel(compressed);
    return 0;
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
        tcmapputcat(term2pos, pc, term_size, &pos, sizeof(pos));
        pos += get_char_size(doc[pos]);
    }

    o_doc_id_t doc_id = db->next_doc_id;

    tcmapiterinit(term2pos);
    int key_size;
    const void* key;
    while ((key = tcmapiternext(term2pos, &key_size)) != NULL) {
        int val_size;
        const void* val = tcmapget(term2pos, key, key_size, &val_size);
        assert(val != NULL);
        assert(val_size % sizeof(int) == 0);
        int pos_num = val_size / sizeof(int);
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

    if (put_doc(db, doc_id, doc, size) != 0) {
        return 1;
    }

    db->next_doc_id++;

    return 0;
}

struct Posting {
    o_doc_id_t doc_id;
    int term_size;
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
Posting_new(oDB* db)
{
    Posting* posting = (Posting*)malloc(sizeof(Posting));
    if (posting == NULL) {
        set_msg_of_errno(db, "Can't allocate memory");
        return NULL;
    }
    posting->doc_id = 0;
    posting->term_size = 0;
    posting->offset = NULL;
    posting->offset_size = 0;
    return posting;
}

static Posting*
Posting_of_offset_size(oDB* db, int offset_size)
{
    Posting* posting = Posting_new(db);
    if (posting == NULL) {
        return NULL;
    }
    int* offset = (int*)malloc(sizeof(int) * offset_size);
    if (offset == NULL) {
        set_msg_of_errno(db, "malloc failed");
        return NULL;
    }
    posting->offset = offset;
    posting->offset_size = offset_size;
    return posting;
}

static Posting*
decompress_posting(oDB* db, const char* compressed_posting)
{
    Posting* posting = Posting_new(db);
    if (posting == NULL) {
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
        posting->term_size = term_size;
        tclistpush(posting_list, &posting, sizeof(posting));
    }
    tclistdel(compressed_posting_list);
    return posting_list;
}

static void
Posting_delete(oDB* db, Posting* posting)
{
    free(posting->offset);
    free(posting);
}

static TCLIST*
intersect(oDB* db, TCLIST* posting_list1, TCLIST* posting_list2, int gap)
{
    /**
     * This function assume that postings stand in ascending order of the
     * document ID. It seems depend on Tokyo Cabinet implementation.
     */
    TCLIST* result = tclistnew();
    int num1 = tclistnum(posting_list1);
    int num2 = tclistnum(posting_list2);
    int i = 0;
    int j = 0;
    while ((i < num1) && (j < num2)) {
        Posting* posting1 = *((Posting**)tclistval2(posting_list1, i));
        Posting* posting2 = *((Posting**)tclistval2(posting_list2, j));
        if (posting1->doc_id < posting2->doc_id) {
            i++;
            continue;
        }
        if (posting2->doc_id < posting1->doc_id) {
            j++;
            continue;
        }

        int offset_size_min = posting1->offset_size < posting2->offset_size ? posting1->offset_size : posting2->offset_size;
        int offset[offset_size_min];
        int offset_size = 0;
        int k = 0;
        int l = 0;
        while ((k < posting1->offset_size) && (l < posting2->offset_size)) {
            int offset_end = posting1->offset[k] + gap;
            if (offset_end < posting2->offset[l]) {
                k++;
                continue;
            }
            if (posting2->offset[l] < offset_end) {
                l++;
                continue;
            }
            offset[offset_size] = posting1->offset[k];
            offset_size++;
            k++;
            l++;
        }
        if (0 < offset_size) {
            Posting* posting = Posting_of_offset_size(db, offset_size);
            if (posting == NULL) {
                return NULL;
            }
            posting->doc_id = posting1->doc_id;
            posting->term_size = posting2->offset[l] + posting2->term_size - posting1->offset[k];
            memcpy(posting->offset, offset, sizeof(offset[0]) * offset_size);
            posting->offset_size = offset_size;
            tclistpush(result, &posting, sizeof(posting));
        }

        i++;
        j++;
    }

    return result;
}

static void
delete_posting_list(oDB* db, TCLIST* posting_list)
{
    int num = tclistnum(posting_list);
    int i;
    for (i = 0; i < num; i++) {
        Posting* posting = *((Posting**)tclistval2(posting_list, i));
        Posting_delete(db, posting);
    }
    tclistdel(posting_list);
}

int
oDB_search(oDB* db, const char* phrase, o_doc_id_t** doc_ids, int* doc_ids_size)
{
    *doc_ids = NULL;

    int term_size = get_term_size(phrase);
    TCLIST* posting_list1 = search_posting_list(db, phrase, term_size);
    if (posting_list1 == NULL) {
        return 1;
    }
    if (tclistnum(posting_list1) == 0) {
        return 0;
    }

    size_t size = strlen(phrase);
    unsigned int pos = term_size;
    unsigned int prev_pos = 0;
    while (pos < size) {
        int char_size = get_char_size(phrase[pos]);
        int gap;
        int from;
        const char* term;
        int term_size;
        if (size <= pos + char_size) {
            gap = get_char_size(phrase[prev_pos]);
            from = prev_pos + gap;
            term = &phrase[from];
            term_size = get_term_size(term);
        }
        else {
            from = pos;
            term = &phrase[from];
            gap = term_size = get_term_size(term);
        }
        TCLIST* posting_list2 = search_posting_list(db, term, term_size);
        if (posting_list2 == NULL) {
            tclistdel(posting_list1);
            return 1;
        }
        if (tclistnum(posting_list2) == 0) {
            tclistdel(posting_list1);
            tclistdel(posting_list2);
            return 0;
        }

        TCLIST* intersected = intersect(db, posting_list1, posting_list2, gap);
        delete_posting_list(db, posting_list1);
        delete_posting_list(db, posting_list2);
        if (intersected == NULL) {
            return 1;
        }

        prev_pos = from;
        pos = from + term_size;
        posting_list1 = intersected;
    }

    int num = tclistnum(posting_list1);
    *doc_ids = (o_doc_id_t*)malloc(sizeof(o_doc_id_t) * num);
    if (*doc_ids == NULL) {
        set_msg_of_errno(db, "malloc failed");
        delete_posting_list(db, posting_list1);
        return 1;
    }
    int i;
    for (i = 0; i < num; i++) {
        Posting* posting = *((Posting**)tclistval2(posting_list1, i));
        (*doc_ids)[i] = posting->doc_id;
    }
    *doc_ids_size = num;

    delete_posting_list(db, posting_list1);
    return 0;
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
