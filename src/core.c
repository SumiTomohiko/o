#include <alloca.h>
#include <assert.h>
#include <ctype.h>
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
#include "o/private.h"

#define BIGRAM_SIZE 2

static void
set_msg(oDB* db, const char* s, const char* t)
{
    if (t == NULL) {
        snprintf(db->msg, array_sizeof(db->msg), "%s", s);
        return;
    }
    snprintf(db->msg, array_sizeof(db->msg), "%s - %s", s, t);
}

void
oDB_set_msg_of_errno(oDB* db, const char* msg)
{
    set_msg(db, msg, strerror(errno));
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
    db->attr2id = tchdbnew();
    int i;
    for (i = 0; i < array_sizeof(db->attrs); i++) {
        db->attrs[i] = NULL;
    }
}

void
oDB_fini(oDB* db)
{
    tchdbdel(db->attr2id);
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
        oDB_set_msg_of_errno(db, "Can't open doc_id");
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
        oDB_set_msg_of_errno(db, "Can't close doc_id");
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
        oDB_set_msg_of_errno(db, "Can't close doc_id");
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

static int
make_dir(oDB* db, const char* path)
{
    if (mkdir(path, 0755) != 0) {
        oDB_set_msg_of_errno(db, "mkdir failed");
        return 1;
    }
    return 0;
}

static void
format_attrs_dir(char* s, size_t size, const char* dir)
{
    snprintf(s, size, "%s/attrs", dir);
}

static int
open_attr(oDB* db, TCHDB* hdb, const char* dir, const char* attr, int omode)
{
    char path[1024];
    snprintf(path, array_sizeof(path), "%s/%s.tch", dir, attr);
    if (!tchdbopen(hdb, path, omode)) {
        set_msg(db, "Can't attribute index", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }
    return 0;
}

static int
close_attr(oDB* db, TCHDB* hdb)
{
    if (!tchdbclose(hdb)) {
        set_msg(db, "Can't close attribute index", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }
    return 0;
}

static int
create_attrs_index(oDB* db, const char* dir, const char* attrs[], int attrs_num)
{
    int i;
    for (i = 0; i < attrs_num; i++) {
        TCHDB* hdb = tchdbnew();
        if (open_attr(db, hdb, dir, attrs[i], HDBOWRITER | HDBOCREAT) != 0) {
            tchdbdel(hdb);
            return 1;
        }
        if (close_attr(db, hdb) != 0) {
            tchdbdel(hdb);
            return 1;
        }
        tchdbdel(hdb);
    }
    return 0;
}

static int
open_attr2id(oDB* db, const char* dir, int omode)
{
    char path[1024];
    snprintf(path, array_sizeof(path), "%s/attr2id.tch", dir);
    TCHDB* hdb = db->attr2id;
    if (!tchdbopen(hdb, path, omode)) {
        set_msg(db, "Can't open attr2id", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }
    return 0;
}

static int
create_attr2id(oDB* db, const char* dir, const char* attrs[], int attrs_num)
{
    if (open_attr2id(db, dir, HDBOWRITER | HDBOCREAT) != 0) {
        return 1;
    }
    TCHDB* hdb = db->attr2id;
    int i;
    for (i = 0; i < attrs_num; i++) {
        const char* attr = attrs[i];
        if (!tchdbput(hdb, attr, strlen(attr), &i, sizeof(i))) {
            set_msg(db, "Can't register attribute to attr2id", tchdberrmsg(tchdbecode(hdb)));
            return 1;
        }
    }
    if (!tchdbclose(hdb)) {
        set_msg(db, "Can't close attr2id", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }

    return 0;
}

static int
create_index(oDB* db, const char* path)
{
    if (open_index(db, path, BDBOWRITER | BDBOCREAT) != 0) {
        return 1;
    }
    if (close_index(db) != 0) {
        return 1;
    }
    return 0;
}

static int
create_doc(oDB* db, const char* path)
{
    if (open_doc(db, path, HDBOWRITER | HDBOCREAT) != 0) {
        return 1;
    }
    if (close_doc(db) != 0) {
        return 1;
    }
    return 0;
}

int
oDB_create(oDB* db, const char* path, const char* attrs[], int attrs_num)
{
    if (make_dir(db, path) != 0) {
        return 1;
    }
    char attrs_dir[1024];
    format_attrs_dir(attrs_dir, array_sizeof(attrs_dir), path);
    if (make_dir(db, attrs_dir) != 0) {
        return 2;
    }
    if (create_attrs_index(db, attrs_dir, attrs, attrs_num) != 0) {
        return 3;
    }
    if (create_attr2id(db, path, attrs, attrs_num) != 0) {
        return 4;
    }
    if (create_index(db, path) != 0) {
        return 5;
    }
    if (write_doc_id(db, path, 0) != 0) {
        return 6;
    }
    if (create_doc(db, path) != 0) {
        return 7;
    }
    return 0;
}

static int
close_attr2id(oDB* db)
{
    TCHDB* hdb = db->attr2id;
    if (!tchdbclose(hdb)) {
        set_msg(db, "Can't close attr2id", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }
    return 0;
}

int
oDB_close(oDB* db)
{
    int status = 0;
    TCHDB** phdb;
    for (phdb = &db->attrs[0]; *phdb != NULL; phdb++) {
        if (close_attr(db, *phdb) != 0) {
            status = 1;
        }
        tchdbdel(*phdb);
    }
    if (close_attr2id(db) != 0) {
        status = 1;
    }
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
        oDB_set_msg_of_errno(db, "close failed");
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
        oDB_set_msg_of_errno(db, "open failed");
        return 1;
    }
    if (flock(fd, operation) != 0) {
        oDB_set_msg_of_errno(db, "flock failed");
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
        oDB_set_msg_of_errno(db, "malloc failed");
        return 1;
    }
    strcpy(s, path);
    db->path = s;
    return 0;
}

static int
open_attrs(oDB* db, const char* path, int omode)
{
    char dir[1024];
    format_attrs_dir(dir, array_sizeof(dir), path);

    TCHDB* hdb = db->attr2id;
    if (!tchdbiterinit(hdb)) {
        set_msg(db, "Can't initialize iterator", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }
    char* key;
    while ((key = tchdbiternext2(hdb)) != NULL) {
        int sp;
        int* pindex = (int*)tchdbget(hdb, key, strlen(key), &sp);
        TCHDB* attr = tchdbnew();
        if (open_attr(db, attr, dir, key, omode) != 0) {
            return 1;
        }
        db->attrs[*pindex] = attr;
        free(key);
    }
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
    if (open_attr2id(db, path, HDBOREADER) != 0) {
        return 1;
    }
    if (open_attrs(db, path, doc_mode) != 0) {
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
compress_posting(o_doc_id_t doc_id, o_attr_id_t attr_id, int* pos, int pos_num, char* data, int* data_size)
{
    char* p = data;
#define COMPRESS(num) do { \
    int size; \
    compress_num((num), p, &size); \
    p += size; \
} while (0)
    int tagged_doc_id = (doc_id << 1) + (attr_id == -1 ? 0 : 1);
    COMPRESS(tagged_doc_id);
    if (attr_id != -1) {
        COMPRESS(attr_id);
    }
    COMPRESS(pos_num);

    int i;
    for (i = 0; i < pos_num; i++) {
        COMPRESS(pos[i]);
    }
#undef COMPRESS
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
oDB_get(oDB* db, o_doc_id_t doc_id)
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

    int size = tcxstrsize(doc) + 1;
    char* s = (char*)malloc(size);
    if (s == NULL) {
        oDB_set_msg_of_errno(db, "Can't allocate string");
        tcxstrdel(doc);
        return NULL;
    }
    memcpy(s, tcxstrptr(doc), size - 1);
    s[size - 1] = '\0';
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

typedef int offset_t;

static int
put_index(oDB* db, o_doc_id_t doc_id, o_attr_id_t attr_id, const char* doc)
{
    TCMAP* term2pos = tcmapnew();
    size_t size = strlen(doc);
    unsigned int pos = 0;
    offset_t offset = 0;
    while (pos < size) {
        const char* pc = &doc[pos];
        int term_size = get_term_size(pc);
        tcmapputcat(term2pos, pc, term_size, &offset, sizeof(offset));
        pos += get_char_size(doc[pos]);
        offset++;
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
        /**
         * 2 of (2 + pos_num) is for a document ID and a attribute ID. Each
         * number in postings needs 8 bytes at most.
         */
        char data[(2 + pos_num) * 8];
        int data_size;
        compress_posting(doc_id, attr_id, (int*)val, pos_num, data, &data_size);
        if (!tcbdbputdup(db->index, key, key_size, data, data_size)) {
            set_msg(db, "Can't register any term", tcbdberrmsg(tcbdbecode(db->index)));
            return 1;
        }
    }
    tcmapdel(term2pos);

    return 0;
}

static void
normalize_doc(char* dest, const char* src)
{
    BOOL is_prev_alpha = FALSE;
    char* last = dest - 1;
    char* p = dest;
    const char* q;
    for (q = src; *q != '\0'; q++) {
        char c = *q;
        if (isspace(c)) {
            if (is_prev_alpha) {
                *p = ' ';
                p++;
            }
            is_prev_alpha = FALSE;
            continue;
        }
        *p = c;
        is_prev_alpha = isalpha(c) ? TRUE : FALSE;
        last = p;
        p++;
    }
    *(last + 1) = '\0';
}

static o_attr_id_t
get_attr_id(oDB* db, const char* name)
{
    int sp;
    o_attr_id_t* pid = (o_attr_id_t*)tchdbget(db->attr2id, name, strlen(name), &sp);
    if (pid == NULL) {
        return -1;
    }
    return *pid;
}

static int
put_attr(oDB* db, o_doc_id_t doc_id, o_attr_id_t attr_id, const char* doc)
{
    TCHDB* hdb = db->attrs[attr_id];
    if (!tchdbput(hdb, &doc_id, sizeof(doc_id), doc, strlen(doc))) {
        set_msg(db, "Can't put attribute", tchdberrmsg(tchdbecode(hdb)));
        return 1;
    }
    return 0;
}

int
oDB_put(oDB* db, const char* doc, oAttr attrs[], int attrs_num)
{
    o_doc_id_t doc_id = db->next_doc_id;

#define NORMALIZE(normalized, text) do { \
    normalized = (char*)alloca(strlen((text)) + 1); \
    normalize_doc(normalized, (text)); \
} while (0)
    char* normalized;
    NORMALIZE(normalized, doc);
    if (put_index(db, doc_id, -1, normalized) != 0) {
        return 1;
    }
    if (put_doc(db, doc_id, normalized, strlen(normalized)) != 0) {
        return 1;
    }

    int i;
    for (i = 0; i < attrs_num; i++) {
        o_attr_id_t attr_id = get_attr_id(db, attrs[i].name);
        if (attr_id == -1) {
            return 1;
        }
        char* normalized;
        NORMALIZE(normalized, attrs[i].val);
        if (put_index(db, doc_id, attr_id, normalized) != 0) {
            return 1;
        }
        if (put_attr(db, doc_id, attr_id, normalized) != 0) {
            return 1;
        }
    }
#undef NORMALIZE

    db->next_doc_id++;

    return 0;
}

struct Posting {
    o_doc_id_t doc_id;
    o_attr_id_t attr_id;
    int term_size;
    offset_t* offset;
    int offset_size;
};

typedef struct Posting Posting;

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
        printf("<%d:%d", posting->doc_id, posting->offset[0]); \
        int j; \
        for (j = 1; j < posting->offset_size; j++) { \
            printf(",%d", posting->offset[j]); \
        } \
        printf(">"); \
    } \
    printf(">\n"); \
} while (0)

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
        oDB_set_msg_of_errno(db, "Can't allocate memory");
        return NULL;
    }
    posting->doc_id = 0;
    posting->attr_id = -1;
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
    offset_t* offset = (offset_t*)malloc(sizeof(offset_t) * offset_size);
    if (offset == NULL) {
        oDB_set_msg_of_errno(db, "malloc failed");
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
#define DECOMPRESS(num) do { \
    int size; \
    num = decompress_num(p, &size); \
    p += size; \
} while (0)
    o_doc_id_t tagged_doc_id;
    DECOMPRESS(tagged_doc_id);
    posting->doc_id = tagged_doc_id >> 1;
    if ((tagged_doc_id & 1) != 0) {
        o_attr_id_t attr_id;
        DECOMPRESS(attr_id);
        posting->attr_id = attr_id;
    }

    int offset_size;
    DECOMPRESS(offset_size);
    posting->offset_size = offset_size;
    offset_t* offset = (offset_t*)malloc(sizeof(offset_t) * offset_size);
    if (offset == NULL) {
        oDB_set_msg_of_errno(db, "Can't allocate offset");
        return NULL;
    }
    posting->offset = offset;

    int i;
    for (i = 0; i < offset_size; i++) {
        offset_t offset;
        DECOMPRESS(offset);
        posting->offset[i] = offset;
    }
#undef DECOMPRESS

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
        posting->term_size = BIGRAM_SIZE;
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
        if (posting1->attr_id < posting2->attr_id) {
            i++;
            continue;
        }
        if (posting2->attr_id < posting1->attr_id) {
            j++;
            continue;
        }

        int offset_size_min = posting1->offset_size < posting2->offset_size ? posting1->offset_size : posting2->offset_size;
        offset_t offset[offset_size_min];
        int offset_size = 0;
        int k = 0;
        int l = 0;
        while ((k < posting1->offset_size) && (l < posting2->offset_size)) {
            int end_offset = posting1->offset[k] + gap;
            if (end_offset < posting2->offset[l]) {
                k++;
                continue;
            }
            if (posting2->offset[l] < end_offset) {
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

static unsigned int
count_chars(const char* s)
{
    unsigned int n = 0;
    const char* pc = s;
    while (*pc != '\0') {
        pc += get_char_size(*pc);
        n++;
    };
    return n;
}

static oHits*
oHits_new(oDB* db, int num)
{
    size_t size = sizeof(oHits) + sizeof(o_doc_id_t) * (num - 1);
    oHits* hits = (oHits*)malloc(size);
    if (hits == NULL) {
        oDB_set_msg_of_errno(db, "oHits allocation failed");
        return NULL;
    }
    hits->num = num;
    return hits;
}

void
oHits_delete(oDB* db, oHits* hits)
{
    free(hits);
}

static int
search_fuzzily(oDB* db, const char* phrase, oHits** phits)
{
    struct FuzzyHit {
        unsigned int offsets_size;
        offset_t offsets[1];
    };
    typedef struct FuzzyHit FuzzyHit;

    int phrase_size = count_chars(phrase);
    int terms_num = phrase_size - 1;
    TCMAP* doc2hits = tcmapnew();
    unsigned int pos = 0;
    unsigned int i;
    for (i = 0; i < terms_num; i++) {
        const char* term = &phrase[pos];
        int term_size = get_term_size(term);
        TCLIST* posting_list = search_posting_list(db, term, term_size);
        int doc_num = tclistnum(posting_list);
        int i;
        for (i = 0; i < doc_num; i++) {
            Posting* posting = *((Posting**)tclistval2(posting_list, i));
            if (posting->attr_id != -1) {
                continue;
            }
            int sp;
            o_doc_id_t doc_id = posting->doc_id;
            TCLIST** p = (TCLIST**)tcmapget(doc2hits, &doc_id, sizeof(doc_id), &sp);
            if (p == NULL) {
                TCLIST* list = tclistnew();
                int i;
                for (i = 0; i < posting->offset_size; i++) {
                    FuzzyHit* hit = (FuzzyHit*)malloc(sizeof(FuzzyHit) + (terms_num - 1) * sizeof(offset_t));
                    if (hit == NULL) {
                        oDB_set_msg_of_errno(db, "malloc failed");
                        return 1;
                    }
                    hit->offsets_size = 1;
                    hit->offsets[0] = posting->offset[i];
                    tclistpush(list, &hit, sizeof(hit));
                }
                tcmapput(doc2hits, &doc_id, sizeof(doc_id), &list, sizeof(list));
                continue;
            }
            TCLIST* hits = *p;
            int hits_num = tclistnum(hits);
            TCLIST* new_hits = tclistnew();
            int i = 0;
            int j = 0;
            while ((i < hits_num) && (j < posting->offset_size)) {
                FuzzyHit* hit = *((FuzzyHit**)tclistval2(hits, i));
                if (posting->offset[j] < hit->offsets[0]) {
                    FuzzyHit* new_hit = (FuzzyHit*)malloc(sizeof(FuzzyHit) + (terms_num - 1) * sizeof(offset_t));
                    if (new_hit == NULL) {
                        oDB_set_msg_of_errno(db, "malloc failed");
                        return 1;
                    }
                    new_hit->offsets_size = 1;
                    new_hit->offsets[0] = posting->offset[j];
                    tclistpush(new_hits, &new_hit, sizeof(new_hit));
                    j++;
                    continue;
                }
                if (i < hits_num - 1) {
                    FuzzyHit* next_hit = *((FuzzyHit**)tclistval2(hits, i + 1));
                    if (next_hit->offsets[next_hit->offsets_size - 1] < posting->offset[j]) {
                        i++;
                        continue;
                    }
                }
                if (phrase_size / 2 < BIGRAM_SIZE + hit->offsets[hit->offsets_size - 1] - posting->offset[j]) {
                    FuzzyHit* new_hit = (FuzzyHit*)malloc(sizeof(FuzzyHit) + (terms_num - 1) * sizeof(offset_t));
                    if (new_hit == NULL) {
                        oDB_set_msg_of_errno(db, "malloc failed");
                        return 1;
                    }
                    new_hit->offsets_size = 1;
                    new_hit->offsets[0] = posting->offset[j];
                    tclistpush(new_hits, &new_hit, sizeof(new_hit));
                    i++;
                    continue;
                }
                hit->offsets[hit->offsets_size] = posting->offset[j];
                hit->offsets_size++;
                i++;
                j++;
            }
            int k;
            for (k = 0; k < tclistnum(new_hits); k++) {
                FuzzyHit* new_hit = *((FuzzyHit**)tclistval2(new_hits, k));
                int l;
                for (l = 0; l < tclistnum(hits) - 1; l++) {
                    FuzzyHit* hit = *((FuzzyHit**)tclistval2(hits, l));
                    if (new_hit->offsets[0] < hit->offsets[0]) {
                        break;
                    }
                }
                tclistinsert(hits, l, &new_hit, sizeof(new_hit));
            }
        }
        delete_posting_list(db, posting_list);
        pos += term_size;
    }

    tcmapiterinit(doc2hits);
    int n = 0;
    int sp;
    o_doc_id_t* key;
    while ((key = (o_doc_id_t*)tcmapiternext(doc2hits, &sp)) != NULL) {
        const TCLIST* hits = *((const TCLIST**)tcmapget(doc2hits, key, sizeof(key), &sp));
        BOOL flag = FALSE;
        int i;
        for (i = 0; i < tclistnum(hits); i++) {
            FuzzyHit* hit = *((FuzzyHit**)tclistval2(hits, i));
            if (terms_num / 2 <= hit->offsets_size) {
                flag = TRUE;
                break;
            }
        }
        if (!flag) {
            continue;
        }
        n++;
    }
    *phits = oHits_new(db, n);
    if (*phits == NULL) {
        return 1;
    }
    int m = 0;
    tcmapiterinit(doc2hits);
    while ((key = (o_doc_id_t*)tcmapiternext(doc2hits, &sp)) != NULL) {
        const TCLIST* hits = *((const TCLIST**)tcmapget(doc2hits, key, sizeof(key), &sp));
        int i;
        for (i = 0; i < tclistnum(hits); i++) {
            FuzzyHit* hit = *((FuzzyHit**)tclistval2(hits, i));
            if (terms_num / 2 <= hit->offsets_size) {
                (*phits)->doc_id[m] = *key;
                m++;
                break;
            }
        }
    }

#if 0
    tcmapiterinit(doc2hits);
    while ((key = (o_doc_id_t*)tcmapiternext(doc2hits, &sp)) != NULL) {
        TRACE("docid=%d", *key);
        const TCLIST* hits = *((const TCLIST**)tcmapget(doc2hits, key, sizeof(key), &sp));
        int i;
        for (i = 0; i < tclistnum(hits); i++) {
            TRACE("hit #%d", i);
            FuzzyHit* hit = *((FuzzyHit**)tclistval2(hits, i));
            int j;
            for (j = 0; j < hit->offsets_size; j++) {
                TRACE("offset[%d]=%d", j, hit->offsets[j]);
            }
        }
    }
#endif

    tcmapdel(doc2hits);
    return 0;
}

static int
search_phrase(oDB* db, const char* phrase, oHits** phits)
{
    int term_size = get_term_size(phrase);
    TCLIST* posting_list1 = search_posting_list(db, phrase, term_size);
    if (posting_list1 == NULL) {
        return 1;
    }
    if (tclistnum(posting_list1) == 0) {
        *phits = oHits_new(db, 0);
        return *phits != NULL ? 0 : 1;
    }

    size_t size = strlen(phrase);
    unsigned int pos = term_size;
    int gap = 0;
    unsigned int prev_pos = 0;
    while (pos < size) {
        int char_size = get_char_size(phrase[pos]);
        int from;
        if (size <= pos + char_size) {
            gap += 1;
            from = prev_pos + get_char_size(phrase[prev_pos]);
        }
        else {
            gap += 2;
            from = pos;
        }
        const char* term = &phrase[from];
        int term_size = get_term_size(term);
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
    *phits = oHits_new(db, tclistnum(posting_list1));
    if (*phits == NULL) {
        delete_posting_list(db, posting_list1);
        return 1;
    }
    int i;
    for (i = 0; i < num; i++) {
        Posting* posting = *((Posting**)tclistval2(posting_list1, i));
        (*phits)->doc_id[i] = posting->doc_id;
    }

    delete_posting_list(db, posting_list1);
    return 0;
}

static int
not_op(oDB* db, oHits* left, oHits* right, oHits** phits)
{
    int max_num = left->num;
    *phits = oHits_new(db, max_num);
    (*phits)->num = 0;

    TCMAP* map = tcmapnew();
    int val = 1;
    int i;
    for (i = 0; i < right->num; i++) {
        o_doc_id_t doc_id = right->doc_id[i];
        tcmapput(map, &doc_id, sizeof(doc_id), &val, sizeof(val));
    }
    for (i = 0; i < left->num; i++) {
        o_doc_id_t doc_id = left->doc_id[i];
        int sp;
        const void* val = tcmapget(map, &doc_id, sizeof(doc_id), &sp);
        if (val != NULL) {
            continue;
        }
        (*phits)->doc_id[(*phits)->num] = doc_id;
        (*phits)->num++;
    }
    tcmapdel(map);

    return 0;
}

static int
and_op(oDB* db, oHits* left, oHits* right, oHits** phits)
{
    int max_num = left->num < right->num ? right->num : left->num;
    *phits = oHits_new(db, max_num);
    (*phits)->num = 0;

    TCMAP* map = tcmapnew();
    int val = 1;
    int i;
    for (i = 0; i < left->num; i++) {
        o_doc_id_t doc_id = left->doc_id[i];
        tcmapput(map, &doc_id, sizeof(doc_id), &val, sizeof(val));
    }
    for (i = 0; i < right->num; i++) {
        o_doc_id_t doc_id = right->doc_id[i];
        int sp;
        const void* val = tcmapget(map, &doc_id, sizeof(doc_id), &sp);
        if (val == NULL) {
            continue;
        }
        (*phits)->doc_id[(*phits)->num] = doc_id;
        (*phits)->num++;
    }
    tcmapdel(map);

    return 0;
}

static int
or_op(oDB* db, oHits* left, oHits* right, oHits** phits)
{
    int max_num = left->num + right->num;
    *phits = oHits_new(db, max_num);
    (*phits)->num = 0;

    TCMAP* map = tcmapnew();
    int val = 1;
    int i;
    for (i = 0; i < left->num; i++) {
        o_doc_id_t doc_id = left->doc_id[i];
        tcmapput(map, &doc_id, sizeof(doc_id), &val, sizeof(val));
    }
    for (i = 0; i < right->num; i++) {
        o_doc_id_t doc_id = right->doc_id[i];
        tcmapput(map, &doc_id, sizeof(doc_id), &val, sizeof(val));
    }
    tcmapiterinit(map);
    const void* key;
    int sp;
    while ((key = tcmapiternext(map, &sp)) != NULL) {
        o_doc_id_t doc_id = *((o_doc_id_t*)key);
        (*phits)->doc_id[(*phits)->num] = doc_id;
        (*phits)->num++;
    }
    tcmapdel(map);

    return 0;
}

static int
eval(oDB* db, oNode* node, oHits** phits)
{
    if ((node->type == NODE_PHRASE) || (node->type == NODE_FUZZY)) {
        const char* phrase = tcxstrptr(node->u.phrase.s);
        int (*f)(oDB*, const char*, oHits**) = node->type == NODE_PHRASE ? search_phrase : search_fuzzily;
        return f(db, phrase, phits);
    }
    oHits* left_hits = NULL;
    if (eval(db, node->u.logical_op.left, &left_hits) != 0) {
        return 1;
    }
    oHits* right_hits = NULL;
    if (eval(db, node->u.logical_op.right, &right_hits) != 0) {
        return 1;
    }
    int (*f)(oDB*, oHits*, oHits*, oHits**);
    switch (node->type) {
    case NODE_AND:
        f = and_op;
        break;
    case NODE_OR:
        f = or_op;
        break;
    case NODE_NOT:
        f = not_op;
        break;
    default:
        return 1;
        break;
    }
    int status = f(db, left_hits, right_hits, phits);
    oHits_delete(db, left_hits);
    oHits_delete(db, right_hits);

    return status;
}

int
oDB_search(oDB* db, const char* phrase, oHits** phits)
{
    oNode* node = oParser_parse(db, phrase);
    return eval(db, node, phits);
}

char*
oDB_get_attr(oDB* db, o_doc_id_t doc_id, const char* attr)
{
    o_attr_id_t attr_id = get_attr_id(db, attr);
    if (attr_id == -1) {
        set_msg(db, "Attribute not found", attr);
        return NULL;
    }
    int sp;
    return (char*)tchdbget(db->attrs[attr_id], &doc_id, sizeof(doc_id), &sp);
}

/**
 * vim: tabstop=4 shiftwidth=4 expandtab softtabstop=4
 */
