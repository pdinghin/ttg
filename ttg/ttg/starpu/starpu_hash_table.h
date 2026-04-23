#ifndef TTG_STARPU_HASH_H
#define TTG_STARPU_HASH_H

#include <type_traits>
#include <unordered_map>
#include "ttg/starpu/task.h"
#include <boost/unordered/concurrent_flat_map.hpp>
#include <starpu.h>

typedef uintptr_t starpu_key_t;

namespace ttg_starpu {


    template <typename TT>
    class starpu_hash_table : public boost::unordered::concurrent_flat_map<starpu_key_t, detail::starpu_ttg_task_t<TT>*> {
        private:
        using task_t = detail::starpu_ttg_task_t<TT>;
        using flat_map_t = boost::unordered::concurrent_flat_map<starpu_key_t, task_t*>;

        public:
        
        starpu_hash_table(): flat_map_t() {}

        //Visit an item if the key is present and apply func to it.
        //return true if the key is present, false otherwise.
        template <typename KeyT, typename F>
        bool starpu_hash_table_visit(KeyT key, F func){
            return this->visit(key, [&](typename flat_map_t::value_type &item){
                func(item.second);
                return true;
            });
        }
        
        //Emplace an item if the key is not present, otherwise visit the item and apply func to it.
        template <typename KeyT, typename F>
        bool starpu_hash_table_emplace_or_visit(KeyT key, F &&func, task_t*&& new_task){
            return this->emplace_or_visit(key, new_task, [&](auto& item){
                func(item.second);
                return true;
            });
        }

        //Emplace an item if the key is not present and apply F1 to it, otherwise visit the item and apply F2 to it.
        template <typename KeyT, typename F1, typename F2>
        bool starpu_hash_table_try_emplace_and_visit(KeyT key, F1 &&func_new, F2 &&func_visit){
            return this->try_emplace_and_visit(key, [&](){ return func_new(key); }, [&](auto& item){ func_visit(item.second); });
        }

        // Remove an item if the key is present and func returns true on it.
        // Returns deleted item pointer.
        //TODO : Need to delete task later
        template <typename starpu_key_t, typename F, typename... Args>
        task_t* starpu_hash_table_remove(starpu_key_t key,F &&func, Args&&... args){
            task_t* task = nullptr;
            this->erase_if(key, [&](auto& item){
                if(func(item.second,std::forward<Args>(args)...)){
                    task = item.second;
                    return true;
                }                
                return false;
            });
            return task;
        }

    };
} // namespace ttg_starpu

#endif