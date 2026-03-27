// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_TASK_H
#define TTG_STARPU_TASK_H

#include "ttg/starpu/ttg_data_copy.h"
#include "ttg/starpu/starpu_hash_table.h"

#include <starpu.h>
#include <array>
#include <atomic>
#include <cstring>

#define MAX_PARAM_COUNT 16  // Maximum number of task parameters

namespace ttg_starpu {


  namespace detail {

    /* StarPU device support structure
     * TODO: Implement full device support when StarPU device integration is ready */
    struct device_ptr_t {
      // Placeholder for future device support
      // starpu_data_handle_t* data_handles = nullptr;
    };

    template<bool SupportDevice>
    struct device_state_t {
      static constexpr bool support_device = false;
      static constexpr size_t num_flows = 0;
      device_state_t() { }
      static constexpr device_ptr_t* dev_ptr() {
        return nullptr;
      }
    };

    template<>
    struct device_state_t<true> {
      static constexpr bool support_device = false;
      static constexpr size_t num_flows = MAX_PARAM_COUNT;
      // TODO: Add device-specific structures when device support is implemented
      device_ptr_t* dev_ptr() {
        return nullptr;
      }
    };

    /* Data flags for tracking read/write patterns */
    enum class ttg_starpu_data_flags : uint8_t {
      NONE            = 0,
      SINGLE_READER   = 1 << 0,
      MULTIPLE_READER = 1 << 1,
      SINGLE_WRITER   = 1 << 2,
      MULTIPLE_WRITER = 1 << 3,
      IS_MODIFIED     = 1 << 4,
      MARKED_PUSHOUT  = 1 << 5
    };

    inline
    ttg_starpu_data_flags operator|(ttg_starpu_data_flags lhs, ttg_starpu_data_flags rhs) {
      using flags_type = std::underlying_type<ttg_starpu_data_flags>::type;
      return ttg_starpu_data_flags(static_cast<flags_type>(lhs) | static_cast<flags_type>(rhs));
    }

    inline
    ttg_starpu_data_flags operator|=(ttg_starpu_data_flags& lhs, ttg_starpu_data_flags rhs) {
      using flags_type = std::underlying_type<ttg_starpu_data_flags>::type;
      lhs = ttg_starpu_data_flags(static_cast<flags_type>(lhs) | static_cast<flags_type>(rhs));
      return lhs;
    }

    inline
    uint8_t operator&(ttg_starpu_data_flags lhs, ttg_starpu_data_flags rhs) {
      using flags_type = std::underlying_type<ttg_starpu_data_flags>::type;
      return static_cast<flags_type>(lhs) & static_cast<flags_type>(rhs);
    }

    inline
    ttg_starpu_data_flags operator&=(ttg_starpu_data_flags& lhs, ttg_starpu_data_flags rhs) {
      using flags_type = std::underlying_type<ttg_starpu_data_flags>::type;
      lhs = ttg_starpu_data_flags(static_cast<flags_type>(lhs) & static_cast<flags_type>(rhs));
      return lhs;
    }

    inline
    bool operator!(ttg_starpu_data_flags lhs) {
      using flags_type = std::underlying_type<ttg_starpu_data_flags>::type;
      return lhs == ttg_starpu_data_flags::NONE;
    }

    /* Task hook return type for StarPU */
    using starpu_static_op_t = int (*)(void *);

    /* Base task structure for StarPU-based TTG tasks
     * 
     * TODO: StarPU task integration. Currently a thin wrapper that maintains
     * compatibility with existing TTG patterns. Full StarPU task submission
     * and scheduling will be implemented when StarPU integration is finalized.
     */
    struct starpu_ttg_task_base_t {
      starpu_task_t *starpu_task = nullptr;  // Pointer to StarPU task (when submitted)
      int32_t in_data_count = 0;             //< number of satisfied inputs
      int32_t data_count = 0;                //< number of data elements in the copies array
      ttg_data_copy_t **copies;              //< pointer to the fixed copies array of the derived task
      
      /* Hash table item for task tracking */
      struct {
        starpu_hash_table_item_t item;
        uint64_t key;
      } tt_ht_item;

      struct stream_info_t {
        std::size_t goal;
        std::size_t size;
        // TODO: StarPU equivalent for reduction tracking
        // For now, use atomic counter instead of LIFO queue
        std::atomic<std::size_t> reduce_count;
      };

    protected:
      template<std::size_t i = 0, typename TT>
      void init_stream_info_impl(TT *tt, std::array<stream_info_t, TT::numins>& streams) {
        if constexpr (TT::numins > i) {
          if (std::get<i>(tt->input_reducers)) {
            streams[i].goal = tt->static_stream_goal[i];
            streams[i].size = 0;
            streams[i].reduce_count.store(0, std::memory_order_relaxed);
          }
          /* recursion */
          if constexpr((i + 1) < TT::numins) {
            init_stream_info_impl<i+1>(tt, streams);
          }
        }
      }

      template<typename TT>
      void init_stream_info(TT *tt, std::array<stream_info_t, TT::numins>& streams) {
        init_stream_info_impl<0>(tt, streams);
      }

    public:
      typedef void (release_task_fn)(starpu_ttg_task_base_t*);
      
      /* Task release callback */
      release_task_fn* release_task_cb = nullptr;
      device_ptr_t* dev_ptr = nullptr;
      bool remove_from_hash = true;
      bool dummy = false;
      bool defer_writer = true;  // TODO: Use TTG configuration when available
      ttg_starpu_data_flags data_flags;

      void release_task() {
        if (release_task_cb) {
          release_task_cb(this);
        }
      }

    protected:
      /* Protected constructors: this class should not be instantiated directly
       * but always be used through starpu_ttg_task_t. */

      starpu_ttg_task_base_t(int data_count, ttg_data_copy_t **copies,
                            bool defer_writer_flag = true)
        : data_count(data_count)
        , copies(copies)
        , defer_writer(defer_writer_flag)
      {
        // TODO: Initialize StarPU task when needed
      }

      starpu_ttg_task_base_t(int32_t priority, int data_count, 
                            ttg_data_copy_t **copies,
                            release_task_fn *release_fn,
                            bool defer_writer_flag = true)
        : data_count(data_count)
        , copies(copies)
        , release_task_cb(release_fn)
        , defer_writer(defer_writer_flag)
      {
        // TODO: Initialize StarPU task when scheduling
      }

    public:
      void set_dummy(bool d) { dummy = d; }
      bool is_dummy() { return dummy; }
    };

    /* Non-streaming task specialization */
    template <typename TT, bool KeyIsVoid = ttg::meta::is_void_v<typename TT::key_type>>
    struct starpu_ttg_task_t : public starpu_ttg_task_base_t {
      using key_type = typename TT::key_type;
      TT* tt = nullptr;
      key_type key;
      static constexpr size_t num_streams = TT::numins;
      std::array<stream_info_t, num_streams> streams;
      ttg_data_copy_t *copies[num_streams] = { nullptr };

      starpu_ttg_task_t(int data_count, TT *tt_ptr)
        : starpu_ttg_task_base_t(data_count, copies)
        , tt(tt_ptr)
      {
        tt_ht_item.key = pkey();
        this->dev_ptr = this->dev_state.dev_ptr();
      }

      starpu_ttg_task_t(key_type key, int data_count, int32_t priority, TT *tt_ptr)
        : starpu_ttg_task_base_t(priority, data_count, copies,
                                &release_task, tt_ptr->m_defer_writer)
        , tt(tt_ptr), key(key)
      {
        tt_ht_item.key = pkey();
        this->dev_ptr = this->dev_state.dev_ptr();
      }

      static void release_task(starpu_ttg_task_base_t* task_base) {
        starpu_ttg_task_t *task = static_cast<starpu_ttg_task_t*>(task_base);
        TT *tt = task->tt;
        tt->release_task(task);
      }

      template<ttg::ExecutionSpace Space>
      int invoke_op() {
        // TODO: Implement StarPU task invocation
        // if constexpr (Space == ttg::ExecutionSpace::Host) {
        //   return TT::static_op(this);
        // }
        return 0;  // Placeholder
      }

      uint64_t pkey() { return 0; }
      device_state_t<TT::derived_has_device_op()> dev_state;
    };

    /* Streaming task specialization */
    template <typename TT>
    struct starpu_ttg_task_t<TT, true> : public starpu_ttg_task_base_t {
      static constexpr size_t num_streams = TT::numins;
      TT* tt = nullptr;

      std::array<stream_info_t, num_streams> streams;
      ttg_data_copy_t *copies[num_streams + 1] = { nullptr };

      starpu_ttg_task_t( TT *tt_ptr)
        : starpu_ttg_task_base_t(num_streams, copies)
        , tt(tt_ptr)
      {
        tt_ht_item.key = pkey();
        this->dev_ptr = this->dev_state.dev_ptr();
      }

      starpu_ttg_task_t(int32_t priority, TT *tt_ptr)
        : starpu_ttg_task_base_t(priority, num_streams, copies,
                                &release_task, tt_ptr->m_defer_writer)
        , tt(tt_ptr)
      {
        tt_ht_item.key = pkey();
        this->dev_ptr = this->dev_state.dev_ptr();
        init_stream_info(tt, streams);
      }

      static void release_task(starpu_ttg_task_base_t* task_base) {
        starpu_ttg_task_t *task = static_cast<starpu_ttg_task_t*>(task_base);
        TT *tt = task->tt;
        tt->release_task(task);
      }

      template<ttg::ExecutionSpace Space>
      int invoke_op() {
        // TODO: Implement StarPU task invocation
        return 0;  // Placeholder
      }

      uint64_t pkey() { return 0; }
      device_state_t<TT::derived_has_device_op()> dev_state;
    };

    /* Reducer task for handling stream reductions */
    struct reducer_task_t : public starpu_ttg_task_base_t {
      starpu_ttg_task_base_t *parent_task;
      bool is_first;

      reducer_task_t(starpu_ttg_task_base_t* task, int32_t priority, bool is_first_flag)
        : starpu_ttg_task_base_t(priority, 0, nullptr,
                                &release_task, true)
        , parent_task(task)
        , is_first(is_first_flag)
      {
      }

      static void release_task(starpu_ttg_task_base_t* task_base) {
        // TODO: Implement StarPU reducer task execution
        // For now, this is a placeholder
      }
    };

  } // namespace detail

} // namespace ttg_starpu

#endif // TTG_STARPU_TASK_H
