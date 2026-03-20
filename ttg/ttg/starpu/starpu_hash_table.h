#ifndef TTG_STARPU_HASH_H
#define TTG_STARPU_HASH_H

#include <unordered_map>
#include <starpu.h>

typedef uintptr_t starpu_key_t;
typedef struct starpu_hash_table_item_s starpu_hash_table_item_t;
struct starpu_hash_table_item_s {
    starpu_hash_table_item_t *next_item;
    uint64_t hash64;
    starpu_key_t key;
};
typedef std::unordered_map<starpu_key_t,starpu_hash_table_item_t> starpu_hash_table_t;


void starpu_hash_table_unlock_bucket(starpu_hash_table_t *hash_table, starpu_key_t key){

}

void starpu_hash_table_lock_bucket(starpu_hash_table_t *hash_table, starpu_key_t key){

}

void starpu_hash_table_insert(starpu_hash_table_t *hash_table, starpu_hash_table_item_t *item){

}

void *starpu_hash_table_remove(starpu_hash_table_t *hash_table, starpu_key_t key){
    starpu_hash_table_item_t *item ;
    return item;
}

void *starpu_hash_table_find(starpu_hash_table_t *hash_table, starpu_key_t key){
    starpu_hash_table_item_t *item ;
    return item;
}
// namespace ttg_starpu {

  
//     class starpu_hash_table : public boost::unordered::concurrent_node_map<starpu_key_t, starpu_hash_table_item_t> {
//         public:
//             starpu_hash_table(): boost::unordered::concurrent_node_map<starpu_key_t, starpu_hash_table_item_t>() {}

//             void starpu_hash_table_unlock_bucket(starpu_hash_table_t *hash_table, starpu_key_t key){
//                 // No locking needed for concurrent_node_map
//             }

//             void starpu_hash_table_lock_bucket(starpu_hash_table_t *hash_table, starpu_key_t key){
//                 // No locking needed for concurrent_node_map
//             }

//             void starpu_hash_table_insert(starpu_hash_table_t *hash_table, starpu_hash_table_item_t *item){
//                 this.insert({item->key, *item});
//             }

//             void *starpu_hash_table_remove(starpu_hash_table_t *hash_table, starpu_key_t key){
//                 void * task;
//                 this.erase_if(key, [&](auto x){
//                     //Get task int starpu_item or the entire item ?
//                     return true; 
//                 });
//                 return task;
//             }

//             template <typename starpu_key_t, typename F, typename... Args>
//             int starpu_hash_table_find(starpu_key_t key,F func, Args&&... args){
//                 int ret; 
//                 // ret = this.visit(key,[&](auto& node) {
//                 //     func(node.second, std::forward<Args>(args)...);
//                 // });
//                 return ret;
//             }

//     }
// } // namespace ttg_starpu

// using starpu_hash_table_t = ttg_starpu::starpu_hash_table;

#endif