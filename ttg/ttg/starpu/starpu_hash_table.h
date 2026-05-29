#ifndef TTG_STARPU_HASH_H
#define TTG_STARPU_HASH_H

#include <type_traits>
#include <unordered_map>
#include "ttg/starpu/task.h"
#include <boost/unordered/concurrent_flat_map.hpp>
#include <starpu.h>



namespace ttg_starpu {


    template <typename TT, typename hashtable_keyT>
    class starpu_hash_table : public boost::unordered::concurrent_flat_map<hashtable_keyT, detail::starpu_ttg_task_t<TT>*, ttg::hash<hashtable_keyT>> {
        private:
        using task_t = detail::starpu_ttg_task_t<TT>;
        using flat_map_t = boost::unordered::concurrent_flat_map<hashtable_keyT, task_t*, ttg::hash<hashtable_keyT>>;

        public:
        
        starpu_hash_table(): flat_map_t() {}

        // Insert a task with a given key only  if there is no element in the table with an equivalent key.
        // Return true if an insert took place.
        bool starpu_hash_table_insert(hashtable_keyT key, task_t* task){
            return this->insert({key, task});
        }

        //Visit an item if the key is present and apply func to it.
        //return true if the key is present, false otherwise.
        template <typename F>
        bool starpu_hash_table_visit(hashtable_keyT key, F func){
            return this->visit(key, [&](auto& item){
                func(item.second);
            });
        }
        
        //Apply func to all items in the table.
        template <typename F>
        void starpu_hash_table_visit_all(F func, void* cb_data){
            this->visit_all([&](auto& item){
                func(item.second, cb_data);
            });
        }
        //Emplace an item if the key is not present, otherwise visit the item and apply func to it.
        template <typename F>
        bool starpu_hash_table_emplace_or_visit(hashtable_keyT key, F &&func, task_t*&& new_task){
            return this->emplace_or_visit(key, new_task, [&](auto& item){
                func(item.second);
            });
        }

        //Emplace an item if the key is not present and apply F1 to it, otherwise visit the item and apply F2 to it.
        template <typename F1, typename F2>
        bool starpu_hash_table_try_emplace_and_visit(hashtable_keyT starpu_key, F1 &&func_new, F2 &&func_visit){
            return this->try_emplace_and_visit(starpu_key, [&](auto& item){ item.second = func_new(); }, [&](auto& item){ func_visit(item.second); });
        }

        // Remove an item if the key is present and func returns true on it.
        // Returns deleted item pointer.
        //TODO : Need to delete task later
        template <typename F, typename... Args>
        task_t* starpu_hash_table_remove(hashtable_keyT key,F &&func, Args&&... args){
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