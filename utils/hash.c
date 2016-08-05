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
#include "ctype.h"
#include "hash.h"
#include "std.h"
#include "alloc.h"
#include "err.h"

#define NN_HASH_INITIAL_SLOTS 32

uint32_t key_gen(const void *key) 
{
    uint32_t k = (uint64_t)key;
    return hash_int_func(k);
}

static struct hash_func default_func = {
    key_gen,
    NULL,
    NULL,
};

void nn_hash_init (hash *self)
{
    uint32_t i;

    self->slots = NN_HASH_INITIAL_SLOTS;
    self->items = 0;
    self->op = &default_func;
    self->array = (hash_item **)nn_malloc (sizeof ( hash_item*) * NN_HASH_INITIAL_SLOTS);
    alloc_assert (self->array);
    for (i = 0; i != NN_HASH_INITIAL_SLOTS; ++i)
        self->array[i] = NULL;
}

void nn_hash_term (hash *self)
{
    uint32_t i;

    for (i = 0; i != self->slots; ++i)
        nn_assert (self->array [i] == NULL);
    nn_free (self->array);
}

void nn_hash_set_op(hash *self, hash_func *op)
{
    self->op = op;
}

void nn_hash_free_all(hash *self)
{
    hash_item *item;
    hash_item **array;
    int i;

    array = self->array;
    for (i = 0; i != self->slots; ++i) 
    {
        while(array[i] != NULL)
        {
            item = array[i];
            array[i] = array[i]->next;

            hash_item_term(self, item);
        }
    }
}

static void nn_hash_rehash (hash *self) 
{
    hash_item **oldarray;
    hash_item *item;
    hash_item *prev;
    hash_item *it;
    uint32_t oldslots;
    uint32_t newslot;
    uint32_t i;

    /*  Allocate new double-sized array of slots. */
    oldslots = self->slots;
    oldarray = self->array;
    self->slots *= 2;
    self->array = (hash_item **)nn_malloc (sizeof ( hash_item *) * self->slots);
    alloc_assert (self->array);
    for (i = 0; i != self->slots; ++i)
        self->array [i] = NULL;

    /*  Move the items from old slot array to new slot array. */
    for (i = 0; i != oldslots; ++i) 
    {
        while(oldarray [i] != NULL)
        {
            item = oldarray[i];
            oldarray[i] = item->next;

            newslot = hash_key(self, item->key) % self->slots;

            item->next = self->array[newslot];
            self->array[newslot] = item;
        }
    }

    /*  Deallocate the old array of slots. */
    nn_free (oldarray);
}

int nn_hash_insert (hash *self, void *key, hash_item *item)
{
    hash_item *it;
    uint32_t i;

    nn_assert (item->next == NN_HASH_NOTINHASH);
    i = hash_key(self, key) % self->slots;

    for (it = self->array[i]; it != NULL; it = it->next) 
    {
        if(hash_compare_keys(self, key, it->key))
            return -1;                 //该key 已经存在
    }

    item->key = key;
    item->next = self->array[i];
    self->array[i] = item;

    ++self->items;

    /*  If the hash is getting full, double the amount of slots and
        re-hash all the items. */
    if (nn_slow (self->items > self->slots * 2 && self->slots < 0x80000000))
        nn_hash_rehash(self);
    return 0;
}
/*  添加一项到hash 返回值 0 插入成功 非0 表示数据已经被更新*/
hash_item *nn_hash_set (hash *self, void *key, hash_item *item)
{
    hash_item *prev = NULL;
    hash_item *it;
    uint32_t i;

    nn_assert (item->next == NN_HASH_NOTINHASH);
    i = hash_key(self, key) % self->slots;

    item->key = key;
    for (it = self->array[i]; it != NULL; it = it->next) 
    {
        if(hash_compare_keys(self, key, it->key))
        {
            if(prev == NULL)
            {
                item->next = it->next;
                self->array[i] = item;
            }
            else
            {
                item->next = it->next;
                prev->next = item;
            }
            return it;
        }  
        prev = it;
    }

    item->next = self->array[i];
    self->array[i] = item;

    ++self->items;

    /*  If the hash is getting full, double the amount of slots and
        re-hash all the items. */
    if (nn_slow (self->items > self->slots * 2 && self->slots < 0x80000000))
        nn_hash_rehash(self);
    return 0;
}

int nn_hash_erase (hash *self, hash_item *item)
{
    hash_item *it;
    hash_item *prev;
    uint32_t slot;

    nn_assert (item->next != NN_HASH_NOTINHASH);
    slot = hash_key(self, item->key) % self->slots;

    prev = NULL;
    for (it = self->array[slot]; it != NULL; it = it->next) 
    {
        if(it == item)
            break;
        prev = it;
    }
    if(it != item)
        return -1;

    if(prev == NULL)
        self->array[slot] = item->next;
    else
        prev->next = it->next;

    --self->items;
    item->next = NN_HASH_NOTINHASH;

    return 0;    
}

hash_item *nn_hash_get (hash *self, void *key)
{
    hash_item *item;
    uint32_t slot;

    slot = hash_key(self, key) % self->slots;

    for (item = self->array[slot]; item != NULL; item = item->next) 
    {
        if (hash_compare_keys(self, key, item->key))
            return item;
    }

    return NULL;
}

void nn_hash_item_init (hash_item *self)
{
    self->key = NULL;
    self->next = NN_HASH_NOTINHASH; 
}

void nn_hash_item_term (hash_item *self)
{
    nn_assert (self->next == NN_HASH_NOTINHASH);
}

/*
 *创建hash遍历器
 */
hash_iterator *nn_hash_iter_init(hash *self)
{
    hash_iterator *iter = (hash_iterator *)nn_malloc(sizeof( hash_iterator));
    iter->h = self;
    iter->index = -1;
    iter->entry = NULL;
    iter->nentry = NULL;
    return iter;
}
/*
 *遍历hash中的节点
 */
hash_item *nn_hash_item_next( hash_iterator *iter)
{
    while (1) {
        if (iter->entry == NULL) 
        {
            iter->index++;
            if(iter->index >= iter->h->slots)
                return NULL;
            iter->entry = iter->h->array[iter->index];
        } 
        else 
        {
            iter->entry = iter->nentry;
        }
        if (iter->entry) {
            /* We need to save the 'next' here, the iterator user
             * may delete the entry we are returning. */
            iter->nentry = iter->entry->next;
            return iter->entry;
        }
    }
    return NULL;
}
/*
 *析构hash遍历器
 */
void nn_hash_iter_term( hash_iterator *iter)
{
    if(iter == 0)
        return;
    nn_free(iter);
}

static uint32_t dict_hash_function_seed = 5381;

void hash_set_func_seed(uint32_t seed) {
    dict_hash_function_seed = seed;
}

uint32_t hash_get_func_seed(void) {
    return dict_hash_function_seed;
}

/* Thomas Wang's 32 bit Mix Function */
uint32_t hash_int_func(uint32_t key)
{
    key += ~(key << 15);
    key ^=  (key >> 10);
    key +=  (key << 3);
    key ^=  (key >> 6);
    key += ~(key << 11);
    key ^=  (key >> 16);
    return key;
}

uint32_t hash_int_func_x (uint32_t key)
{
    /*  TODO: This is a randomly chosen hashing function. Give some thought
        to picking a more fitting one. */
    key = (key ^ 61) ^ (key >> 16);
    key += key << 3;
    key = key ^ (key >> 4);
    key = key * 0x27d4eb2d;

    key = key ^ (key >> 15);
    return key;
}

/* MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */
uint32_t hash_string_func(const void *key, int len) 
{
    /* 'm' and 'r' are mixing constants generated offline.
       They're not really 'magic', they just happen to work well.  */
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    /* Initialize the hash to a 'random' value */
    uint32_t h = seed ^ len;

    /* Mix 4 bytes at a time into the hash */
    const unsigned char *data = (const unsigned char *)key;

    while(len >= 4) {
        uint32_t k = *(uint32_t*)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    /* Handle the last few bytes of the input array  */
    switch(len) {
        case 3: h ^= data[2] << 16;
        case 2: h ^= data[1] << 8;
        case 1: h ^= data[0]; h *= m;
    };

    /* Do a few final mixes of the hash to ensure the last few
     * bytes are well-incorporated. */
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (uint32_t)h;
}

/* And a case insensitive hash function (based on djb hash) */
uint32_t hash_string_case_func(const unsigned char *buf, int len) 
{
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++)); /* hash * 33 + c */
    return hash;
}


#ifdef HASH_TEST_MAIN

typedef struct hash_entry
{
    int v;
    hash_item item;    
} hash_entry;

int main(int argc, char** argv)
{
    hash_item *it; 
    hash_iterator *iter;    
    hash_entry *item;
    hash hash;
    int i;

    nn_hash_init(&hash);

    for(i=0; i<10000; i++)
    {
        item = (hash_entry *)nn_malloc(sizeof(*item));
        nn_hash_item_init(&item->item);
        item->v = 10000-i;

        nn_hash_insert(&hash, (void *)i, &item->item);
    }
    iter = nn_hash_iter_init(&hash);
    i=1;
    while(it = nn_hash_item_next(iter))
    {
        i++;
        item = nn_cont (it, struct hash_entry, item);
        if(i%1000 == 0)
            printf("iter i:%d key: %lld  value: %d\n",  i, (uint64_t)it->key, item->v);
    }

    nn_hash_iter_term(iter);
    for(i=0; i<10000; i++)
    {
        it = nn_hash_get(&hash, (void *)i);
        item = nn_cont (it, struct hash_entry, item);
        if(i%100 == 0)
            printf("find i:%d key: %lld  value: %d\n", i, (uint64_t)it->key, item->v);
        nn_hash_erase(&hash, it);
        nn_free(item);
    }
    printf("end!!!!!!!!!!\n");
    nn_hash_term(&hash);
    return 0;
}
#endif
