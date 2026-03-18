#ifndef TTG_STARPU_HASH_H
#define TTG_STARPU_HASH_H

#include <unordered_map>
#include <starpu.h>

namespace ttg::starpu {
    typedef uintptr_t starpu_key_t;
    typedef struct starpu_hash_table_item_s {
        starpu_hash_table_item_t *next_item;
        uint64_t hash64;
        starpu_key_t key;
    } starpu_hash_table_item_t;

    typedef std::unordered_map<starpu_key_t,starpu_hash_table_item_t> starpu_hash_table_t;

    void starpu_hash_table_unlock_bucket(starpu_hash_table_t hash_table, starpu_key_t key);

    void starpu_hash_table_lock_bucket(starpu_hash_table_t hash_table, starpu_key_t key);

    void starpu_hash_table_insert(starpu_hash_table_t hash_table, starpu_hash_table_item_t item);

    void *starpu_hash_table_remove(starpu_hash_table_t hash_table, starpu_key_t key);


}


#endif