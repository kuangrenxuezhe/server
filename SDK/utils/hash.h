/*
    Copyright (c) 2012-2013 Martin Sustrik  All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_HASH_INCLUDED
#define NN_HASH_INCLUDED

#include "list.h"

#include <stddef.h>
#include <stdint.h>

/*  Use for initialising a hash item statically. */
#define NN_HASH_NOTINHASH ((struct hash_item*) -1)
#define NN_HASH_ITEM_INITIALIZER {0xffff, NN_HASH_NOTINHASH}

typedef struct hash_item {
    void *key;
    struct hash_item *next;
} hash_item;

typedef struct hash_func {
    uint32_t (*key_gen)(const void *key);
    int (*key_cmp)(const void *key1, const void *key2);
    void (*item_term)(void *key);
} hash_func;

typedef struct hash {
    uint32_t slots;
    uint32_t items;
    hash_func *op;
    hash_item **array;
} hash;

typedef struct hash_iterator {
    hash *h;
    long index;
    hash_item *entry;
    hash_item *nentry;
} hash_iterator;

#define hash_compare_keys(d, key1, key2) \
    (((d)->op->key_cmp) ? \
     (d)->op->key_cmp(key1, key2) : \
     (key1) == (key2))
#define hash_item_term(d, entry) \
    if((d)->op->item_term)  \
(d)->op->item_term(entry) 
#define hash_key(d, key) (d)->op->key_gen(key)

/*  初始化hash表 */
void nn_hash_init (hash *self);

/*  析构hash表 */
void nn_hash_term (hash *self);

/* 释放资源 */
void nn_hash_free_all(hash *self);

/*  设置操作函数 */
void nn_hash_set_op(hash *self, hash_func *op);

/*  添加一项到hash 返回值 0 插入成功 -1 插入值已存在*/
int nn_hash_insert (hash *self, void *key, hash_item *item);

/*  添加一项到hash 返回值 0 插入成功 非0 表示数据已经被更新*/
hash_item *nn_hash_set (hash *self, void *key, hash_item *item);

/*  从hash中查找键为key的项 0没找到 非0 为结果*/
hash_item *nn_hash_get (hash *self, void *key);

/*  从hash表中删除一项  0 成功 -1  没有找到item*/
int nn_hash_erase (hash *self, hash_item *item);

/*  初始化hash 最小单位元素*/
void nn_hash_item_init (hash_item *self);

/*  析构hash 最小单位元素. */
void nn_hash_item_term (hash_item *self);

/* 创建hash遍历器 */
hash_iterator *nn_hash_iter_init(hash *self);

/* 遍历hash中的节点 */
hash_item *nn_hash_item_next(hash_iterator *iter);

/* 析构hash遍历器 */
void nn_hash_iter_term(hash_iterator *iter);

/* 数字生成hashkey算法 Thomas Wang */
uint32_t hash_int_func(uint32_t key);

/* 数字生成hashkey算法 */
uint32_t hash_int_func_x (uint32_t key);

/* 字符串生成hashkey算法 大小写敏感 MurmurHash2*/
uint32_t hash_string_func(const void *key, int len);

/* 字符串生成hashkey算法 大小写不敏感 djb hash*/
uint32_t hash_string_case_func(const unsigned char *buf, int len); 

#endif

