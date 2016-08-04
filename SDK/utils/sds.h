#ifndef __SDS_H
#define __SDS_H

#define SDS_MAX_PREALLOC (1024*1024)

#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

typedef char *sds;

/* Note: sdshdr5 is never used, we just access the flags byte directly.
 * However is here to document the layout of type 5 SDS strings. */
struct __attribute__ ((__packed__)) sdshdr5 {
    unsigned char flags; /* 3 lsb of type, and 5 msb of string length */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr8 {
    uint8_t len; /* used */
    uint8_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr16 {
    uint16_t len; /* used */
    uint16_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr32 {
    uint32_t len; /* used */
    uint32_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};
struct __attribute__ ((__packed__)) sdshdr64 {
    uint64_t len; /* used */
    uint64_t alloc; /* excluding the header and null terminator */
    unsigned char flags; /* 3 lsb of type, 5 unused bits */
    char buf[];
};

#define SDS_TYPE_5  0
#define SDS_TYPE_8  1
#define SDS_TYPE_16 2
#define SDS_TYPE_32 3
#define SDS_TYPE_64 4
#define SDS_TYPE_MASK 7
#define SDS_TYPE_BITS 3
#define SDS_HDR_VAR(T,s) struct sdshdr##T *sh = (struct sdshdr##T*)((s)-(sizeof(struct sdshdr##T)));
#define SDS_HDR(T,s) ((struct sdshdr##T *)((s)-(sizeof(struct sdshdr##T))))
#define SDS_TYPE_5_LEN(f) ((f)>>SDS_TYPE_BITS)

// 返回s当前使用的长度
static inline size_t sds_len(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->len;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->len;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->len;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->len;
    }
    return 0;
}

// 返回s当前可用长度
static inline size_t sds_avail(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5: {
            return 0;
        }
        case SDS_TYPE_8: {
            SDS_HDR_VAR(8,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_16: {
            SDS_HDR_VAR(16,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_32: {
            SDS_HDR_VAR(32,s);
            return sh->alloc - sh->len;
        }
        case SDS_TYPE_64: {
            SDS_HDR_VAR(64,s);
            return sh->alloc - sh->len;
        }
    }
    return 0;
}

// 设置s已使用长度
static inline void sds_set_len(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len = newlen;
            break;
    }
}

// 增加已使用长度
static inline void sds_inc_len(sds s, size_t inc) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            {
                unsigned char *fp = ((unsigned char*)s)-1;
                unsigned char newlen = SDS_TYPE_5_LEN(flags)+inc;
                *fp = SDS_TYPE_5 | (newlen << SDS_TYPE_BITS);
            }
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->len += inc;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->len += inc;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->len += inc;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->len += inc;
            break;
    }
}

// 返回总长度
/* sds_alloc() = sds_avail() + sds_len() */
static inline size_t sds_alloc(const sds s) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            return SDS_TYPE_5_LEN(flags);
        case SDS_TYPE_8:
            return SDS_HDR(8,s)->alloc;
        case SDS_TYPE_16:
            return SDS_HDR(16,s)->alloc;
        case SDS_TYPE_32:
            return SDS_HDR(32,s)->alloc;
        case SDS_TYPE_64:
            return SDS_HDR(64,s)->alloc;
    }
    return 0;
}

/*
 * 设置总长度值
 */
static inline void sds_set_alloc(sds s, size_t newlen) {
    unsigned char flags = s[-1];
    switch(flags&SDS_TYPE_MASK) {
        case SDS_TYPE_5:
            /* Nothing to do, this type has no total allocation info. */
            break;
        case SDS_TYPE_8:
            SDS_HDR(8,s)->alloc = newlen;
            break;
        case SDS_TYPE_16:
            SDS_HDR(16,s)->alloc = newlen;
            break;
        case SDS_TYPE_32:
            SDS_HDR(32,s)->alloc = newlen;
            break;
        case SDS_TYPE_64:
            SDS_HDR(64,s)->alloc = newlen;
            break;
    }
}

/*
 *带长度的string结构申请
 */
sds sds_new_len(const void *init, size_t initlen);
/*
 *不带长度的string结构请
 */
sds sds_new(const char *init);
/*
 *生成空string结构
 */
sds sds_empty(void);
/*
 *复制一个string结构
 */
sds sds_dup(const sds s);
/*
 *注销一个string结构
 */
void sds_free(sds s);
/*
 *扩展len个空间并赋空置
 */
sds sds_grow_zero(sds s, size_t len);
/*
 *追加len个长度内容到sds中
 */
sds sds_append_len(sds s, const void *t, size_t len);
/*
 *追加字符串到sds中
 */
sds sds_append_buffer(sds s, const char *t);
/*
 *追加sds到当前sds中
 */
sds sds_append_string(sds s, const sds t);
/*
 *复制长度为len的字符串到sds中
 */
sds sds_copy_len(sds s, const char *t, size_t len);
/*
 *复制字符串到sds中
 */
sds sds_copy(sds s, const char *t);

/*
 *格式化输入内容到sds中
 */
#ifdef __GNUC__
sds sds_append_printf(sds s, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
sds sds_append_printf(sds s, const char *fmt, ...);
#endif

/*
 *格式化输入内容到sds中 扩展了sds结构
 * format specifiers:
 *
 * %s - C String
 * %S - SDS string
 * %i - signed int
 * %I - 64 bit signed integer (long long, int64_t)
 * %u - unsigned int
 * %U - 64 bit unsigned integer (unsigned long long, uint64_t)
 * %% - Verbatim "%" character.
 */
sds sds_append_format(sds s, char const *fmt, ...);

/*
 *sds前后去除cset中包含的字符
 */
sds sds_trim(sds s, const char *cset);
/*
 *范围截取
 */
void sds_range(sds s, int start, int end);
/*
 *修改sds中的长度到字符串的长度
 */
void sds_update_len(sds s);
/*
 *清空sds中的内容
 */
void sds_clear(sds s);

/*
 *sds内容对比 按照二进制内容对比
 */
int sds_cmp(const sds s1, const sds s2);
/*
 *字符串内容分割 返回sds数组 以及分割的sds个数
 */
sds *sds_split_len(const char *s, int len, const char *sep, int seplen, int *count);
/*
 *释放sds数组空间
 */
void sds_free_splitres(sds *tokens, int count);
/*
 *字符串全部转小写
 */
void sds_to_lower(sds s);
/*
 *字符串全部转大写
 */
void sds_to_upper(sds s);
/*
 *转64位数字为sds
 */
sds sds_from_ll(long long value);
/*
 *从s中获得long long的数值
 */
int sds_to_ll(const char *s, size_t slen, long long *value); 
/*
 *追加转移字符到sds结构中
 */
sds sds_append_repr(sds s, const char *p, size_t len);
sds *sds_split_targs(const char *line, int *argc);
/*
 *对sds中的字符做从from到to的转化 from to 一一对应
 *
 *setlen是from和to的长度
 */
sds sds_map_chars(sds s, const char *from, const char *to, size_t setlen);
/*
 *通过sep链接argv中的参数 返回sds
 */
sds sds_join(char **argv, int argc, char *sep);
/*
 *通过sep链接argv中的参数 argv是sds结构
 */
sds sds_join_string(sds *argv, int argc, const char *sep, size_t seplen);

/* Low level functions exposed to the user API */
/*
 *为s扩展addlen个长度空间
 */
sds sds_make_room_for(sds s, size_t addlen);
/*
 *为s增加incr个长度的空间
 */
void sds_incr_len(sds s, int incr);
/*
 *释放s中未被使用的空间
 */
sds sds_remove_free_space(sds s);
/*
 *返回s申请的总空间长度
 */
size_t sds_alloc_size(sds s);
/*
 *返回s的头指针
 */
void *sds_alloc_ptr(sds s);

#endif

