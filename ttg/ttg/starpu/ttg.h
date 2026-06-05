// SPDX-License-Identifier: BSD-3-Clause
// clang-format off
#ifndef STARPU_TTG_H_INCLUDED
#define STARPU_TTG_H_INCLUDED

/* set up env if this header was included directly */
#if !defined(TTG_IMPL_NAME)
#define TTG_USE_STARPU 1
#endif  // !defined(TTG_IMPL_NAME)

/* Whether to defer a potential writer if there are readers.
 * This may avoid extra copies in exchange for concurrency.
 * This may cause deadlocks, so use with caution. */
#define TTG_STARPU_DEFER_WRITER false

#include "ttg/config.h"

#include "ttg/impl_selector.h"

/* include ttg header to make symbols available in case this header is included directly */
#include "../../ttg.h"

#include "ttg/base/keymap.h"
#include "ttg/base/tt.h"
#include "ttg/base/world.h"
#include "ttg/constraint.h"
#include "ttg/edge.h"
#include "ttg/execution.h"
#include "ttg/func.h"
#include "ttg/runtimes.h"
#include "ttg/terminal.h"
#include "ttg/tt.h"
#include "ttg/util/env.h"
#include "ttg/util/hash.h"
#include "ttg/util/meta.h"
#include "ttg/util/meta/callable.h"
#include "ttg/util/print.h"
#include "ttg/util/scope_exit.h"
#include "ttg/util/trace.h"
#include "ttg/util/typelist.h"

#include "ttg/serialization/data_descriptor.h"

#include "ttg/starpu/fwd.h"
#include "ttg/starpu/task.h"
#include "ttg/starpu/buffer.h"
#include "ttg/starpu/devicescratch.h"
#include "ttg/starpu/thread_local.h"
#include "ttg/starpu/devicefunc.h"
#include "ttg/starpu/ttvalue.h"
#include "ttg/starpu/starpu_hash_table.h"
#include "ttg/device/task.h"
#include "ttg/starpu/starpu_data.h"
#include "ttg/starpu/ptr.h"


#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <experimental/type_traits>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <cstdlib>
#include <cstring>

#include "ttg/device/device.h"

#include "starpu.h"

namespace ttg_starpu {
  typedef void (*static_set_arg_fct_type)(void *, size_t, ttg::TTBase *);
  typedef std::pair<static_set_arg_fct_type, ttg::TTBase *> static_set_arg_fct_call_t;
  inline std::map<uint64_t, static_set_arg_fct_call_t> static_id_to_op_map;
  inline std::mutex static_map_mutex;
  typedef std::tuple<int, std::unique_ptr<std::byte[]>, size_t> static_set_arg_fct_arg_t;
  inline std::multimap<uint64_t, static_set_arg_fct_arg_t> delayed_unpack_actions;

  struct msg_header_t {
    typedef enum fn_id : std::int8_t {
      MSG_INVALID = -1,
      MSG_SET_ARG = 0,
      MSG_SET_ARGSTREAM_SIZE = 1,
      MSG_FINALIZE_ARGSTREAM_SIZE = 2,
      MSG_GET_FROM_PULL = 3 } fn_id_t;
    uint32_t taskpool_id = std::numeric_limits<uint32_t>::max();
    uint64_t op_id = std::numeric_limits<uint64_t>::max();
    std::size_t key_offset = 0;
    fn_id_t fn_id = MSG_INVALID;
    std::int8_t num_iovecs = 0;
    bool inline_data = false;
    int32_t param_id = -1;
    int num_keys = 0;
    int sender = -1;

    msg_header_t() = default;

    msg_header_t(fn_id_t fid, uint32_t tid, uint64_t oid, int32_t pid, int sender, int nk)
    : fn_id(fid)
    , taskpool_id(tid)
    , op_id(oid)
    , param_id(pid)
    , num_keys(nk)
    , sender(sender)
    { }
  };

  // static void unregister_parsec_tags(void *_);

  namespace detail {

    constexpr const int STARPU_TTG_MAX_AM_SIZE = 1 * 1024*1024;

    struct msg_t {
      msg_header_t tt_id;
      static constexpr std::size_t max_payload_size = STARPU_TTG_MAX_AM_SIZE - sizeof(msg_header_t);
      unsigned char bytes[max_payload_size];

      msg_t() = default;
      msg_t(uint64_t tt_id,
            uint32_t taskpool_id,
            msg_header_t::fn_id_t fn_id,
            int32_t param_id,
            int sender,
            int num_keys = 1)
      : tt_id(fn_id, taskpool_id, tt_id, param_id, sender, num_keys)
      {}
    };

    inline std::size_t max_inline_size = msg_t::max_payload_size;
    //parsec_comm_engine to void type
    static int static_unpack_msg(void *ce, uint64_t tag, void *data, long unsigned int size,
                                 int src_rank, void *obj) {
      static_set_arg_fct_type static_set_arg_fct;
      msg_header_t *msg = static_cast<msg_header_t *>(data);
      uint64_t op_id = msg->op_id;
      static_map_mutex.lock();
      // if (PARSEC_TERM_TP_NOT_READY != tp->tdm.module->taskpool_state(tp)) {
      //   try {
      //     auto op_pair = static_id_to_op_map.at(op_id);
      //     static_map_mutex.unlock();
      //     tp->tdm.module->incoming_message_start(tp, src_rank, NULL, NULL, 0, NULL);
      //     static_set_arg_fct = op_pair.first;
      //     static_set_arg_fct(data, size, op_pair.second);
      //     tp->tdm.module->incoming_message_end(tp, NULL);
      //     return 0;
      //   } catch (const std::out_of_range &e)
      //   { /* fall-through */ }
      // }
      auto data_cpy = std::make_unique_for_overwrite<std::byte[]>(size);
      memcpy(data_cpy.get(), data, size);
      ttg::trace("ttg_starpu(", ttg_default_execution_context().rank(), ") Delaying delivery of message (", src_rank,
                 ", ", op_id, ", ", data_cpy, ", ", size, ")");
      delayed_unpack_actions.insert(std::make_pair(op_id, std::make_tuple(src_rank, std::move(data_cpy), size)));
      static_map_mutex.unlock();
      return 1;
    }

    static int get_remote_complete_cb(void *ce, int tag, void *msg, size_t msg_size,
                                      int src, void *cb_data);

    inline bool &initialized_mpi() {
      static bool im = false;
      return im;
    }

    inline bool all_devices_peer_access;

    inline void send_active_message(int owner, const void *data, size_t size) {
      // TODO: replace by StarPU remote communication 
      (void)owner;
      (void)data;
      (void)size;
      ttg::trace("ttg_starpu: send_active_message stub (owner=", owner, ")");
    }


  }  // namespace detail

  class WorldImpl : public ttg::base::WorldImplBase {
    ttg::Edge<> m_ctl_edge;
    bool _dag_profiling;
    bool _task_profiling;
    std::array<bool, static_cast<std::size_t>(ttg::ExecutionSpace::Invalid)>
               mpi_space_support = {true, false, false};


    int query_comm_size() {
      int comm_size;
      //MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
      comm_size = 1;
      return comm_size;
    }

    int query_comm_rank() {
      int comm_rank;
      //MPI_Comm_rank(MPI_COMM_WORLD, &comm_rank);
      comm_rank = 0; 
      return comm_rank;
    }

    static void ttg_starpu_ce_up(void *comm_engine, void *user_data)
    {
      // register message handlers 

    }

    static void ttg_starpu_ce_down(void *comm_engine, void *user_data)
    {
      // unregister message handlers 

    }

   public:
#if defined(PARSEC_PROF_TRACE) && defined(PARSEC_TTG_PROFILE_BACKEND)
    int starpu_ttg_profile_backend_set_arg_start, starpu_ttg_profile_backend_set_arg_end;
    int starpu_ttg_profile_backend_bcast_arg_start, starpu_ttg_profile_backend_bcast_arg_end;
    int starpu_ttg_profile_backend_allocate_datacopy, starpu_ttg_profile_backend_free_datacopy;
#endif
    WorldImpl(int *argc, char **argv[], int ncores, void *c = nullptr)
        : WorldImplBase(query_comm_size(), query_comm_rank())
        , ctx(c)
        , own_ctx(c == nullptr)
       , _dag_profiling(false)
       , _task_profiling(false)
    {
      ttg::detail::register_world(*this);
      if (own_ctx) {
         int ret = starpu_init(nullptr);
          if (ret != 0) {
            throw std::runtime_error("Failed to initialize StarPU");
          } 
        }

      // if( NULL != parsec_ce.tag_register) {
      //   parsec_ce.tag_register(WorldImpl::parsec_ttg_tag(), &detail::static_unpack_msg, this, detail::STARPU_TTG_MAX_AM_SIZE);
      //   parsec_ce.tag_register(WorldImpl::parsec_ttg_rma_tag(), &detail::get_remote_complete_cb, this, 128);
      // }
    }


    auto *context() { return ctx; }
    void *execution_stream() {return nullptr;}
    void *taskpool() {return nullptr;}

    void create_tpool() {
      //No taskpool in starpu
    }

    /* Deleted copy ctor */
    WorldImpl(const WorldImpl &other) = delete;

    /* Deleted move ctor */
    WorldImpl(WorldImpl &&other) = delete;

    /* Deleted copy assignment */
    WorldImpl &operator=(const WorldImpl &other) = delete;

    /* Deleted move assignment */
    WorldImpl &operator=(WorldImpl &&other) = delete;

    ~WorldImpl() { destroy(); }

    //TODO: need to define or not?
    static constexpr int starpu_ttg_tag() { return 0; }
    static constexpr int starpu_ttg_rma_tag() { return 0; }

    //TODO: delete when mpi support is added
    #ifndef MPI_Comm
      #define MPI_Comm int
    #endif
    MPI_Comm comm() const { return 0; }

    virtual void execute() override {
      
    }

    void destroy_tpool() {
      //No taskpool in starpu
    }
    virtual void destroy() override {
      if (is_valid()) {
        release_ops();
        ttg::detail::deregister_world(*this);
        // if (own_ctx) {
        //   unregister_parsec_tags(nullptr);
        // } else {
        //   parsec_context_at_fini(unregister_parsec_tags, nullptr);
        // }
        // if (own_ctx) parsec_fini(&ctx);
        mark_invalid();
      }
    }

    ttg::Edge<> &ctl_edge() { return m_ctl_edge; }

    const ttg::Edge<> &ctl_edge() const { return m_ctl_edge; }

    void increment_created() {  }

    void increment_inflight_msg() {  }
    void decrement_inflight_msg() {  }

    bool dag_profiling() override { return _dag_profiling; }

    virtual void dag_on(const std::string &filename) override {
    }

    virtual void dag_off() override {
    }

    virtual void profile_off() override {}

    virtual void profile_on() override {}

    virtual bool profiling() override { return false;}

    bool mpi_support(ttg::ExecutionSpace space) {
      return mpi_space_support[static_cast<std::size_t>(space)];
    }

    virtual void final_task() override {
    }

    template <typename keyT, typename output_terminalsT, typename derivedT,
              typename input_valueTs = ttg::typelist<>, ttg::ExecutionSpace Space>
    void register_tt_profiling(const TT<keyT, output_terminalsT, derivedT, input_valueTs, Space> *t) {
    }

   protected:

    virtual void fence_impl(void) override {
      int rank = this->rank();

      starpu_task_wait_for_all();
      // MPI_Barrier(comm());
    }

   private: 
      void *ctx = nullptr;
      bool own_ctx = false;  //< whether I own the context
      void *tpool = nullptr;
      bool parsec_taskpool_started = false;
  };

  // static void unregister_parsec_tags(void *_pidx)
  // {
  //   if(NULL != parsec_ce.tag_unregister) {
  //     parsec_ce.tag_unregister(WorldImpl::parsec_ttg_tag());
  //     parsec_ce.tag_unregister(WorldImpl::parsec_ttg_rma_tag());
  //   }
  // }

  namespace detail {
    // const starpu_symbol_t starpu_taskclass_param0 = {
    //   .flags = STARPU_SYMBOL_IS_STANDALONE|STARPU_SYMBOL_IS_GLOBAL,
    //   .name = "HASH0",
    //   .context_index = 0,
    //   .min = nullptr,
    //   .max = nullptr,
    //   .expr_inc = nullptr,
    //   .cst_inc = 0 };
    // const starpu_symbol_t starpu_taskclass_param1 = {
    //   .flags = STARPU_SYMBOL_IS_STANDALONE|STARPU_SYMBOL_IS_GLOBAL,
    //   .name = "HASH1",
    //   .context_index = 1,
    //   .min = nullptr,
    //   .max = nullptr,
    //   .expr_inc = nullptr,
    //   .cst_inc = 0 };
    // const starpu_symbol_t starpu_taskclass_param2 = {
    //   .flags = STARPU_SYMBOL_IS_STANDALONE|STARPU_SYMBOL_IS_GLOBAL,
    //   .name = "KEY0",
    //   .context_index = 2,
    //   .min = nullptr,
    //   .max = nullptr,
    //   .expr_inc = nullptr,
    //   .cst_inc = 0 };
    // const starpu_symbol_t starpu_taskclass_param3 = {
    //   .flags = STARPU_SYMBOL_IS_STANDALONE|STARPU_SYMBOL_IS_GLOBAL,
    //   .name = "KEY1",
    //   .context_index = 3,
    //   .min = nullptr,
    //   .max = nullptr,
    //   .expr_inc = nullptr,
    //   .cst_inc = 0 };

    inline ttg_data_copy_t *find_copy_in_task(starpu_ttg_task_base_t *task, const void *ptr) {
      ttg_data_copy_t *res = nullptr;
      if (task == nullptr || ptr == nullptr) {
        return res;
      }
      for (int i = 0; i < task->data_count; ++i) {
        auto copy = static_cast<ttg_data_copy_t *>(task->copies[i]);
        if (NULL != copy && copy->get_ptr() == ptr) {
          res = copy;
          break;
        }
      }
      return res;
    }

    inline int find_index_of_copy_in_task(starpu_ttg_task_base_t *task, const void *ptr) {
      int i = -1;
      if (task == nullptr || ptr == nullptr) {
        return i;
      }
      for (i = 0; i < task->data_count; ++i) {
        auto copy = static_cast<ttg_data_copy_t *>(task->copies[i]);
        if (NULL != copy && copy->get_ptr() == ptr) {
          return i;
        }
      }
      return -1;
    }

    inline bool add_copy_to_task(ttg_data_copy_t *copy, starpu_ttg_task_base_t *task) {
      if (task == nullptr || copy == nullptr) {
        return false;
      }

      if (MAX_PARAM_COUNT < task->data_count) {
        throw std::logic_error("Too many data copies, check MAX_PARAM_COUNT!");
      }

      task->copies[task->data_count] = copy;
      task->data_count++;
      return true;
    }

    inline void remove_data_copy(ttg_data_copy_t *copy, starpu_ttg_task_base_t *task) {
      int i;
      /* find and remove entry; copies are usually appended and removed, so start from back */
      for (i = task->data_count-1; i >= 0; --i) {
        if (copy == task->copies[i]) {
          break;
        }
      }
      if (i < 0) return;
      /* move all following elements one up */
      for (; i < task->data_count - 1; ++i) {
        task->copies[i] = task->copies[i + 1];
      }
      /* null last element */
      task->copies[i] = nullptr;
      task->data_count--;
    }



    template <typename Value>
    inline ttg_data_copy_t *create_new_datacopy(Value &&value) {
      using value_type = std::decay_t<Value>;
      ttg_data_copy_t *copy = nullptr;
      if constexpr (std::is_base_of_v<ttg::TTValue<value_type>, value_type> &&
                    std::is_constructible_v<value_type, decltype(value)>) {
        copy = new value_type(std::forward<Value>(value));
      } else if constexpr (std::is_constructible_v<ttg_data_value_copy_t<value_type>, decltype(value)>) {
        copy = new ttg_data_value_copy_t<value_type>(std::forward<Value>(value));
      } else {
        /* we have no way to create a new copy from this value */
        throw std::logic_error("Trying to copy-construct data that is not copy-constructible!");
      }

      return copy;
    }

#if 0
    template <std::size_t... IS, typename Key = keyT>
    void invoke_pull_terminals(std::index_sequence<IS...>, const Key &key, detail::starpu_ttg_task_base_t *task) {
      int junk[] = {0, (invoke_pull_terminal<IS>(
                            std::get<IS>(input_terminals), key, task),
                        0)...};
      junk[0]++;
    }
#endif // 0

    template<typename T>
    inline void transfer_ownership_impl(T&& arg, int device) {
      if constexpr(!std::is_const_v<std::remove_reference_t<T>>) {
        detail::foreach_starpu_data(arg, [&](auto *data){
          //TODO: Implement equivalent parsec_data_transfer_ownership_to_copy for starpu
        });
      }
    }

    template<typename TT, std::size_t... Is>
    inline void transfer_ownership(starpu_ttg_task_t<TT> *me, int device, std::index_sequence<Is...>) {
      /* transfer ownership of each data */
      int junk[] = {0,
                    (transfer_ownership_impl(
                        *reinterpret_cast<std::remove_reference_t<std::tuple_element_t<Is, typename TT::input_refs_tuple_type>> *>(
                          me->copies[Is]->get_ptr()), device), 0)...};
      junk[0]++;
    }

    template<typename TT>
    inline void hook(void *descr[],void *cl_arg) {
      auto *me = static_cast<starpu_ttg_task_t<TT>*>(cl_arg);
      if constexpr(std::tuple_size_v<typename TT::input_values_tuple_type> > 0) {
        transfer_ownership<TT>(me, 0, std::make_index_sequence<std::tuple_size_v<typename TT::input_values_tuple_type>>{});
      }
      me->template invoke_op<ttg::ExecutionSpace::Host>();
    }

    template <typename KeyT, typename ActivationCallbackT>
    class rma_delayed_activate {
      std::vector<KeyT> _keylist;
      std::atomic<int> _outstanding_transfers;
      ActivationCallbackT _cb;
      detail::ttg_data_copy_t *_copy;

     public:
      rma_delayed_activate(std::vector<KeyT> &&key, detail::ttg_data_copy_t *copy, int num_transfers, ActivationCallbackT cb)
          : _keylist(std::move(key)), _outstanding_transfers(num_transfers), _cb(cb), _copy(copy) {}

      bool complete_transfer(void) {
        assert(_outstanding_transfers > 0);
        int left = --_outstanding_transfers;
        if (0 == left) {
          _cb(std::move(_keylist), _copy);
          return true;
        }
        return false;
      }
    };

    template <typename ActivationT>
    static int get_complete_cb(void *comm_engine, void* lreg, ptrdiff_t ldispl,
                               void * rreg, ptrdiff_t rdispl, size_t size, int remote,
                               void *cb_data) {
      // parsec_ce.mem_unregister(&lreg);
      ActivationT *activation = static_cast<ActivationT *>(cb_data);
      if (activation->complete_transfer()) {
        delete activation;
      }
      return 0; //Success
    }

    inline void release_data_copy(ttg_data_copy_t *copy) {
      if (copy->is_mutable() && nullptr == copy->get_next_task()) {
        /* current task mutated the data but there are no consumers so prepare
        * the copy to be freed below */
        copy->reset_readers();
      }

      int32_t readers = copy->num_readers();
      if (readers > 1) {
        /* potentially more than one reader, decrement atomically */
        readers = copy->decrement_readers();
      } else if (readers == 1) {
        /* make sure readers drop to zero */
        readers = copy->decrement_readers<false>();
      }
      /* if there was only one reader (the current task) or
       * a mutable copy and a successor, we release the copy */
      if (1 == readers || readers == copy->mutable_tag) {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (nullptr != copy->get_next_task()) {
          /* Release the deferred task.
           * The copy was mutable and will be mutated by the released task,
           * so simply transfer ownership.
           */
          starpu_task_t *next_task = copy->get_next_task();
          copy->set_next_task(nullptr);
          starpu_ttg_task_base_t *deferred_op = (starpu_ttg_task_base_t *)next_task;
          copy->mark_mutable();
          deferred_op->release_task();
        } else if ((1 == copy->num_ref()) || (1 == copy->drop_ref())) {
          delete copy;
        }
      }
    }

    template <typename Value>
    inline ttg_data_copy_t *register_data_copy(ttg_data_copy_t *copy_in, starpu_ttg_task_base_t *task, bool readonly) {
      ttg_data_copy_t *copy_res = copy_in;
      bool replace = false;
      int32_t readers = copy_in->num_readers();
      assert(readers != 0);

      if (readonly && !copy_in->is_mutable()) {
        /* simply increment the number of readers */
        readers = copy_in->increment_readers();
      }

      if (readers == copy_in->mutable_tag) {
        if (copy_res->get_next_task() != nullptr) {
          if (readonly) {
            starpu_ttg_task_base_t *next_task = reinterpret_cast<starpu_ttg_task_base_t *>(copy_res->get_next_task());
            if (next_task->defer_writer) {
              /* there is a writer but it signalled that it wants to wait for readers to complete */
              return copy_res;
            }
          }
        }
        /* someone is going to write into this copy -> we need to make a copy */
        copy_res = NULL;
        if (readonly) {
          /* we replace the copy in a deferred task if the copy will be mutated by
           * the deferred task and we are readonly.
           * That way, we can share the copy with other readonly tasks and release
           * the deferred task. */
          replace = true;
        }
      } else if (!readonly) {
        /* this task will mutate the data
         * check whether there are other readers already and potentially
         * defer the release of this task to give following readers a
         * chance to make a copy of the data before this task mutates it
         *
         * Try to replace the readers with a negative value that indicates
         * the value is mutable. If that fails we know that there are other
         * readers or writers already.
         *
         * NOTE: this check is not atomic: either there is a single reader
         *       (current task) or there are others, in which we case won't
         *       touch it.
         */
        /* try hard to defer writers if we cannot make copies
         * if deferral fails we have to bail out */
        bool defer_writer = (!std::is_copy_constructible_v<std::decay_t<Value>>) ||
                            ((nullptr != task) && task->defer_writer);

        if (1 == copy_in->num_readers() && !defer_writer) {
          /**
           * no other readers, mark copy as mutable and defer the release
           * of the task
           */
          assert(nullptr == copy_in->get_next_task());
          copy_in->set_next_task(reinterpret_cast<starpu_task_t *>(task));
          std::atomic_thread_fence(std::memory_order_release);
          copy_in->mark_mutable();
        } else {
          if (defer_writer && nullptr == copy_in->get_next_task()) {
            /* we're the first writer and want to wait for all readers to complete */
            copy_res->set_next_task(reinterpret_cast<starpu_task_t *>(task));
            task->defer_writer = true;
          } else {
            /* there are writers and/or waiting already of this copy already, make a copy that we can mutate */
            copy_res = NULL;
          }
        }
      }

      if (NULL == copy_res) {
        // can only make a copy if Value is copy-constructible ... so this codepath should never be hit
        if constexpr (std::is_copy_constructible_v<std::decay_t<Value>>) {
          ttg_data_copy_t *new_copy = detail::create_new_datacopy(*static_cast<Value *>(copy_in->get_ptr()));
          if (replace && nullptr != copy_in->get_next_task()) {
            /* replace the task that was deferred */
            starpu_ttg_task_base_t *deferred_op = (starpu_ttg_task_base_t *)copy_in->get_next_task();
            new_copy->mark_mutable();
            /* replace the copy in the deferred task */
            for (int i = 0; i < deferred_op->data_count; ++i) {
              if (deferred_op->copies[i] == copy_in) {
                deferred_op->copies[i] = new_copy;
                break;
              }
            }
            copy_in->set_next_task(nullptr);
            deferred_op->release_task();
            copy_in->reset_readers();            // set the copy back to being read-only
            copy_in->increment_readers<false>(); // register as reader
            copy_res = copy_in;                  // return the copy we were passed
          } else {
            if (!readonly) {
              new_copy->mark_mutable();
            }
            copy_res = new_copy;  // return the new copy
          }
        }
        else {
          throw std::logic_error(std::string("TTG::StarPU: need to copy a datum of type") + typeid(std::decay_t<Value>).name() + " but the type is not copyable");
        }
      }
      return copy_res;
    }

  }  // namespace detail

  inline void ttg_initialize(int argc, char **argv, int num_threads, void *ctx) {
    if (detail::initialized_mpi()) throw std::runtime_error("ttg_starpu::ttg_initialize: can only be called once");

    // make sure it's not already initialized
    int mpi_initialized;
    //MPI_Initialized(&mpi_initialized);
    mpi_initialized = 0;
    if (!mpi_initialized) {  // MPI not initialized? do it, remember that we did it
      int provided;
      //MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
      // provided = MPI_THREAD_MULTIPLE;
      // if (!provided)
      //   throw std::runtime_error("ttg_starpu::ttg_initialize: MPI_Init_thread did not provide MPI_THREAD_MULTIPLE");
      detail::initialized_mpi() = true;
    } else {  // no way to test that MPI was initialized with MPI_THREAD_MULTIPLE, cross fingers and proceed
    }

    if (num_threads < 1) num_threads = ttg::detail::num_threads();
    auto world_ptr = new ttg_starpu::WorldImpl{&argc, &argv, num_threads, ctx};
    std::shared_ptr<ttg::base::WorldImplBase> world_sptr{static_cast<ttg::base::WorldImplBase *>(world_ptr)};
    ttg::World world{std::move(world_sptr)};
    ttg::detail::set_default_world(std::move(world));

    // query the first device ID
    detail::first_device_id = -1;

    /* parse the maximum inline size */
    const char* ttg_max_inline_cstr = std::getenv("TTG_MAX_INLINE");
    if (nullptr != ttg_max_inline_cstr) {
      std::size_t inline_size = std::atol(ttg_max_inline_cstr);
      if (inline_size < detail::max_inline_size) {
        detail::max_inline_size = inline_size;
      }
    }

    bool all_peer_access = true;
    detail::all_devices_peer_access = all_peer_access;
  }
  inline void ttg_finalize() {
    // We need to notify the current taskpool of termination if we are in user termination detection mode
    // or the parsec_context_wait() in destroy_worlds() will never complete
    if(0 == ttg::default_execution_context().rank())
      ttg::default_execution_context().impl().final_task();
    ttg::detail::set_default_world(ttg::World{});  // reset the default world
    detail::ptr_impl::drop_all_ptr();
    ttg::detail::destroy_worlds<ttg_starpu::WorldImpl>();
    if (detail::initialized_mpi()) {
      //MPI_Finalize();
    }
  }
  inline ttg::World ttg_default_execution_context() { return ttg::get_default_world(); }
  [[noreturn]]
  inline void ttg_abort() {  std::abort(); }//MPI_Abort
  inline void ttg_execute(ttg::World world) { world.impl().execute(); }
  inline void ttg_fence(ttg::World world) { world.impl().fence(); }

  template <typename T>
  inline void ttg_register_ptr(ttg::World world, const std::shared_ptr<T> &ptr) {
    world.impl().register_ptr(ptr);
  }

  template <typename T>
  inline void ttg_register_ptr(ttg::World world, std::unique_ptr<T> &&ptr) {
    world.impl().register_ptr(std::move(ptr));
  }

  inline void ttg_register_status(ttg::World world, const std::shared_ptr<std::promise<void>> &status_ptr) {
    world.impl().register_status(status_ptr);
  }

  template <typename Callback>
  inline void ttg_register_callback(ttg::World world, Callback &&callback) {
    world.impl().register_callback(std::forward<Callback>(callback));
  }

  inline ttg::Edge<> &ttg_ctl_edge(ttg::World world) { return world.impl().ctl_edge(); }

  inline void ttg_sum(ttg::World world, double &value) {
    double result = 0.0;
    //MPI_Allreduce(&value, &result, 1, MPI_DOUBLE, MPI_SUM, world.impl().comm());
    value = result;
  }

  inline void make_executable_hook(ttg::World& world) {
    //MPI_Barrier(world.impl().comm());
  }

  /// broadcast
  /// @tparam T a serializable type
  template <typename T>
  void ttg_broadcast(::ttg::World world, T &data, int source_rank) {
    int64_t BUFLEN;
    if (world.rank() == source_rank) {
      BUFLEN = ttg::default_data_descriptor<T>::payload_size(&data);
    }
    //MPI_Bcast(&BUFLEN, 1, MPI_INT64_T, source_rank, world.impl().comm());

    unsigned char *buf = new unsigned char[BUFLEN];
    if (world.rank() == source_rank) {
      ttg::default_data_descriptor<T>::pack_payload(&data, BUFLEN, 0, buf);
    }
    //MPI_Bcast(buf, BUFLEN, MPI_UNSIGNED_CHAR, source_rank, world.impl().comm());
    if (world.rank() != source_rank) {
      ttg::default_data_descriptor<T>::unpack_payload(&data, BUFLEN, 0, buf);
    }
    delete[] buf;
  }

  namespace detail {
    template <typename TT, typename keyT>
    struct StarPUTTBase {
      using hashtable_keyT = std::conditional_t<ttg::meta::is_void_v<keyT>,int,keyT>;
    protected:
      starpu_codelet_t starpu_tt_cl = {
        .where = STARPU_CPU,
        .cpu_funcs = {detail::hook<TT>},
        .nbuffers = 0
      };
      std::unique_ptr<starpu_hash_table<TT, hashtable_keyT>> tasks_table;
      std::unique_ptr<starpu_hash_table<TT, hashtable_keyT>> task_constraint_table;

      StarPUTTBase() 
        : tasks_table(std::make_unique<starpu_hash_table<TT, hashtable_keyT,ttg::hash<hashtable_keyT>>>()),
          task_constraint_table(std::make_unique<starpu_hash_table<TT, hashtable_keyT,ttg::hash<hashtable_keyT>>>())
      {
      }


      ~StarPUTTBase() {
        
      }
    };

  }  // namespace detail

  template <typename keyT, typename output_terminalsT, typename derivedT, typename input_valueTs, ttg::ExecutionSpace Space>
  class TT : public ttg::TTBase, detail::StarPUTTBase<TT<keyT, output_terminalsT, derivedT, input_valueTs, Space>,keyT> {
   private:
    /// preconditions
    static_assert(ttg::meta::is_typelist_v<input_valueTs>,
                  "The fourth template for ttg::TT must be a ttg::typelist containing the input types");
    // create a virtual control input if the input list is empty, to be used in invoke()
    using actual_input_tuple_type = std::conditional_t<!ttg::meta::typelist_is_empty_v<input_valueTs>,
                                                       ttg::meta::typelist_to_tuple_t<input_valueTs>, std::tuple<void>>;
    using input_tuple_type = ttg::meta::typelist_to_tuple_t<input_valueTs>;
    static_assert(ttg::meta::is_tuple_v<output_terminalsT>,
                  "Second template argument for ttg::TT must be std::tuple containing the output terminal types");
    static_assert((ttg::meta::none_has_reference_v<input_valueTs>), "Input typelist cannot contain reference types");
    static_assert(ttg::meta::is_none_Void_v<input_valueTs>, "ttg::Void is for internal use only, do not use it");

    //parsec_mempool_t mempools;

    bool alive = true;

    static constexpr int numinedges = std::tuple_size_v<input_tuple_type>;     // number of input edges
    static constexpr int numins = std::tuple_size_v<actual_input_tuple_type>;  // number of input arguments
    static constexpr int numouts = std::tuple_size_v<output_terminalsT>;       // number of outputs
    static constexpr int numflows = std::max(numins, numouts);                 // max number of flows

   public:
    
    //TODO: add Cuda or other device
    static constexpr bool derived_has_device_op() {
      return false;
    }

    using ttT = TT;
    using key_type = std::conditional_t<ttg::meta::is_void_v<keyT>,int,keyT>;
    using input_terminals_type = ttg::detail::input_terminals_tuple_t<keyT, input_tuple_type>;
    using input_args_type = actual_input_tuple_type;
    using input_edges_type = ttg::detail::edges_tuple_t<keyT, ttg::meta::decayed_typelist_t<input_tuple_type>>;
    // if have data inputs and (always last) control input, convert last input to Void to make logic easier
    using input_values_full_tuple_type =
        ttg::meta::void_to_Void_tuple_t<ttg::meta::decayed_typelist_t<actual_input_tuple_type>>;
    using input_refs_full_tuple_type =
        ttg::meta::add_glvalue_reference_tuple_t<ttg::meta::void_to_Void_tuple_t<actual_input_tuple_type>>;
    using input_values_tuple_type = ttg::meta::drop_void_t<ttg::meta::decayed_typelist_t<input_tuple_type>>;
    using input_refs_tuple_type = ttg::meta::drop_void_t<ttg::meta::add_glvalue_reference_tuple_t<input_tuple_type>>;

    static constexpr int numinvals =
        std::tuple_size_v<input_refs_tuple_type>;  // number of input arguments with values (i.e. omitting the control
                                                   // input, if any)

    using output_terminals_type = output_terminalsT;
    using output_edges_type = typename ttg::terminals_to_edges<output_terminalsT>::type;

    template <std::size_t i, typename resultT, typename InTuple>
    static resultT get(InTuple &&intuple) {
      return static_cast<resultT>(std::get<i>(std::forward<InTuple>(intuple)));
    };
    template <std::size_t i, typename InTuple>
    static auto &get(InTuple &&intuple) {
      return std::get<i>(std::forward<InTuple>(intuple));
    };

   private:
    using task_t = detail::starpu_ttg_task_t<ttT>;

    friend detail::starpu_ttg_task_base_t;
    friend task_t;

    /* the offset of the key placed after the task structure in the memory from mempool */
    constexpr static const size_t task_key_offset = sizeof(task_t);

    input_terminals_type input_terminals;
    output_terminalsT output_terminals;

   protected:
    const auto &get_output_terminals() const { return output_terminals; }

   private:
    template <std::size_t... IS>
    static constexpr auto make_set_args_fcts(std::index_sequence<IS...>) {
      using resultT = decltype(set_arg_from_msg_fcts);
      return resultT{{&TT::set_arg_from_msg<IS>...}};
    }
    constexpr static std::array<void (TT::*)(void *, std::size_t), numins> set_arg_from_msg_fcts =
        make_set_args_fcts(std::make_index_sequence<numins>{});

    template <std::size_t... IS>
    static constexpr auto make_set_size_fcts(std::index_sequence<IS...>) {
      using resultT = decltype(set_argstream_size_from_msg_fcts);
      return resultT{{&TT::argstream_set_size_from_msg<IS>...}};
    }
    constexpr static std::array<void (TT::*)(void *, std::size_t), numins> set_argstream_size_from_msg_fcts =
        make_set_size_fcts(std::make_index_sequence<numins>{});

    template <std::size_t... IS>
    static constexpr auto make_finalize_argstream_fcts(std::index_sequence<IS...>) {
      using resultT = decltype(finalize_argstream_from_msg_fcts);
      return resultT{{&TT::finalize_argstream_from_msg<IS>...}};
    }
    constexpr static std::array<void (TT::*)(void *, std::size_t), numins> finalize_argstream_from_msg_fcts =
        make_finalize_argstream_fcts(std::make_index_sequence<numins>{});

    template <std::size_t... IS>
    static constexpr auto make_get_from_pull_fcts(std::index_sequence<IS...>) {
      using resultT = decltype(get_from_pull_msg_fcts);
      return resultT{{&TT::get_from_pull_msg<IS>...}};
    }
    constexpr static std::array<void (TT::*)(void *, std::size_t), numinedges> get_from_pull_msg_fcts =
        make_get_from_pull_fcts(std::make_index_sequence<numinedges>{});

    template<std::size_t... IS>
    constexpr static auto make_input_is_const(std::index_sequence<IS...>) {
      using resultT = decltype(input_is_const);
      return resultT{{std::is_const_v<std::tuple_element_t<IS, input_args_type>>...}};
    }
    constexpr static std::array<bool, numins> input_is_const = make_input_is_const(std::make_index_sequence<numins>{});

    ttg::World world;
    ttg::meta::detail::keymap_t<keyT> keymap;
    ttg::meta::detail::keymap_t<keyT> priomap;
    ttg::meta::detail::keymap_t<keyT, ttg::device::Device> devicemap;
    // For now use same type for unary/streaming input terminals, and stream reducers assigned at runtime
    ttg::meta::detail::input_reducers_t<actual_input_tuple_type>
        input_reducers;  //!< Reducers for the input terminals (empty = expect single value)
    //std::array<parsec_task_class_t*, numins> inpute_reducers_taskclass = { nullptr };
    std::array<std::size_t, numins> static_stream_goal = { std::numeric_limits<std::size_t>::max() };
    int num_pullins = 0;

    bool m_defer_writer = TTG_STARPU_DEFER_WRITER;

    std::vector<ttg::meta::detail::constraint_callback_t<keyT>> constraints_check;
    std::vector<ttg::meta::detail::constraint_callback_t<keyT>> constraints_complete;

   public:
    ttg::World get_world() const override final { return world; }

   private:
    /// dispatches a call to derivedT::op
    /// @return void if called a synchronous function, or ttg::coroutine_handle<> if called a coroutine (if non-null,
    ///    points to the suspended coroutine)
    template <typename... Args>
    auto op(Args &&...args) {
      derivedT *derived = static_cast<derivedT *>(this);
      using return_type = decltype(derived->op(std::forward<Args>(args)...));
      if constexpr (std::is_same_v<return_type,void>) {
        derived->op(std::forward<Args>(args)...);
        return;
      }
      else {
        return derived->op(std::forward<Args>(args)...);
      }
    }

    template <std::size_t i, typename terminalT, typename Key>
    void invoke_pull_terminal(terminalT &in, const Key &key, detail::starpu_ttg_task_base_t *task) {
      if (in.is_pull_terminal) {
        auto owner = in.container.owner(key);
        if (owner != world.rank()) {
          get_pull_terminal_data_from<i>(owner, key);
        } else {
          // push the data to the task
          set_arg<i>(key, (in.container).get(key));
        }
      }
    }

    template <std::size_t i, typename Key>
    void get_pull_terminal_data_from(const int owner,
                                     const Key &key) {
      using msg_t = detail::msg_t;
      auto &world_impl = world.impl();
      //TODO: Exhange tp->taskpool_id with ?
      // std::unique_ptr<msg_t> msg = std::make_unique<msg_t>(get_instance_id(), tp->taskpool_id,
      //                                                       msg_header_t::MSG_GET_FROM_PULL, i,
      //                                                       world.rank(), 1);
      /* pack the key */
      // size_t pos = 0;
      // pos = pack(key, msg->bytes, pos);
      // detail::send_active_message(owner, msg.get(), sizeof(msg_header_t) + pos);
    }

    template <std::size_t... IS, typename Key = keyT>
    void invoke_pull_terminals(std::index_sequence<IS...>, const Key &key, detail::starpu_ttg_task_base_t *task) {
      int junk[] = {0, (invoke_pull_terminal<IS>(
                            std::get<IS>(input_terminals), key, task),
                        0)...};
      junk[0]++;
    }

    template <std::size_t... IS>
    static input_refs_tuple_type make_tuple_of_ref_from_array(task_t *task, std::index_sequence<IS...>) {
      return input_refs_tuple_type{static_cast<std::tuple_element_t<IS, input_refs_tuple_type>>(
          *reinterpret_cast<std::remove_reference_t<std::tuple_element_t<IS, input_refs_tuple_type>> *>(
              task->copies[IS]->get_ptr()))...};
    }


    static starpu_hook_return_t static_op(starpu_task_t *starpu_task) {

      task_t *task = (task_t*)starpu_task;
      void* suspended_task_address =
      #ifdef TTG_HAVE_COROUTINE
        task->suspended_task_address;  // non-null = need to resume the task
#else  // TTG_HAVE_COROUTINE
        nullptr;
#endif // TTG_HAVE_COROUTINE
      //std::cout << "static_op: suspended_task_address " << suspended_task_address << std::endl;
      if (suspended_task_address == nullptr) {  // task is a coroutine that has not started or an ordinary function

        ttT *baseobj = task->tt;
        derivedT *obj = static_cast<derivedT *>(baseobj);
        assert(detail::starpu_ttg_caller == nullptr);
        detail::starpu_ttg_caller = static_cast<detail::starpu_ttg_task_base_t*>(task);
        if (obj->tracing()) {
          if constexpr (!ttg::meta::is_void_v<keyT>)
            ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : ", task->key, ": executing");
          else
            ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : executing");
        }

        if constexpr (!ttg::meta::is_void_v<keyT> && !ttg::meta::is_empty_tuple_v<input_values_tuple_type>) {
          auto input = make_tuple_of_ref_from_array(task, std::make_index_sequence<numinvals>{});
          TTG_PROCESS_TT_OP_RETURN(suspended_task_address, task->coroutine_id, baseobj->op(task->key, std::move(input), obj->output_terminals));
        } else if constexpr (!ttg::meta::is_void_v<keyT> && ttg::meta::is_empty_tuple_v<input_values_tuple_type>) {
          TTG_PROCESS_TT_OP_RETURN(suspended_task_address, task->coroutine_id, baseobj->op(task->key, obj->output_terminals));
        } else if constexpr (ttg::meta::is_void_v<keyT> && !ttg::meta::is_empty_tuple_v<input_values_tuple_type>) {
          auto input = make_tuple_of_ref_from_array(task, std::make_index_sequence<numinvals>{});
          TTG_PROCESS_TT_OP_RETURN(suspended_task_address, task->coroutine_id, baseobj->op(std::move(input), obj->output_terminals));
        } else if constexpr (ttg::meta::is_void_v<keyT> && ttg::meta::is_empty_tuple_v<input_values_tuple_type>) {
          TTG_PROCESS_TT_OP_RETURN(suspended_task_address, task->coroutine_id, baseobj->op(obj->output_terminals));
        } else {
          ttg::abort();
        }
        detail::starpu_ttg_caller = nullptr;
      }
      else {  // resume the suspended coroutine

#ifdef TTG_HAVE_COROUTINE
        assert(task->coroutine_id != ttg::TaskCoroutineID::Invalid);

      if (task->coroutine_id == ttg::TaskCoroutineID::ResumableTask) {
        auto ret = static_cast<ttg::resumable_task>(ttg::coroutine_handle<ttg::resumable_task_state>::from_address(suspended_task_address));
        assert(ret.ready());
        auto old_output_tls_ptr = task->tt->outputs_tls_ptr_accessor();
        task->tt->set_outputs_tls_ptr();
        ret.resume();
        if (ret.completed()) {
          ret.destroy();
          suspended_task_address = nullptr;
        }
        else { // not yet completed
          // leave suspended_task_address as is

          // right now can events are not properly implemented, we are only testing the workflow with dummy events
          // so mark the events finished manually, parsec will rerun this task again and it should complete the second time
          auto events = static_cast<ttg::resumable_task>(ttg::coroutine_handle<ttg::resumable_task_state>::from_address(suspended_task_address)).events();
          for (auto &event_ptr : events) {
            event_ptr->finish();
          }
          assert(ttg::coroutine_handle<ttg::resumable_task_state>::from_address(suspended_task_address).promise().ready());
        }
        task->tt->set_outputs_tls_ptr(old_output_tls_ptr);
        detail::starpu_ttg_caller = nullptr;
        task->suspended_task_address = suspended_task_address;
      }
      else
        ttg::abort();  // unrecognized task id
#else // TTG_HAVE_COROUTINE
      ttg::abort();  // should not happen
#endif  // TTG_HAVE_COROUTINE
      }
#ifdef TTG_HAVE_COROUTINE
      task->suspended_task_address = suspended_task_address;
#endif // TTG_HAVE_COROUTINE
      if (suspended_task_address == nullptr) {
        ttT *baseobj = task->tt;
        derivedT *obj = static_cast<derivedT *>(baseobj);
        if (obj->tracing()) {
          if constexpr (!ttg::meta::is_void_v<keyT>)
            ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : ", task->key, ": done executing");
          else
            ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : done executing");
        }
      }

      return STARPU_HOOK_RETURN_DONE;
    }

    static starpu_hook_return_t static_op_noarg(starpu_task_t *starpu_task) {
      task_t *task = static_cast<task_t*>(starpu_task);

      void* suspended_task_address =
#ifdef TTG_HAVE_COROUTINE
        task->suspended_task_address;  // non-null = need to resume the task
#else // TTG_HAVE_COROUTINE
        nullptr;
#endif // TTG_HAVE_COROUTINE
      if (suspended_task_address == nullptr) {  // task is a coroutine that has not started or an ordinary function
        ttT *baseobj = (ttT *)task->object_ptr;
        derivedT *obj = (derivedT *)task->object_ptr;
        assert(detail::starpu_ttg_caller == NULL);
        detail::starpu_ttg_caller = task;
        if constexpr (!ttg::meta::is_void_v<keyT>) {
          TTG_PROCESS_TT_OP_RETURN(suspended_task_address, task->coroutine_id, baseobj->op(task->key, obj->output_terminals));
        } else if constexpr (ttg::meta::is_void_v<keyT>) {
          TTG_PROCESS_TT_OP_RETURN(suspended_task_address, task->coroutine_id, baseobj->op(obj->output_terminals));
        } else  // unreachable
          ttg:: abort();
        detail::starpu_ttg_caller = NULL;
      }
      else {
#ifdef TTG_HAVE_COROUTINE
        auto ret = static_cast<ttg::resumable_task>(ttg::coroutine_handle<ttg::resumable_task_state>::from_address(suspended_task_address));
        assert(ret.ready());
        ret.resume();
        if (ret.completed()) {
          ret.destroy();
          suspended_task_address = nullptr;
        }
        else { // not yet completed
          // leave suspended_task_address as is
        }
#else  // TTG_HAVE_COROUTINE
        ttg::abort();  // should not happen
#endif // TTG_HAVE_COROUTINE
      }
      task->suspended_task_address = suspended_task_address;

      if (suspended_task_address) {
        ttg::abort();  // not yet implemented
        // see comments in static_op()
        return -1;//PA_HOOK_RETURN_AGAIN
      }
      else
        return 0;//PA_HOOK_RETURN_DONE
    }

    template <std::size_t i>
    static starpu_hook_return_t static_reducer_op(void *es, starpu_task_t *starpu_task) {
      using rtask_t = detail::reducer_task_t;
      using value_t = std::tuple_element_t<i, actual_input_tuple_type>;
      constexpr const bool val_is_void = ttg::meta::is_void_v<value_t>;
      constexpr const bool input_is_const = std::is_const_v<value_t>;
      rtask_t *rtask = (rtask_t*)starpu_task;
      task_t *parent_task = static_cast<task_t*>(rtask->parent_task);
      ttT *baseobj = parent_task->tt;
      derivedT *obj = static_cast<derivedT *>(baseobj);

      auto& reducer = std::get<i>(baseobj->input_reducers);

      //std::cout << "static_reducer_op " << parent_task->key << std::endl;

      if (obj->tracing()) {
        if constexpr (!ttg::meta::is_void_v<keyT>)
          ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : ", parent_task->key, ": reducer executing");
        else
          ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : reducer executing");
      }

      /* the copy to reduce into */
      detail::ttg_data_copy_t *target_copy;
      target_copy = parent_task->copies[i];
      assert(val_is_void || nullptr != target_copy);
      /* once we hit 0 we have to stop since another thread might enqueue a new reduction task */
      std::size_t c = 0;
      std::size_t size = 0;
      assert(parent_task->streams[i].reduce_count > 0);
      if (rtask->is_first) {
        if (0 == (parent_task->streams[i].reduce_count.fetch_sub(1, std::memory_order_acq_rel)-1)) {
          /* we were the first and there is nothing to be done */
          if (obj->tracing()) {
            if constexpr (!ttg::meta::is_void_v<keyT>)
              ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : ", parent_task->key, ": first reducer empty");
            else
              ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : first reducer empty");
          }

          return 0;//PA_HOOK_RETURN_DONE
        }
      }

      assert(detail::starpu_ttg_caller == NULL);
      detail::starpu_ttg_caller = rtask->parent_task;

      do {
        if constexpr(!val_is_void) {
          /* the copies to reduce out of */
          detail::ttg_data_copy_t *source_copy;
          // parsec_list_item_t *item;
          // item = parsec_lifo_pop(&parent_task->streams[i].reduce_copies);
          // if (nullptr == item) {
          //   // maybe someone is changing the goal right now
          //   break;
          // }
          // source_copy = ((detail::ttg_data_copy_self_t *)(item))->self;
          assert(target_copy->num_readers() == target_copy->mutable_tag);
          assert(source_copy->num_readers() > 0);
          reducer(*reinterpret_cast<std::decay_t<value_t> *>(target_copy->get_ptr()),
                  *reinterpret_cast<std::decay_t<value_t> *>(source_copy->get_ptr()));
          detail::release_data_copy(source_copy);
        } else if constexpr(val_is_void) {
          reducer(); // invoke control reducer
        }
        // there is only one task working on this stream, so no need to be atomic here
        size = ++parent_task->streams[i].size;
        //std::cout << "static_reducer_op size " << size << " of " << parent_task->streams[i].goal << std::endl;
      } while ((c = (parent_task->streams[i].reduce_count.fetch_sub(1, std::memory_order_acq_rel)-1)) > 0);
      //} while ((c = (--task->streams[i].reduce_count)) > 0);

      /* finalize_argstream sets goal to 1, so size may be larger than goal */
      bool complete = (size >= parent_task->streams[i].goal);

      //std::cout << "static_reducer_op size " << size
      //          << " of " << parent_task->streams[i].goal << " complete " << complete
      //          << " c " << c << std::endl;
      if (complete && c == 0) {
        if constexpr(input_is_const) {
          /* make the consumer task a reader if its input is const */
          target_copy->reset_readers();
        }
        /* task may not be runnable yet because other inputs are missing, have release_task decide */
        parent_task->remove_from_hash = true;
        parent_task->release_task(parent_task);
      }

      detail::starpu_ttg_caller = NULL;

      if (obj->tracing()) {
        if constexpr (!ttg::meta::is_void_v<keyT>)
          ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : ", parent_task->key, ": done executing");
        else
          ttg::trace(obj->get_world().rank(), ":", obj->get_name(), " : done executing");
      }

      return 0;//PA_HOOK_RETURN_DONE
    }


   protected:
    template <typename T>
    uint64_t unpack(T &obj, void *_bytes, uint64_t pos) {
      using dd_t = ttg::default_data_descriptor<ttg::meta::remove_cvr_t<T>>;
      uint64_t payload_size;
      if constexpr (!dd_t::serialize_size_is_const) {
        pos = ttg::default_data_descriptor<uint64_t>::unpack_payload(&payload_size, sizeof(uint64_t), pos, _bytes);
      } else {
        payload_size = dd_t::payload_size(&obj);
      }
      pos = dd_t::unpack_payload(&obj, payload_size, pos, _bytes);
      return pos;
    }

    template <typename T>
    uint64_t pack(T &obj, void *bytes, uint64_t pos, detail::ttg_data_copy_t *copy = nullptr) {
      using dd_t = ttg::default_data_descriptor<ttg::meta::remove_cvr_t<T>>;
      uint64_t payload_size = dd_t::payload_size(&obj);
      if constexpr (!dd_t::serialize_size_is_const) {
        pos = ttg::default_data_descriptor<uint64_t>::pack_payload(&payload_size, sizeof(uint64_t), pos, bytes);
      }
      pos = dd_t::pack_payload(&obj, payload_size, pos, bytes);
      return pos;
    }

    static void static_set_arg(void *data, std::size_t size, ttg::TTBase *bop) {
      assert(size >= sizeof(msg_header_t) &&
             "Trying to unpack as message that does not hold enough bytes to represent a single header");
      msg_header_t *hd = static_cast<msg_header_t *>(data);
      derivedT *obj = reinterpret_cast<derivedT *>(bop);
      switch (hd->fn_id) {
        case msg_header_t::MSG_SET_ARG: {
          if (0 <= hd->param_id) {
            assert(hd->param_id >= 0);
            assert(hd->param_id < obj->set_arg_from_msg_fcts.size());
            auto member = obj->set_arg_from_msg_fcts[hd->param_id];
            (obj->*member)(data, size);
          } else {
            // there is no good reason to have negative param ids
            ttg::abort();
          }
          break;
        }
        case msg_header_t::MSG_SET_ARGSTREAM_SIZE: {
          assert(hd->param_id >= 0);
          assert(hd->param_id < obj->set_argstream_size_from_msg_fcts.size());
          auto member = obj->set_argstream_size_from_msg_fcts[hd->param_id];
          (obj->*member)(data, size);
          break;
        }
        case msg_header_t::MSG_FINALIZE_ARGSTREAM_SIZE: {
          assert(hd->param_id >= 0);
          assert(hd->param_id < obj->finalize_argstream_from_msg_fcts.size());
          auto member = obj->finalize_argstream_from_msg_fcts[hd->param_id];
          (obj->*member)(data, size);
          break;
        }
        case msg_header_t::MSG_GET_FROM_PULL: {
          assert(hd->param_id >= 0);
          assert(hd->param_id < obj->get_from_pull_msg_fcts.size());
          auto member = obj->get_from_pull_msg_fcts[hd->param_id];
          (obj->*member)(data, size);
          break;
        }
        default:
          ttg::abort();
      }
    }

    /** Returns the task memory pool owned by the calling thread */
    // inline parsec_thread_mempool_t *get_task_mempool(void) {
    //   auto &world_impl = world.impl();
    //   starpu_execution_stream_s *es = world_impl.execution_stream();
    //   int index = (es->virtual_process->vp_id * es->virtual_process->nb_cores + es->th_id);
    //   return &mempools.thread_mempools[index];
    // }

    template <size_t i, typename valueT>
    void set_arg_from_msg_keylist(ttg::span<keyT> &&keylist, detail::ttg_data_copy_t *copy) {
      /* create a dummy task that holds the copy, which can be reused by others */
      task_t *dummy = nullptr;

      /* save the current task and set the dummy task */
      auto starpu_ttg_caller_save = detail::starpu_ttg_caller;
      detail::starpu_ttg_caller = dummy;

      /* iterate over the keys and have them use the copy we made */
      std::vector<starpu_task_t*> task_ring;
      for (auto &&key : keylist) {
        // copy-constructible? can broadcast to any number of keys
        if constexpr (std::is_copy_constructible_v<valueT>) {
          set_arg_local_impl<i>(key, *reinterpret_cast<valueT *>(copy->get_ptr()), copy, &task_ring);
        }
        else {
          // not copy-constructible? can move, but only to single key
          static_assert(!std::is_reference_v<valueT>);
          if (std::size(keylist) == 1)
            set_arg_local_impl<i>(key, std::move(*reinterpret_cast<valueT *>(copy->get_ptr())), copy, &task_ring);
          else {
            throw std::logic_error(std::string("TTG::StarPU: need to copy a datum of type") + typeid(std::decay_t<valueT>).name() + " but the type is not copyable");
          }
        }
      }

      if (!task_ring.empty()) {
        for (auto *task_ptr : task_ring) {
          starpu_task_submit(task_ptr);
        }
      }

      /* restore the previous task */
      detail::starpu_ttg_caller = starpu_ttg_caller_save;
    }

    // there are 6 types of set_arg:
    // - case 1: nonvoid Key, complete Value type
    // - case 2: nonvoid Key, void Value, mixed (data+control) inputs
    // - case 3: nonvoid Key, void Value, no inputs
    // - case 4:    void Key, complete Value type
    // - case 5:    void Key, void Value, mixed (data+control) inputs
    // - case 6:    void Key, void Value, no inputs
    // implementation of these will be further split into "local-only" and global+local

    template <std::size_t i>
    void set_arg_from_msg(void *data, std::size_t size) {
      using valueT = std::tuple_element_t<i, actual_input_tuple_type>;
      using msg_t = detail::msg_t;
      msg_t *msg = static_cast<msg_t *>(data);
      if constexpr (!ttg::meta::is_void_v<keyT>) {
        /* unpack the keys */
        /* TODO: can we avoid copying all the keys?! */
        uint64_t pos = msg->tt_id.key_offset;
        uint64_t key_end_pos;
        std::vector<keyT> keylist;
        int num_keys = msg->tt_id.num_keys;
        keylist.reserve(num_keys);
        auto rank = world.rank();
        for (int k = 0; k < num_keys; ++k) {
          keyT key;
          pos = unpack(key, msg->bytes, pos);
          assert(keymap(key) == rank);
          keylist.push_back(std::move(key));
        }
        key_end_pos = pos;
        /* jump back to the beginning of the message to get the value */
        pos = 0;
        // case 1
        if constexpr (!ttg::meta::is_void_v<valueT>) {
          using decvalueT = std::decay_t<valueT>;
          int32_t num_iovecs = msg->tt_id.num_iovecs;
          //bool inline_data = msg->inline_data;
          detail::ttg_data_copy_t *copy;
          if constexpr (ttg::has_split_metadata<decvalueT>::value) {
            ttg::SplitMetadataDescriptor<decvalueT> descr;
            using metadata_t = decltype(descr.get_metadata(std::declval<decvalueT>()));

            /* unpack the metadata */
            metadata_t metadata;
            pos = unpack(metadata, msg->bytes, pos);

            //std::cout << "set_arg_from_msg splitmd num_iovecs " << num_iovecs << std::endl;

            copy = detail::create_new_datacopy(descr.create_from_metadata(metadata));
          } else if constexpr (!ttg::has_split_metadata<decvalueT>::value) {
            copy = detail::create_new_datacopy(decvalueT{});
            /* unpack the object, potentially discovering iovecs */
            pos = unpack(*static_cast<decvalueT *>(copy->get_ptr()), msg->bytes, pos);
          }

          if (num_iovecs == 0) {
            set_arg_from_msg_keylist<i, decvalueT>(ttg::span<keyT>(&keylist[0], num_keys), copy);
          } else {
            /* unpack the header and start the RMA transfers */

            /* get the remote rank */
            int remote = msg->tt_id.sender;
            assert(remote < world.size());

            auto &val = *static_cast<decvalueT *>(copy->get_ptr());

            bool inline_data = msg->tt_id.inline_data;

            int nv = 0;
            //starpu_ce_tag_t cbtag;
            /* start the RMA transfers */
            auto create_activation_fn = [&]() {
              /* extract the callback tag */
              // std::memcpy(&cbtag, msg->bytes + pos, sizeof(cbtag));
              // pos += sizeof(cbtag);

              copy->add_ref(); // so we can safely decrement the readers in the activation
              /* create the value from the metadata */
              auto activation = new detail::rma_delayed_activate(
                  std::move(keylist), copy, num_iovecs, [this, &val](std::vector<keyT> &&keylist, detail::ttg_data_copy_t *copy) {
                    set_arg_from_msg_keylist<i, decvalueT>(keylist, copy);
                    this->world.impl().decrement_inflight_msg();
                    detail::foreach_starpu_data(val, [&](auto *data){
                      // TODO: decrement reader counter on equivalent StarPU data copy.
                    });
                    copy->drop_ref();
                  });
              return activation;
            };
            auto read_inline_data = [&](auto&& iovec){
              /* unpack the data from the message */
              ++nv;
              std::memcpy(iovec.data, msg->bytes + pos, iovec.num_bytes);
              pos += iovec.num_bytes;
            };
            auto handle_iovec_fn = [&](auto&& iovec, auto activation) {
              using ActivationT = std::decay_t<decltype(*activation)>;

              ++nv;
              void *rreg;
              int32_t rreg_size_i;
              std::memcpy(&rreg_size_i, msg->bytes + pos, sizeof(rreg_size_i));
              pos += sizeof(rreg_size_i);
              rreg = static_cast<void *>(msg->bytes + pos);
              pos += rreg_size_i;
              // std::intptr_t *fn_ptr = reinterpret_cast<std::intptr_t *>(msg->bytes + pos);
              // pos += sizeof(*fn_ptr);
              std::intptr_t fn_ptr;
              std::memcpy(&fn_ptr, msg->bytes + pos, sizeof(fn_ptr));
              pos += sizeof(fn_ptr);

              /* register the local memory */
              void * lreg;
              size_t lreg_size;
              // parsec_ce.mem_register(iovec.data, PARSEC_MEM_TYPE_NONCONTIGUOUS, iovec.num_bytes, parsec_datatype_int8_t,
              //                        iovec.num_bytes, &lreg, &lreg_size);
              world.impl().increment_inflight_msg();
              /* TODO: PaRSEC should treat the remote callback as a tag, not a function pointer! */
              //std::cout << "set_arg_from_msg: get rreg " << rreg << " remote " << remote << std::endl;
              // parsec_ce.get(&parsec_ce, lreg, 0, rreg, 0, iovec.num_bytes, remote,
              //               &detail::get_complete_cb<ActivationT>, activation,
              //               /*world.impl().parsec_ttg_rma_tag()*/
              //               cbtag, &fn_ptr, sizeof(std::intptr_t));
            };
            /* make sure all buffers are properly allocated */
            ttg::detail::buffer_apply(val, [&]<typename T, typename A>(const ttg::Buffer<T, A>& b){
              /* cast away const */
              auto& buffer = const_cast<ttg::Buffer<T, A>&>(b);
              /* remember which device we used last time */
              static auto last_device = ttg::device::Device{0, Space};
              ttg::device::Device device;
              if (inline_data || !world.impl().mpi_support(Space))  {
                device = ttg::device::Device::host(); // have to allocate on host
              } else if (!keylist.empty() && devicemap) {
                device = devicemap(keylist[0]); // pick a device we know will use the data
              } else {
                device = last_device; // use the previously used device
              }
              // remember where we started so we can cycle through all devices once
              auto start_device = device;
              do {
                /* try to allocate on any device */
                try {
                  buffer.allocate_on(device);
                  buffer.set_owner_device(device);
                  break;
                } catch (const std::bad_alloc&) {
                  device = device.cycle();
                  if (device == start_device) {
                    /* make sure we have memory on the host */
                    buffer.allocate_on(ttg::device::Device::host());
                    break; // failed to find a device that works
                  }
                  last_device = device;
                }
              } while(true);
            });

            /* kick off transfers */
            if constexpr (ttg::has_split_metadata<decvalueT>::value) {
              ttg::SplitMetadataDescriptor<decvalueT> descr;
              if (inline_data) {
                for (auto&& iov : descr.get_data(val)) {
                  read_inline_data(iov);
                }
              } else {
                auto activation = create_activation_fn();
                for (auto&& iov : descr.get_data(val)) {
                  handle_iovec_fn(iov, activation);
                }
              }
            } else if constexpr (!ttg::has_split_metadata<decvalueT>::value) {
              if (inline_data) {
                detail::foreach_starpu_data(val, [&](auto *data){
                  read_inline_data(ttg::iovec{data->nb_elts, data->device_copies[data->owner_device]->device_private});
                });
              } else {
                auto activation = create_activation_fn();
                detail::foreach_starpu_data(val, [&](auto *data){
                  //TODO: increment reader counter on equivalent StarPU data copy.
                  handle_iovec_fn(ttg::iovec{data->nb_elts, data->device_copies[data->owner_device]->device_private}, activation);
                });
              }
            }

            assert(num_iovecs == nv);
            assert(size == (key_end_pos + sizeof(msg_header_t)));

            if (inline_data) {
              set_arg_from_msg_keylist<i, decvalueT>(ttg::span<keyT>(&keylist[0], num_keys), copy);
            }
          }
          // case 2 and 3
        } else if constexpr (!ttg::meta::is_void_v<keyT> && std::is_void_v<valueT>) {
          for (auto &&key : keylist) {
            set_arg<i, keyT, ttg::Void>(key, ttg::Void{});
          }
        }
        // case 4
      } else if constexpr (ttg::meta::is_void_v<keyT> && !std::is_void_v<valueT>) {
        using decvalueT = std::decay_t<valueT>;
        decvalueT val;
        /* TODO: handle split-metadata case as with non-void keys */
        unpack(val, msg->bytes, 0);
        set_arg<i, keyT, valueT>(std::move(val));
        // case 5 and 6
      } else if constexpr (ttg::meta::is_void_v<keyT> && std::is_void_v<valueT>) {
        set_arg<i, keyT, ttg::Void>(ttg::Void{});
      } else {  // unreachable
        ttg::abort();
      }
    }

    template <std::size_t i>
    void finalize_argstream_from_msg(void *data, std::size_t size) {
      using msg_t = detail::msg_t;
      msg_t *msg = static_cast<msg_t *>(data);
      if constexpr (!ttg::meta::is_void_v<keyT>) {
        /* unpack the key */
        uint64_t pos = 0;
        auto rank = world.rank();
        keyT key;
        pos = unpack(key, msg->bytes, pos);
        assert(keymap(key) == rank);
        finalize_argstream<i>(key);
      } else {
        auto rank = world.rank();
        assert(keymap() == rank);
        finalize_argstream<i>();
      }
    }

    template <std::size_t i>
    void argstream_set_size_from_msg(void *data, std::size_t size) {
      using msg_t = detail::msg_t;
      auto msg = static_cast<msg_t *>(data);
      uint64_t pos = 0;
      if constexpr (!ttg::meta::is_void_v<keyT>) {
        /* unpack the key */
        auto rank = world.rank();
        keyT key;
        pos = unpack(key, msg->bytes, pos);
        assert(keymap(key) == rank);
        std::size_t argstream_size;
        pos = unpack(argstream_size, msg->bytes, pos);
        set_argstream_size<i>(key, argstream_size);
      } else {
        auto rank = world.rank();
        assert(keymap() == rank);
        std::size_t argstream_size;
        pos = unpack(argstream_size, msg->bytes, pos);
        set_argstream_size<i>(argstream_size);
      }
    }

    template <std::size_t i>
    void get_from_pull_msg(void *data, std::size_t size) {
      using msg_t = detail::msg_t;
      msg_t *msg = static_cast<msg_t *>(data);
      auto &in = std::get<i>(input_terminals);
      if constexpr (!ttg::meta::is_void_v<keyT>) {
        /* unpack the key */
        uint64_t pos = 0;
        keyT key;
        pos = unpack(key, msg->bytes, pos);
        set_arg<i>(key, (in.container).get(key));
      }
    }

    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg_local(
        const Key &key, Value &&value) {
      set_arg_local_impl<i>(key, std::forward<Value>(value));
    }

    template <std::size_t i, typename Key = keyT, typename Value>
    std::enable_if_t<ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg_local(
        Value &&value) {
      set_arg_local_impl<i>(ttg::Void{}, std::forward<Value>(value));
    }

    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg_local(
        const Key &key, const Value &value) {
      set_arg_local_impl<i>(key, value);
    }

    template <std::size_t i, typename Key = keyT, typename Value>
    std::enable_if_t<ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg_local(
        const Value &value) {
      set_arg_local_impl<i>(ttg::Void{}, value);
    }

    template <std::size_t i, typename Key = keyT, typename Value>
    std::enable_if_t<ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg_local(
        std::shared_ptr<const Value> &valueptr) {
      set_arg_local_impl<i>(ttg::Void{}, *valueptr);
    }

    template <typename Key>
    task_t* create_new_task(const Key &key) {
      constexpr const bool keyT_is_Void = ttg::meta::is_void_v<keyT>;
      auto &world_impl = world.impl();
      int32_t priority = 0;
      task_t* newtask = [&]() -> task_t* {
        if constexpr (!keyT_is_Void) {
          priority = priomap(key);
          return new task_t(key, priority, this);
        } else {
          priority = priomap();
          return new task_t(priority, this);
        }
      }();

      for (int i = 0; i < static_stream_goal.size(); ++i) {
        newtask->streams[i].goal = static_stream_goal[i];
      }

      ttg::trace(world.rank(), ":", get_name(), " : ", key, ": creating task");
      return newtask;
    }


    template <std::size_t i>
    detail::reducer_task_t *create_new_reducer_task(task_t *task, bool is_first) {
      /* make sure we can reuse the existing memory pool and don't have to create a new one */
      static_assert(sizeof(task_t) >= sizeof(detail::reducer_task_t));
      constexpr const bool keyT_is_Void = ttg::meta::is_void_v<keyT>;
      auto &world_impl = world.impl();
      detail::reducer_task_t *newtask;
      //parsec_thread_mempool_t *mempool = get_task_mempool();
      //char *taskobj = (char *)parsec_thread_mempool_allocate(mempool);
      // use the priority of the task we stream into
      int32_t priority = 0;
      if constexpr (!keyT_is_Void) {
        priority = priomap(task->key);
        ttg::trace(world.rank(), ":", get_name(), " : ", task->key, ": creating reducer task");
      } else {
        priority = priomap();
        ttg::trace(world.rank(), ":", get_name(), ": creating reducer task");
      }
      /* placement-new the task */
      // newtask = new (taskobj) detail::reducer_task_t(task, mempool, inpute_reducers_taskclass[i],
      //                                                world_impl.taskpool(), priority, is_first);

      return newtask;
    }


    // Used to set the i'th argument
    template <std::size_t i, typename Key, typename Value>
    void set_arg_local_impl(const Key &key, Value &&value, detail::ttg_data_copy_t *copy_in = nullptr,
                            std::vector<starpu_task_t*> *task_ring = nullptr) {
      using valueT = std::tuple_element_t<i, input_values_full_tuple_type>;
      constexpr const bool input_is_const = std::is_const_v<std::tuple_element_t<i, input_args_type>>;
      constexpr const bool valueT_is_Void = ttg::meta::is_void_v<valueT>;
      constexpr const bool keyT_is_Void = ttg::meta::is_void_v<Key>;


      ttg::trace(world.rank(), ":", get_name(), " : ", key, ": received value for argument : ", i);
      
      if constexpr (keyT_is_Void) {
        key = 0;
      }
      else{
        assert(keymap(key) == world.rank());
      }
      task_t *task = nullptr;
      auto &world_impl = world.impl();
      auto &reducer = std::get<i>(input_reducers);
      bool release = false;
      bool remove_from_hash = true;
      bool to_remove = false;
      bool get_pull_data = false;
      /* If we have only one input and no reducer on that input we can skip the hash table */

      auto callback_fn = [&](task_t *task) {
        auto get_copy_fn = [&](detail::starpu_ttg_task_base_t *task, auto&& value, bool is_const){
          detail::ttg_data_copy_t *copy = copy_in;
          if (nullptr == copy && nullptr != detail::starpu_ttg_caller) {
            copy = detail::find_copy_in_task(detail::starpu_ttg_caller, &value);
          }
          if (nullptr != copy) {
            /* retain the data copy */
            copy = detail::register_data_copy<valueT>(copy, task, is_const);
          } else {
            /* create a new copy */
            copy = detail::create_new_datacopy(std::forward<Value>(value));
            if (!is_const) {
              copy->mark_mutable();
            }
          }
          return copy;
        };

        if (reducer && 1 != task->streams[i].goal) {  // is this a streaming input? reduce the received value
          auto submit_reducer_task = [&](auto *parent_task){
            /* check if we need to create a task */
            std::size_t c = parent_task->streams[i].reduce_count.fetch_add(1, std::memory_order_acquire);
            //std::cout << "submit_reducer_task " << key << " c " << c << std::endl;
            if (0 == c) {
              /* we are responsible for creating the reduction task */
              detail::reducer_task_t *reduce_task;
              reduce_task = create_new_reducer_task<i>(parent_task, false);
              reduce_task->release_task(reduce_task); // release immediately
            }
          };

          if constexpr (!ttg::meta::is_void_v<valueT>) {  // for data values
            // have a value already? if not, set, otherwise reduce
            detail::ttg_data_copy_t *copy = nullptr;
            if (nullptr == (copy = task->copies[i])) {
              using decay_valueT = std::decay_t<valueT>;

              /* first input value, create a task and bind it to the copy */
              //std::cout << "Creating new reducer task for " << key << std::endl;
              detail::reducer_task_t *reduce_task;
              reduce_task = create_new_reducer_task<i>(task, true);

              /* protected by the bucket lock */
              task->streams[i].size = 1;
              task->streams[i].reduce_count.store(1, std::memory_order_relaxed);

              /* get the copy to use as input for this task */
              detail::ttg_data_copy_t *copy = get_copy_fn(reduce_task, std::forward<Value>(value), false);

              /* put the copy into the task */
              task->copies[i] = copy;

              /* release the task if we're not deferred
              * TODO: can we delay that until we get the second value?
              */
              if (copy->get_next_task() != reduce_task->starpu_task) {
                reduce_task->release_task(reduce_task);
              }

            } else {


              /* get the copy to use as input for this task */
              detail::ttg_data_copy_t *copy = get_copy_fn(task, std::forward<Value>(value), true);

              /* enqueue the data copy to be reduced */
              // parsec_lifo_push(&task->streams[i].reduce_copies, &copy->super);
              submit_reducer_task(task);
            }
          } else {

            submit_reducer_task(task);
          }
        } else {
          
          /* whether the task needs to be deferred or not */
          if constexpr (!valueT_is_Void) {
            if (nullptr != task->copies[i]) {
              ttg::print_error(get_name(), " : ", key, ": error argument is already set : ", i);
              throw std::logic_error("bad set arg");
            }

            /* get the copy to use as input for this task */
            detail::ttg_data_copy_t *copy = get_copy_fn(task, std::forward<Value>(value), input_is_const);

            /* if we registered as a writer and were the first to register with this copy
            * we need to defer the release of this task to give other tasks a chance to
            * make a copy of the original data */
            release = (copy->get_next_task() != task->starpu_task);
            task->copies[i] = copy;
          } else {
            release = true;
          }
        }
      };


      if (numins > 1 || reducer) {
        
        this->tasks_table->starpu_hash_table_try_emplace_and_visit(key, [&](){
          task = create_new_task(key);
          world_impl.increment_created();
          get_pull_data = !is_lazy_pull();
          callback_fn(task);
          return task;
        }, [&](auto& item){
          if(!reducer && numins == (item->in_data_count + 1)) {
            to_remove = true;
          }
          callback_fn(item);
	  task = item;
        });
        if(to_remove) {
          task = this->tasks_table->starpu_hash_table_remove(key,[](auto& item){return true;});
          task->remove_from_hash = false;
        }
      } else {
        task = create_new_task(key);
        world_impl.increment_created();
        callback_fn(task);
        task->remove_from_hash = false;
      }
      //std::cout << "KEY: " << key << "goal: " << task->in_data_count << std::endl;
      if (release) {
        release_task(task, task_ring);
      }
      /* if not pulling lazily, pull the data here */
      if constexpr (!ttg::meta::is_void_v<keyT>) {
        if (get_pull_data) {
          invoke_pull_terminals(std::make_index_sequence<std::tuple_size_v<input_values_tuple_type>>{}, task->key, task);
        }
      }
    }

    bool check_constraints(task_t *task) {
      bool constrained = false;
      if (constraints_check.size() > 0) {
        if constexpr (ttg::meta::is_void_v<keyT>) {
          constrained = !constraints_check[0]();
        } else {
          constrained = !constraints_check[0](task->key);
        }
      }
      if (constrained) {
        // store the task so we can later access it once it is released
        this->task_constraint_table->starpu_hash_table_insert(task->pkey(), task);
      }
      return !constrained;
    }

    template<typename Key = keyT>
    std::enable_if_t<ttg::meta::is_void_v<Key>, void> release_constraint(std::size_t cid) {
      // check the next constraint, if any
      assert(cid < constraints_check.size());
      bool release = true;
      for (std::size_t i = cid+1; i < constraints_check.size(); i++) {
        if (!constraints_check[i]()) {
          release = false;
          break;
        }
      }
      if (release) {
        // no constraint blocked us
        task_t *task;
        key_type hk = 0;
        task = this->task_constraint_table->starpu_hash_table_remove(hk,[](auto& item){return true;});
        assert(task != nullptr);
        auto &world_impl = world.impl();
        //starpu_execution_stream_t *es = world_impl.execution_stream();
        starpu_task_submit(task->starpu_task);
      }
    }

    template<typename Key = keyT>
    std::enable_if_t<!ttg::meta::is_void_v<Key>, void> release_constraint(std::size_t cid, const std::span<Key>& keys) {
      assert(cid < constraints_check.size());
      std::vector<starpu_task_t*> task_ring;
      for (auto& key : keys) {
        task_t *task;
        bool release = true;
        for (std::size_t i = cid+1; i < constraints_check.size(); i++) {
          if (!constraints_check[i](key)) {
            release = false;
            break;
          }
        }

        if (release) {
          // no constraint blocked this task, so go ahead and release
          task = this->task_constraint_table->starpu_hash_table_remove(key,[](auto& item){return true;});
          assert(task != nullptr);
          task_ring.push_back(&task->starpu_task);
        }
      }
      if (!task_ring.empty()) {
        for (auto *task_ptr : task_ring) {
          starpu_task_submit(task_ptr);
        }
      }
    }

    void release_task(task_t *task,
                      std::vector<starpu_task_t*> *task_ring = nullptr) {
      constexpr const bool keyT_is_Void = ttg::meta::is_void_v<keyT>;

      /* if remove_from_hash == false, someone has already removed the task from the hash table
       * so we know that the task is ready, no need to do atomic increments here */
      bool is_ready = !task->remove_from_hash;
      int32_t count;
      if (is_ready) {
        count = numins;
      } else {
        count = __atomic_fetch_add(&task->in_data_count, 1, __ATOMIC_ACQ_REL) + 1;
        // assert(count <= self.dependencies_goal);
      }

      auto &world_impl = world.impl();
      ttT *baseobj = task->tt;

      if (count == numins) {
        //starpu_execution_stream_t *es = world_impl.execution_stream();
        key_type hk = task->pkey();
        if (tracing()) {
          if constexpr (!keyT_is_Void) {
            ttg::trace(world.rank(), ":", get_name(), " : ", task->key, ": submitting task for op ");
          } else {
            ttg::trace(world.rank(), ":", get_name(), ": submitting task for op ");
          }
        }
        if (task->remove_from_hash) this->tasks_table->starpu_hash_table_remove(hk,[](auto& item){return true;});

        if (check_constraints(task)) {
          if (nullptr == task_ring) {
            starpu_task_submit(task->starpu_task);
          } else {
            task_ring->push_back(task->starpu_task);
          }
        }
      } else if constexpr (!ttg::meta::is_void_v<keyT>) {
        if ((baseobj->num_pullins + count == numins) && baseobj->is_lazy_pull()) {
          /* lazily pull the pull terminal data */
          baseobj->invoke_pull_terminals(std::make_index_sequence<std::tuple_size_v<input_values_tuple_type>>{}, task->key, task);
        }
      }
    }

    // cases 1+2
    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg(const Key &key,
                                                                                                       Value &&value) {
      set_arg_impl<i>(key, std::forward<Value>(value));
    }

    // cases 4+5+6
    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>, void> set_arg(Value &&value) {
      set_arg_impl<i>(ttg::Void{}, std::forward<Value>(value));
    }

    template <std::size_t i, typename Key = keyT>
    std::enable_if_t<ttg::meta::is_void_v<Key>, void> set_arg() {
      set_arg_impl<i>(ttg::Void{}, ttg::Void{});
    }

    // case 3
    template <std::size_t i, typename Key>
    std::enable_if_t<!ttg::meta::is_void_v<Key>, void> set_arg(const Key &key) {
      set_arg_impl<i>(key, ttg::Void{});
    }

    template<typename Value, typename Key>
    bool can_inline_data(Value* value_ptr, detail::ttg_data_copy_t *copy, const Key& key, std::size_t num_keys) {
      /* non-device data */
      using decvalueT = std::decay_t<Value>;
      bool inline_data = false;
      /* check whether to send data in inline */
      std::size_t iov_size = 0;
      std::size_t metadata_size = 0;
      if constexpr (ttg::has_split_metadata<std::decay_t<Value>>::value) {
        ttg::SplitMetadataDescriptor<decvalueT> descr;
        auto iovs = descr.get_data(*const_cast<decvalueT *>(value_ptr));
        iov_size = std::accumulate(iovs.begin(), iovs.end(), 0,
                                    [](std::size_t s, auto& iov){ return s + iov.num_bytes; });
        auto metadata = descr.get_metadata(*const_cast<decvalueT *>(value_ptr));
        metadata_size = ttg::default_data_descriptor<decltype(metadata)>::payload_size(&metadata);
      } else {
        /* TODO: how can we query the iovecs of the buffers here without actually packing the data? */
        metadata_size = ttg::default_data_descriptor<ttg::meta::remove_cvr_t<Value>>::payload_size(value_ptr);
        // detail::foreach_starpu_data(*value_ptr, [&](parsec_data_t* data){ iov_size += data->nb_elts; });
      }
      /* key is packed at the end */
      std::size_t key_pack_size = ttg::default_data_descriptor<Key>::payload_size(&key);
      std::size_t pack_size = key_pack_size + metadata_size + iov_size;
      if (pack_size < detail::max_inline_size) {
        inline_data = true;
      }
      return inline_data;
    }

    // Used to set the i'th argument
    template <std::size_t i, typename Key, typename Value>
    void set_arg_impl(const Key &key, Value &&value, detail::ttg_data_copy_t *copy_in = nullptr) {
      int owner;
      using decvalueT = std::decay_t<Value>;
      using norefvalueT = std::remove_reference_t<Value>;
      norefvalueT *value_ptr = &value;

      if constexpr (!ttg::meta::is_void_v<Key>)
        owner = keymap(key);
      else
        owner = keymap();
      if (owner == world.rank()) {
        if constexpr (!ttg::meta::is_void_v<keyT>)
          set_arg_local_impl<i>(key, std::forward<Value>(value), copy_in);
        else
          set_arg_local_impl<i>(ttg::Void{}, std::forward<Value>(value), copy_in);

        return;
      }
      // the target task is remote. Pack the information and send it to
      // the corresponding peer.
      // TODO do we need to copy value?
      using msg_t = detail::msg_t;
      auto &world_impl = world.impl();
      uint64_t pos = 0;
      int num_iovecs = 0;
      // std::unique_ptr<msg_t> msg = std::make_unique<msg_t>(get_instance_id(), world_impl.taskpool()->taskpool_id,
      //                                                      msg_header_t::MSG_SET_ARG, i, world_impl.rank(), 1);

      if constexpr (!ttg::meta::is_void_v<decvalueT>) {

        detail::ttg_data_copy_t *copy = copy_in;
        /* make sure we have a data copy to register with */
        if (nullptr == copy) {
          copy = detail::find_copy_in_task(detail::starpu_ttg_caller, value_ptr);
          if (nullptr == copy) {
            // We need to create a copy for this data, as it does not exist yet.
            copy = detail::create_new_datacopy(std::forward<Value>(value));
            // use the new value from here on out
            value_ptr = static_cast<norefvalueT*>(copy->get_ptr());
          }
        }

        bool inline_data = can_inline_data(value_ptr, copy, key, 1);
        // msg->tt_id.inline_data = inline_data;

        auto write_header_fn = [&]() {
          if (!inline_data) {
            /* TODO: at the moment, the tag argument to parsec_ce.get() is treated as a
            * raw function pointer instead of a preregistered AM tag, so play that game.
            * Once this is fixed in PaRSEC we need to use parsec_ttg_rma_tag instead! */
            // starpu_ce_tag_t cbtag = reinterpret_cast<starpu_ce_tag_t>(&detail::get_remote_complete_cb);
            // std::memcpy(msg->bytes + pos, &cbtag, sizeof(cbtag));
            // pos += sizeof(cbtag);
          }
        };
        //auto handle_iovec_fn = [&](auto&& iovec, parsec_data_copy_t *device_copy = nullptr) {

        //   if (inline_data) {
        //     /* inline data is packed right after the tt_id in the message */
        //     std::memcpy(msg->bytes + pos, iovec.data, iovec.num_bytes);
        //     pos += iovec.num_bytes;
        //   } else {

        //     /**
        //      * register the generic iovecs and pack the registration handles
        //      * memory layout: [<lreg_size, lreg, release_cb_ptr>, ...]
        //      */
        //     copy = detail::register_data_copy<decvalueT>(copy, nullptr, true);
        //     void * lreg;
        //     size_t lreg_size;
        //     /* TODO: only register once when we can broadcast the data! */
        //     // parsec_ce.mem_register(iovec.data, PARSEC_MEM_TYPE_NONCONTIGUOUS, iovec.num_bytes, parsec_datatype_int8_t,
        //     //                        iovec.num_bytes, &lreg, &lreg_size);
        //     auto lreg_ptr = std::shared_ptr<void>{lreg, [device_copy](void *ptr) {
        //                                             void *memreg = (void *)ptr;
        //                                             //parsec_ce.mem_unregister(&memreg);
        //                                             if (device_copy != nullptr) {
        //                                               /* remove a reader */
        //                                               //parsec_atomic_fetch_sub_int32(&device_copy->readers, 1);
        //                                             }
        //                                           }};
        //     int32_t lreg_size_i = lreg_size;
        //     std::memcpy(msg->bytes + pos, &lreg_size_i, sizeof(lreg_size_i));
        //     pos += sizeof(lreg_size_i);
        //     std::memcpy(msg->bytes + pos, lreg, lreg_size);
        //     pos += lreg_size;
        //     //std::cout << "set_arg_impl lreg " << lreg << std::endl;
        //     /* TODO: can we avoid the extra indirection of going through std::function? */
        //     std::function<void(void)> *fn = new std::function<void(void)>([=]() mutable {
        //       /* shared_ptr of value and registration captured by value so resetting
        //       * them here will eventually release the memory/registration */
        //       lreg_ptr.reset();
        //       detail::release_data_copy(copy);
        //     });
        //     std::intptr_t fn_ptr{reinterpret_cast<std::intptr_t>(fn)};
        //     std::memcpy(msg->bytes + pos, &fn_ptr, sizeof(fn_ptr));
        //     pos += sizeof(fn_ptr);
        //   }
        // };

        if constexpr (ttg::has_split_metadata<std::decay_t<Value>>::value) {
          ttg::SplitMetadataDescriptor<decvalueT> descr;
          auto iovs = descr.get_data(*const_cast<decvalueT *>(value_ptr));
          num_iovecs = std::distance(std::begin(iovs), std::end(iovs));
          /* pack the metadata */
          auto metadata = descr.get_metadata(*const_cast<decvalueT *>(value_ptr));
          // pos = pack(metadata, msg->bytes, pos);
          //std::cout << "set_arg_impl splitmd num_iovecs " << num_iovecs << std::endl;
          write_header_fn();
          for (auto&& iov : iovs) {
            //handle_iovec_fn(iov);
          }
        } else if constexpr (!ttg::has_split_metadata<std::decay_t<Value>>::value) {
          /* serialize the object */
          // pos = pack(*value_ptr, msg->bytes, pos, copy);
          //detail::foreach_starpu_data(value, [&](parsec_data_t *data){ ++num_iovecs; });
          //std::cout << "POST pack num_iovecs " << num_iovecs << std::endl;
          /* handle any iovecs contained in it */
          write_header_fn();
          // detail::foreach_starpu_data(value, [&](parsec_data_t *data){
          //   int device = 0;
          //   parsec_data_copy_t* device_copy = nullptr;
          //   if (world.impl().mpi_support(Space) && Space != ttg::ExecutionSpace::Host) {
          //     /* Try to find a device that is not the host and has the latest version. */
          //     std::tie(device, device_copy) = detail::find_device_copy(data);
          //   }
          //   handle_iovec_fn(ttg::iovec{data->nb_elts, data->device_copies[device]->device_private},
          //                   device_copy);
          // });
        }

        // msg->tt_id.num_iovecs = num_iovecs;
      }

      /* pack the key */
      // msg->tt_id.num_keys = 0;
      // msg->tt_id.key_offset = pos;
      // if constexpr (!ttg::meta::is_void_v<Key>) {
      //   size_t tmppos = pack(key, msg->bytes, pos);
      //   pos = tmppos;
      //   msg->tt_id.num_keys = 1;
      // }

      //std::cout << "set_arg_impl send_am owner " << owner << " sender " << msg->tt_id.sender << std::endl;
      // detail::send_active_message(owner, msg.get(), sizeof(msg_header_t) + pos);
    }

    template <int i, typename Iterator, typename Value>
    void broadcast_arg_local(Iterator &&begin, Iterator &&end, const Value &value) {

      std::vector<starpu_task_t*> task_ring;
      detail::ttg_data_copy_t *copy = nullptr;
      if (nullptr != detail::starpu_ttg_caller) {
        copy = detail::find_copy_in_task(detail::starpu_ttg_caller, &value);
      }

      for (auto it = begin; it != end; ++it) {
        set_arg_local_impl<i>(*it, value, copy, &task_ring);
      }
      /* submit all ready tasks at once */
      if (!task_ring.empty()) {
        for (auto *task_ptr : task_ring) {
          starpu_task_submit(task_ptr);
        }
      }

    }

    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>,
                     void>
    broadcast_arg(const ttg::span<const Key> &keylist, const Value &value) {
      using valueT = std::tuple_element_t<i, input_values_full_tuple_type>;
      auto world = ttg_default_execution_context();
      auto np = world.size();
      int rank = world.rank();
      uint64_t pos = 0;
      bool have_remote = keylist.end() != std::find_if(keylist.begin(), keylist.end(),
                                                       [&](const Key &key) { return keymap(key) != rank; });

      if (have_remote) {
        
      } else {
        /* handle local keys */
        broadcast_arg_local<i>(keylist.begin(), keylist.end(), value);
      }
    }

    // Used by invoke to set all arguments associated with a task
    // Is: index sequence of elements in args
    // Js: index sequence of input terminals to set
    template <typename Key, typename... Ts, size_t... Is, size_t... Js>
    std::enable_if_t<ttg::meta::is_none_void_v<Key>, void> set_args(std::index_sequence<Is...>,
                                                                    std::index_sequence<Js...>, const Key &key,
                                                                    const std::tuple<Ts...> &args) {
      static_assert(sizeof...(Js) == sizeof...(Is));
      constexpr size_t js[] = {Js...};
      int junk[] = {0, (set_arg<js[Is]>(key, TT::get<Is>(args)), 0)...};
      junk[0]++;
    }

    // Used by invoke to set all arguments associated with a task
    // Is: index sequence of input terminals to set
    template <typename Key, typename... Ts, size_t... Is>
    std::enable_if_t<ttg::meta::is_none_void_v<Key>, void> set_args(std::index_sequence<Is...> is, const Key &key,
                                                                    const std::tuple<Ts...> &args) {
      set_args(std::index_sequence_for<Ts...>{}, is, key, args);
    }

    // Used by invoke to set all arguments associated with a task
    // Is: index sequence of elements in args
    // Js: index sequence of input terminals to set
    template <typename Key = keyT, typename... Ts, size_t... Is, size_t... Js>
    std::enable_if_t<ttg::meta::is_void_v<Key>, void> set_args(std::index_sequence<Is...>, std::index_sequence<Js...>,
                                                               const std::tuple<Ts...> &args) {
      static_assert(sizeof...(Js) == sizeof...(Is));
      constexpr size_t js[] = {Js...};
      int junk[] = {0, (set_arg<js[Is], void>(TT::get<Is>(args)), 0)...};
      junk[0]++;
    }

    // Used by invoke to set all arguments associated with a task
    // Is: index sequence of input terminals to set
    template <typename Key = keyT, typename... Ts, size_t... Is>
    std::enable_if_t<ttg::meta::is_void_v<Key>, void> set_args(std::index_sequence<Is...> is,
                                                               const std::tuple<Ts...> &args) {
      set_args(std::index_sequence_for<Ts...>{}, is, args);
    }

   public:
    /// sets the default stream size for input \c i
    /// \param size positive integer that specifies the default stream size
    template <std::size_t i>
    void set_static_argstream_size(std::size_t size) {
      assert(std::get<i>(input_reducers) && "TT::set_static_argstream_size called on nonstreaming input terminal");
      assert(size > 0 && "TT::set_static_argstream_size(key,size) called with size=0");

      this->trace(world.rank(), ":", get_name(), ": setting global stream size for terminal ", i);

      // Check if stream is already bounded
      if (static_stream_goal[i] < std::numeric_limits<std::size_t>::max()) {
        ttg::print_error(world.rank(), ":", get_name(), " : error stream is already bounded : ", i);
        throw std::runtime_error("TT::set_static_argstream_size called for a bounded stream");
      }

      static_stream_goal[i] = size;
    }

    /// sets stream size for input \c i
    /// \param size positive integer that specifies the stream size
    /// \param key the task identifier that expects this number of inputs in the streaming terminal
    template <std::size_t i, typename Key>
    std::enable_if_t<!ttg::meta::is_void_v<Key>, void> set_argstream_size(const Key &key, std::size_t size) {
      // preconditions
      assert(std::get<i>(input_reducers) && "TT::set_argstream_size called on nonstreaming input terminal");
      assert(size > 0 && "TT::set_argstream_size(key,size) called with size=0");

      // body
      const auto owner = keymap(key);
      if (owner != world.rank()) {
        ttg::trace(world.rank(), ":", get_name(), ":", key, " : forwarding stream size for terminal ", i);
        using msg_t = detail::msg_t;
        auto &world_impl = world.impl();
        uint64_t pos = 0;
        // std::unique_ptr<msg_t> msg = std::make_unique<msg_t>(get_instance_id(), world_impl.taskpool()->taskpool_id,
        //                                                      msg_header_t::MSG_SET_ARGSTREAM_SIZE, i,
        //                                                      world_impl.rank(), 1);
        /* pack the key */
        // pos = pack(key, msg->bytes, pos);
        // pos = pack(size, msg->bytes, pos);
        // detail::send_active_message(owner, msg.get(), sizeof(msg_header_t) + pos);
      } else {
        ttg::trace(world.rank(), ":", get_name(), ":", key, " : setting stream size to ", size, " for terminal ", i);

        task_t *task;
        //TODO: Verify if we need to put fetch_add/sub here
        this->tasks_table->starpu_hash_table_try_emplace_and_visit(key, [&](){
          task = create_new_task(key);
          world.impl().increment_created();
          return task;
        }, [&](auto& item){
          task = item;
        });

        // TODO: Unfriendly implementation, cannot check if stream is already bounded
        // TODO: Unfriendly implementation, cannot check if stream has been finalized already

        // commit changes
        // 1) "lock" the stream by incrementing the reduce_count
        // 2) set the goal
        // 3) "unlock" the stream
        // only one thread will see the reduce_count be zero and the goal match the size
        task->streams[i].reduce_count.fetch_add(1, std::memory_order_acquire);
        task->streams[i].goal = size;
        auto c = task->streams[i].reduce_count.fetch_sub(1, std::memory_order_release);
        if (1 == c && (task->streams[i].size >= size)) {
          release_task(task);
        }
      }
    }

    /// sets stream size for input \c i
    /// \param size positive integer that specifies the stream size
    template <std::size_t i, typename Key = keyT>
    std::enable_if_t<ttg::meta::is_void_v<Key>, void> set_argstream_size(std::size_t size) {
      // preconditions
      assert(std::get<i>(input_reducers) && "TT::set_argstream_size called on nonstreaming input terminal");
      assert(size > 0 && "TT::set_argstream_size(key,size) called with size=0");

      // body
      const auto owner = keymap();
      if (owner != world.rank()) {
        ttg::trace(world.rank(), ":", get_name(), " : forwarding stream size for terminal ", i);
        using msg_t = detail::msg_t;
        auto &world_impl = world.impl();
        uint64_t pos = 0;
        // std::unique_ptr<msg_t> msg = std::make_unique<msg_t>(get_instance_id(), world_impl.taskpool()->taskpool_id,
        //                                                      msg_header_t::MSG_SET_ARGSTREAM_SIZE, i,
        //                                                      world_impl.rank(), 0);
        // pos = pack(size, msg->bytes, pos);

        // detail::send_active_message(owner, msg.get(), sizeof(msg_header_t) + pos);
      } else {
        ttg::trace(world.rank(), ":", get_name(), " : setting stream size to ", size, " for terminal ", i);

        key_type hk = 0;
        task_t *task;
        this->tasks_table->starpu_hash_table_try_emplace_and_visit(hk, [&](){
          task = create_new_task(ttg::Void{});
          world.impl().increment_created();
          return task;
        }, [&](auto& item){
          task = item;
        });

        // TODO: Unfriendly implementation, cannot check if stream is already bounded
        // TODO: Unfriendly implementation, cannot check if stream has been finalized already

        // commit changes
        // 1) "lock" the stream by incrementing the reduce_count
        // 2) set the goal
        // 3) "unlock" the stream
        // only one thread will see the reduce_count be zero and the goal match the size
        task->streams[i].reduce_count.fetch_add(1, std::memory_order_acquire);
        task->streams[i].goal = size;
        auto c = task->streams[i].reduce_count.fetch_sub(1, std::memory_order_release);
        if (1 == c && (task->streams[i].size >= size)) {
          release_task(task);
        }
      }
    }

    /// finalizes stream for input \c i
    template <std::size_t i, typename Key>
    std::enable_if_t<!ttg::meta::is_void_v<Key>, void> finalize_argstream(const Key &key) {
      // preconditions
      assert(std::get<i>(input_reducers) && "TT::finalize_argstream called on nonstreaming input terminal");

      // body
      const auto owner = keymap(key);
      if (owner != world.rank()) {
        ttg::trace(world.rank(), ":", get_name(), " : ", key, ": forwarding stream finalize for terminal ", i);
        using msg_t = detail::msg_t;
        auto &world_impl = world.impl();
        uint64_t pos = 0;
        // std::unique_ptr<msg_t> msg = std::make_unique<msg_t>(get_instance_id(), world_impl.taskpool()->taskpool_id,
        //                                                      msg_header_t::MSG_FINALIZE_ARGSTREAM_SIZE, i,
        //                                                      world_impl.rank(), 1);
        // /* pack the key */
        // pos = pack(key, msg->bytes, pos);

        // detail::send_active_message(owner, msg.get(), sizeof(msg_header_t) + pos);
      } else {
        ttg::trace(world.rank(), ":", get_name(), " : ", key, ": finalizing stream for terminal ", i);
        task_t *task = nullptr;
        if(!this->tasks_table->starpu_hash_table_visit(key, [&](auto& item) {
          task = item;
        })){
          ttg::print_error(world.rank(), ":", get_name(), " : error finalize called on stream that never received an input data: ", i);
          throw std::runtime_error("TT::finalize called on stream that never received an input data");
        }

        // TODO: Unfriendly implementation, cannot check if stream is already bounded
        // TODO: Unfriendly implementation, cannot check if stream has been finalized already

        // commit changes
        // 1) "lock" the stream by incrementing the reduce_count
        // 2) set the goal
        // 3) "unlock" the stream
        // only one thread will see the reduce_count be zero and the goal match the size
        task->streams[i].reduce_count.fetch_add(1, std::memory_order_acquire);
        task->streams[i].goal = 1;
        auto c = task->streams[i].reduce_count.fetch_sub(1, std::memory_order_release);
        if (1 == c && (task->streams[i].size >= 1)) {
          release_task(task);
        }
      }
    }

    /// finalizes stream for input \c i
    template <std::size_t i, bool key_is_void = ttg::meta::is_void_v<keyT>>
    std::enable_if_t<key_is_void, void> finalize_argstream() {
      // preconditions
      assert(std::get<i>(input_reducers) && "TT::finalize_argstream called on nonstreaming input terminal");

      // body
      const auto owner = keymap();
      if (owner != world.rank()) {
        ttg::trace(world.rank(), ":", get_name(), ": forwarding stream finalize for terminal ", i);
        using msg_t = detail::msg_t;
        auto &world_impl = world.impl();
        uint64_t pos = 0;
        // std::unique_ptr<msg_t> msg = std::make_unique<msg_t>(get_instance_id(), world_impl.taskpool()->taskpool_id,
        //                                                      msg_header_t::MSG_FINALIZE_ARGSTREAM_SIZE, i,
        //                                                      world_impl.rank(), 0);

        // detail::send_active_message(owner, msg.get(), sizeof(msg_header_t) + pos);
      } else {
        ttg::trace(world.rank(), ":", get_name(), ": finalizing stream for terminal ", i);

        key_type hk = 0;
        task_t *task = nullptr;
        if (!this->tasks_table->starpu_hash_table_visit(hk, [&](auto& item) {
              task = item;
            })) {
          ttg::print_error(world.rank(), ":", get_name(),
                           " : error finalize called on stream that never received an input data: ", i);
          throw std::runtime_error("TT::finalize called on stream that never received an input data");
        }

        // TODO: Unfriendly implementation, cannot check if stream is already bounded
        // TODO: Unfriendly implementation, cannot check if stream has been finalized already

        // commit changes
        // 1) "lock" the stream by incrementing the reduce_count
        // 2) set the goal
        // 3) "unlock" the stream
        // only one thread will see the reduce_count be zero and the goal match the size
        task->streams[i].reduce_count.fetch_add(1, std::memory_order_acquire);
        task->streams[i].goal = 1;
        auto c = task->streams[i].reduce_count.fetch_sub(1, std::memory_order_release);
        if (1 == c && (task->streams[i].size >= 1)) {
          release_task(task);
        }
      }
    }

    template<typename Value>
    void copy_mark_pushout(const Value& value) {
      // auto check_parsec_data = [&](parsec_data_t* data) {
      //   if (data->owner_device != 0) {
      //     /* find the flow */
      //     int flowidx = 0;
      //     while (flowidx < MAX_PARAM_COUNT &&
      //           gpu_task->flow[flowidx] != nullptr &&
      //           gpu_task->flow[flowidx]->flow_flags != PARSEC_FLOW_ACCESS_NONE) {
      //       if (detail::starpu_ttg_caller->starpu_task.data[flowidx].data_in->original == data) {
      //         /* found the right data, set the corresponding flow as pushout */
      //         break;
      //       }
      //       ++flowidx;
      //     }
      //     if (flowidx == MAX_PARAM_COUNT) {
      //       throw std::runtime_error("Cannot add more than MAX_PARAM_COUNT flows to a task!");
      //     }
      //     if (gpu_task->flow[flowidx]->flow_flags == PARSEC_FLOW_ACCESS_NONE) {
      //       /* no flow found, add one and mark it pushout */
      //       detail::starpu_ttg_caller->starpu_task.data[flowidx].data_in = data->device_copies[0];
      //       detail::starpu_ttg_caller->starpu_task.data[flowidx].data_out = data->device_copies[data->owner_device];
      //       gpu_task->flow_nb_elts[flowidx] = data->nb_elts;
      //     }
      //     /* need to mark the flow WRITE, otherwise PaRSEC will not do the pushout */
      //     ((parsec_flow_t *)gpu_task->flow[flowidx])->flow_flags |= PARSEC_FLOW_ACCESS_WRITE;
      //     gpu_task->pushout |= 1<<flowidx;
      //   }
      // };
      // detail::foreach_parsec_data(value,
      //   [&](parsec_data_t* data){
      //     check_parsec_data(data);
      //   });
    }


    /* check whether a data needs to be pushed out */
    template <std::size_t i, typename Value, typename RemoteCheckFn>
    std::enable_if_t<!std::is_void_v<std::decay_t<Value>>,
                     void>
    do_prepare_send(const Value &value, RemoteCheckFn&& remote_check) {
      constexpr const bool value_is_const = std::is_const_v<std::tuple_element_t<i, input_args_type>>;

      /* get the copy */
      detail::ttg_data_copy_t *copy;
      copy = detail::find_copy_in_task(detail::starpu_ttg_caller, &value);

      /* if there is no copy we don't need to prepare anything */
      if (nullptr == copy) {
        return;
      }

      detail::starpu_ttg_task_base_t *caller = detail::starpu_ttg_caller;
      bool need_pushout = false;

      if (caller->data_flags & detail::ttg_starpu_data_flags::MARKED_PUSHOUT) {
        /* already marked pushout, skip the rest */
        return;
      }

      /* TODO: remove this once we support reductions on the GPU */
      auto &reducer = std::get<i>(input_reducers);
      if (reducer) {
        /* reductions are currently done only on the host so push out */
        copy_mark_pushout(value);
        caller->data_flags |= detail::ttg_starpu_data_flags::MARKED_PUSHOUT;
        return;
      }

      if constexpr (value_is_const) {
        if (caller->data_flags & detail::ttg_starpu_data_flags::IS_MODIFIED) {
          /* The data has been modified previously. If not all devices can access
           * their peers then we need to push out to the host so that all devices
           * have the data available for reading.
           * NOTE: we currently don't allow users to force the next writer to be
           *       on a different device. In that case PaRSEC would take the host-side
           *       copy. If we change our restriction we need to revisit this.
           *       Ideally, PaRSEC would take the device copy if the owner moves... */
          need_pushout = !detail::all_devices_peer_access;
        }

        /* check for multiple readers */
        if (caller->data_flags & detail::ttg_starpu_data_flags::SINGLE_READER) {
          caller->data_flags |= detail::ttg_starpu_data_flags::MULTIPLE_READER;
        }

        if (caller->data_flags & detail::ttg_starpu_data_flags::SINGLE_WRITER) {
          /* there is a writer already, we will need to create a copy */
          need_pushout = true;
        }

        caller->data_flags |= detail::ttg_starpu_data_flags::SINGLE_READER;
      } else {
        if (caller->data_flags & detail::ttg_starpu_data_flags::SINGLE_WRITER) {
          caller->data_flags |= detail::ttg_starpu_data_flags::MULTIPLE_WRITER;
          need_pushout = true;
        } else {
          if (caller->data_flags & detail::ttg_starpu_data_flags::SINGLE_READER) {
            /* there are readers, we will need to create a copy */
            need_pushout = true;
          }
          caller->data_flags |= detail::ttg_starpu_data_flags::SINGLE_WRITER;
        }
      }

      if (need_pushout) {
        copy_mark_pushout(value);
        caller->data_flags |= detail::ttg_starpu_data_flags::MARKED_PUSHOUT;
      }
    }

    /* check whether a data needs to be pushed out */
    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>,
                     void>
    prepare_send(const ttg::span<const Key> &keylist, const Value &value) {
      auto remote_check = [&](){
          auto world = ttg_default_execution_context();
          int rank = world.rank();
          bool remote = keylist.end() != std::find_if(keylist.begin(), keylist.end(),
                                                      [&](const Key &key) { return keymap(key) != rank; });
          return remote;
        };
      do_prepare_send<i>(value, remote_check);
    }

    template <std::size_t i, typename Key, typename Value>
    std::enable_if_t<ttg::meta::is_void_v<Key> && !std::is_void_v<std::decay_t<Value>>,
                     void>
    prepare_send(const Value &value) {
      auto remote_check = [&](){
          auto world = ttg_default_execution_context();
          int rank = world.rank();
          return (keymap() != rank);
        };
      do_prepare_send<i>(value, remote_check);
    }

   private:
    // Copy/assign/move forbidden ... we could make it work using
    // PIMPL for this base class.  However, this instance of the base
    // class is tied to a specific instance of a derived class a
    // pointer to which is captured for invoking derived class
    // functions.  Thus, not only does the derived class has to be
    // involved but we would have to do it in a thread safe way
    // including for possibly already running tasks and remote
    // references.  This is not worth the effort ... wherever you are
    // wanting to move/assign an TT you should be using a pointer.
    TT(const TT &other) = delete;
    TT &operator=(const TT &other) = delete;
    TT(TT &&other) = delete;
    TT &operator=(TT &&other) = delete;

    // Registers the callback for the i'th input terminal
    template <typename terminalT, std::size_t i>
    void register_input_callback(terminalT &input) {
      using valueT = std::decay_t<typename terminalT::value_type>;
      if (input.is_pull_terminal) {
        num_pullins++;
      }
      //////////////////////////////////////////////////////////////////
      // case 1: nonvoid key, nonvoid value
      //////////////////////////////////////////////////////////////////
      if constexpr (!ttg::meta::is_void_v<keyT> && !std::is_void_v<valueT>) {
        auto move_callback = [this](const keyT &key, valueT &&value) {
          set_arg<i, keyT, valueT>(key, std::forward<valueT>(value));
        };
        auto send_callback = [this](const keyT &key, const valueT &value) {
          set_arg<i, keyT, const valueT &>(key, value);
        };
        auto broadcast_callback = [this](const ttg::span<const keyT> &keylist, const valueT &value) {
          broadcast_arg<i, keyT, valueT>(keylist, value);
        };
        auto prepare_send_callback = [this](const ttg::span<const keyT> &keylist, const valueT &value) {
            prepare_send<i, keyT, valueT>(keylist, value);
        };
        auto setsize_callback = [this](const keyT &key, std::size_t size) { set_argstream_size<i>(key, size); };
        auto finalize_callback = [this](const keyT &key) { finalize_argstream<i>(key); };
        input.set_callback(send_callback, move_callback, broadcast_callback,
                           setsize_callback, finalize_callback, prepare_send_callback);
      }
      //////////////////////////////////////////////////////////////////
      // case 2: nonvoid key, void value, mixed inputs
      //////////////////////////////////////////////////////////////////
      else if constexpr (!ttg::meta::is_void_v<keyT> && std::is_void_v<valueT>) {
        auto send_callback = [this](const keyT &key) { set_arg<i, keyT, ttg::Void>(key, ttg::Void{}); };
        auto setsize_callback = [this](const keyT &key, std::size_t size) { set_argstream_size<i>(key, size); };
        auto finalize_callback = [this](const keyT &key) { finalize_argstream<i>(key); };
        input.set_callback(send_callback, send_callback, {}, setsize_callback, finalize_callback);
      }
      //////////////////////////////////////////////////////////////////
      // case 3: nonvoid key, void value, no inputs
      // NOTE: subsumed in case 2 above, kept for historical reasons
      //////////////////////////////////////////////////////////////////
      //////////////////////////////////////////////////////////////////
      // case 4: void key, nonvoid value
      //////////////////////////////////////////////////////////////////
      else if constexpr (ttg::meta::is_void_v<keyT> && !std::is_void_v<valueT>) {
        auto move_callback = [this](valueT &&value) { set_arg<i, keyT, valueT>(std::forward<valueT>(value)); };
        auto send_callback = [this](const valueT &value) {
          if constexpr (std::is_copy_constructible_v<valueT>) {
            set_arg<i, keyT, const valueT &>(value);
          }
          else {
            throw std::logic_error(std::string("TTG::StarPU: send_callback is invoked on datum of type ") + typeid(std::decay_t<valueT>).name() + " which is not copy constructible, std::move datum into send/broadcast statement");
          }
        };
        auto setsize_callback = [this](std::size_t size) { set_argstream_size<i>(size); };
        auto finalize_callback = [this]() { finalize_argstream<i>(); };
        auto prepare_send_callback = [this](const valueT &value) {
            prepare_send<i, void>(value);
        };
        input.set_callback(send_callback, move_callback, {}, setsize_callback, finalize_callback, prepare_send_callback);
      }
      //////////////////////////////////////////////////////////////////
      // case 5: void key, void value, mixed inputs
      //////////////////////////////////////////////////////////////////
      else if constexpr (ttg::meta::is_void_v<keyT> && std::is_void_v<valueT>) {
        auto send_callback = [this]() { set_arg<i, keyT, ttg::Void>(ttg::Void{}); };
        auto setsize_callback = [this](std::size_t size) { set_argstream_size<i>(size); };
        auto finalize_callback = [this]() { finalize_argstream<i>(); };
        input.set_callback(send_callback, send_callback, {}, setsize_callback, finalize_callback);
      }
      //////////////////////////////////////////////////////////////////
      // case 6: void key, void value, no inputs
      // NOTE: subsumed in case 5 above, kept for historical reasons
      //////////////////////////////////////////////////////////////////
      else
        ttg::abort();
    }

    template <std::size_t... IS>
    void register_input_callbacks(std::index_sequence<IS...>) {
      int junk[] = {
          0,
          (register_input_callback<std::tuple_element_t<IS, input_terminals_type>, IS>(std::get<IS>(input_terminals)),
           0)...};
      junk[0]++;
    }

    template <std::size_t... IS, typename inedgesT>
    void connect_my_inputs_to_incoming_edge_outputs(std::index_sequence<IS...>, inedgesT &inedges) {
      int junk[] = {0, (std::get<IS>(inedges).set_out(&std::get<IS>(input_terminals)), 0)...};
      junk[0]++;
    }

    template <std::size_t... IS, typename outedgesT>
    void connect_my_outputs_to_outgoing_edge_inputs(std::index_sequence<IS...>, outedgesT &outedges) {
      int junk[] = {0, (std::get<IS>(outedges).set_in(&std::get<IS>(output_terminals)), 0)...};
      junk[0]++;
    }

#if 0
    template <typename input_terminals_tupleT, std::size_t... IS, typename flowsT>
    void _initialize_flows(std::index_sequence<IS...>, flowsT &&flows) {
      int junk[] = {0,
                    (*(const_cast<std::remove_const_t<decltype(flows[IS]->flow_flags)> *>(&(flows[IS]->flow_flags))) =
                         (std::is_const_v<std::tuple_element_t<IS, input_terminals_tupleT>> ? PARSEC_FLOW_ACCESS_READ
                                                                                            : PARSEC_FLOW_ACCESS_RW),
                     0)...};
      junk[0]++;
    }

    template <typename input_terminals_tupleT, typename flowsT>
    void initialize_flows(flowsT &&flows) {
      _initialize_flows<input_terminals_tupleT>(
          std::make_index_sequence<std::tuple_size<input_terminals_tupleT>::value>{}, flows);
    }
#endif // 0

    void fence() override { ttg::default_execution_context().impl().fence(); }

    static int key_equal(key_type a, key_type b, void *user_data) {
      if constexpr (std::is_same_v<keyT, void>) {
        return 1;
      } else {
        keyT &ka = *(reinterpret_cast<keyT *>(a));
        keyT &kb = *(reinterpret_cast<keyT *>(b));
        return ka == kb;
      }
    }

    static uint64_t key_hash(key_type k, void *user_data) {
      constexpr const bool keyT_is_Void = ttg::meta::is_void_v<keyT>;
      if constexpr (keyT_is_Void || std::is_same_v<keyT, void>) {
        return 0;
      } else {
        keyT &kk = *(reinterpret_cast<keyT *>(k));
        using ttg::hash;
        uint64_t hv = hash<std::decay_t<decltype(kk)>>{}(kk);
        return hv;
      }
    }

    static char *key_print(char *buffer, size_t buffer_size, key_type k, void *user_data) {
      if constexpr (std::is_same_v<keyT, void>) {
        buffer[0] = '\0';
        return buffer;
      } else {
        keyT kk = *(reinterpret_cast<keyT *>(k));
        std::stringstream iss;
        iss << kk;
        memset(buffer, 0, buffer_size);
        iss.get(buffer, buffer_size);
        return buffer;
      }
    }

    // static starpu_key_t make_key(const parsec_taskpool_t *tp, const parsec_assignment_t *as) {
    //     // we use the parsec_assignment_t array as a scratchpad to store the hash and address of the key
    //     keyT *key = *(keyT**)&(as[2]);
    //     return reinterpret_cast<starpu_key_t>(key);
    // }

    static char *starpu_ttg_task_snprintf(char *buffer, size_t buffer_size, const starpu_task_t *starpu_task) {
      if(buffer_size == 0)
        return buffer;

      if constexpr (ttg::meta::is_void_v<keyT>) {
        snprintf(buffer, buffer_size, "%s()[]<%d>", starpu_task->name, starpu_task->priority);
      }  else {
        const task_t *task = reinterpret_cast<const task_t*>(starpu_task);
        std::stringstream ss;
        ss << task->key;

        std::string keystr = ss.str();
        std::replace(keystr.begin(), keystr.end(), '(', ':');
        std::replace(keystr.begin(), keystr.end(), ')', ':');

        snprintf(buffer, buffer_size, "%s(%s)[]<%d>", starpu_task->name, keystr.c_str(), starpu_task->priority);
      }
      return buffer;
    }



    //parsec_key_fn_t tasks_hash_fcts = {key_equal, key_print, key_hash};

    static starpu_hook_return_t complete_task_and_release(void *es, starpu_task_t *starpu_task) {

      //std::cout << "complete_task_and_release: task " << parsec_task << std::endl;

      task_t *task = (task_t*)starpu_task;

#ifdef TTG_HAVE_COROUTINE
      /* if we still have a coroutine handle we invoke it one more time to get the sends/broadcasts */
      if (task->suspended_task_address) {
        assert(task->coroutine_id != ttg::TaskCoroutineID::Invalid);
        /* the coroutine should have completed and we cannot access the promise anymore */
        task->suspended_task_address = nullptr;
      }
#endif // TTG_HAVE_COROUTINE

      /* release our data copies */
      for (int i = 0; i < task->data_count; i++) {
        detail::ttg_data_copy_t *copy = task->copies[i];
        if (nullptr == copy) continue;
        detail::release_data_copy(copy);
        task->copies[i] = nullptr;
      }

      for (auto& c : task->tt->constraints_complete) {
        if constexpr(std::is_void_v<keyT>) {
          c();
        } else {
          c(task->key);
        }
      }
      return 0;//PA_HOOK_RETURN_DONE
    }

   public:
    template <typename keymapT = ttg::detail::default_keymap<keyT>,
              typename priomapT = ttg::detail::default_priomap<keyT>>
    TT(const std::string &name, const std::vector<std::string> &innames, const std::vector<std::string> &outnames,
       ttg::World world, keymapT &&keymap_ = keymapT(), priomapT &&priomap_ = priomapT())
        : ttg::TTBase(name, numinedges, numouts)
        , world(world)
        // if using default keymap, rebind to the given world
        , keymap(std::is_same<keymapT, ttg::detail::default_keymap<keyT>>::value
                     ? decltype(keymap)(ttg::detail::default_keymap<keyT>(world))
                     : decltype(keymap)(std::forward<keymapT>(keymap_)))
        , priomap(decltype(keymap)(std::forward<priomapT>(priomap_))) {
      // Cannot call these in base constructor since terminals not yet constructed
      if (innames.size() != numinedges) throw std::logic_error("ttg_starpu::TT: #input names != #input terminals");
      if (outnames.size() != numouts) throw std::logic_error("ttg_starpu::TT: #output names != #output terminals");

      auto &world_impl = world.impl();
      world_impl.register_op(this);

      if constexpr (numinedges == numins) {
        register_input_terminals(input_terminals, innames);
      } else {
        // create a name for the virtual control input
        register_input_terminals(input_terminals, std::array<std::string, 1>{std::string("Virtual Control")});
      }
      register_output_terminals(output_terminals, outnames);

      register_input_callbacks(std::make_index_sequence<numinedges>{});
      int i;



      if( world_impl.profiling() ) {

      }

      // self.release_task = &parsec_release_task_to_mempool_update_nbtasks;
      // self.complete_execution = complete_task_and_release;


      for (i = 0; i < MAX_PARAM_COUNT; i++) {
        // parsec_flow_t *flow = new parsec_flow_t;
        // flow->name = strdup((std::string("flow out") + std::to_string(i)).c_str());
        // flow->sym_type = PARSEC_SYM_INOUT;
        // flow->flow_flags = PARSEC_FLOW_ACCESS_READ;  // does PaRSEC use this???
        // flow->dep_in[0] = NULL;
        // flow->dep_out[0] = NULL;
        // flow->flow_index = i;
        // flow->flow_datatype_mask = (1 << i);
        // *((parsec_flow_t **)&(self.out[i])) = flow;
      }

      int nbthreads = 0;
      //TODO: May we use starpu_get_num_threads()
      // auto *context = world_impl.context();
      // for (int i = 0; i < context->nb_vp; i++) {
      //   nbthreads += context->virtual_processes[i]->nb_cores;
      // }

      // parsec_mempool_construct(&mempools, PARSEC_OBJ_CLASS(parsec_task_t), sizeof(task_t),
      //                          offsetof(parsec_task_t, mempool_owner), nbthreads);

      // parsec_hash_table_init(&tasks_table, offsetof(detail::parsec_ttg_task_base_t, tt_ht_item), 8, tasks_hash_fcts,
      //                        NULL);

      // parsec_hash_table_init(&task_constraint_table, offsetof(detail::parsec_ttg_task_base_t, tt_ht_item), 8, tasks_hash_fcts,
      //                        NULL);
    }

    template <typename keymapT = ttg::detail::default_keymap<keyT>,
              typename priomapT = ttg::detail::default_priomap<keyT>>
    TT(const std::string &name, const std::vector<std::string> &innames, const std::vector<std::string> &outnames,
       keymapT &&keymap = keymapT(ttg::default_execution_context()), priomapT &&priomap = priomapT())
        : TT(name, innames, outnames, ttg::default_execution_context(), std::forward<keymapT>(keymap),
             std::forward<priomapT>(priomap)) {}

    template <typename keymapT = ttg::detail::default_keymap<keyT>,
              typename priomapT = ttg::detail::default_priomap<keyT>>
    TT(const input_edges_type &inedges, const output_edges_type &outedges, const std::string &name,
       const std::vector<std::string> &innames, const std::vector<std::string> &outnames, ttg::World world,
       keymapT &&keymap_ = keymapT(), priomapT &&priomap = priomapT())
        : TT(name, innames, outnames, world, std::forward<keymapT>(keymap_), std::forward<priomapT>(priomap)) {
      connect_my_inputs_to_incoming_edge_outputs(std::make_index_sequence<numinedges>{}, inedges);
      connect_my_outputs_to_outgoing_edge_inputs(std::make_index_sequence<numouts>{}, outedges);
      //DO NOT MOVE THIS - information about the number of pull terminals is only available after connecting the edges.
      if constexpr (numinedges > 0) {
        register_input_callbacks(std::make_index_sequence<numinedges>{});
      }
    }
    template <typename keymapT = ttg::detail::default_keymap<keyT>,
              typename priomapT = ttg::detail::default_priomap<keyT>>
    TT(const input_edges_type &inedges, const output_edges_type &outedges, const std::string &name,
       const std::vector<std::string> &innames, const std::vector<std::string> &outnames,
       keymapT &&keymap = keymapT(ttg::default_execution_context()), priomapT &&priomap = priomapT())
        : TT(inedges, outedges, name, innames, outnames, ttg::default_execution_context(),
             std::forward<keymapT>(keymap), std::forward<priomapT>(priomap)) {}

    // Destructor checks for unexecuted tasks
    virtual ~TT() {
      // if(nullptr != self.name ) {
      //   free((void*)self.name);
      //   self.name = nullptr;
      // }

      for (std::size_t i = 0; i < numins; ++i) {
        // if (inpute_reducers_taskclass[i] != nullptr) {
        //   std::free(inpute_reducers_taskclass[i]);
        //   inpute_reducers_taskclass[i] = nullptr;
        // }
      }
      release();
    }

    static void ht_iter_cb(void *item, void *cb_data) {
      task_t *task = (task_t *)item;
      ttT *op = (ttT *)cb_data;
      if constexpr (!ttg::meta::is_void_v<keyT>) {
        std::cout << "Left over task " << op->get_name() << " " << task->key << std::endl;
      } else {
        std::cout << "Left over task " << op->get_name() << std::endl;
      }
    }

    virtual void print_incomplete_tasks() const override {
      this->tasks_table->starpu_hash_table_visit_all(ht_iter_cb, (void *)this);
    }

    virtual void release() override { do_release(); }

    void do_release() {
      if (!alive) {
        return;
      }
      alive = false;
      /* print all outstanding tasks */
      print_incomplete_tasks();
      // parsec_hash_table_fini(&tasks_table);
      // parsec_mempool_destruct(&mempools);
      // uintptr_t addr = (uintptr_t)self.incarnations;
      // free((void *)addr);
      // free((__parsec_chore_t *)self.incarnations);
      for (int i = 0; i < MAX_PARAM_COUNT; i++) {
        // if (NULL != self.in[i]) {
        //   free(self.in[i]->name);
        //   delete self.in[i];
        //   self.in[i] = nullptr;
        // }
        // if (NULL != self.out[i]) {
        //   free(self.out[i]->name);
        //   delete self.out[i];
        //   self.out[i] = nullptr;
        // }
      }
      world.impl().deregister_op(this);
    }

    static constexpr const ttg::Runtime runtime = ttg::Runtime::StarPU;

    /// define the reducer function to be called when additional inputs are
    /// received on a streaming terminal
    ///   @tparam <i> the index of the input terminal that is used as a streaming terminal
    ///   @param[in] reducer: a function of prototype `void(input_type<i> &a, const input_type<i> &b)`
    ///                       that function should aggregate b into a
    template <std::size_t i, typename Reducer>
    void set_input_reducer(Reducer &&reducer) {
      ttg::trace(world.rank(), ":", get_name(), " : setting reducer for terminal ", i);
      std::get<i>(input_reducers) = reducer;

      // parsec_task_class_t *tc = inpute_reducers_taskclass[i];
      // if (nullptr == tc) {
      //   tc = (parsec_task_class_t *)std::calloc(1, sizeof(*tc));
      //   inpute_reducers_taskclass[i] = tc;

      //   tc->name = strdup((get_name() + std::string(" reducer ") + std::to_string(i)).c_str());
      //   tc->task_class_id = get_instance_id();
      //   tc->nb_parameters = 0;
      //   tc->nb_locals = 0;
      //   tc->nb_flows = numflows;

      //   auto &world_impl = world.impl();

      //   if( world_impl.profiling() ) {
      //     // first two ints are used to store the hash of the key.
      //     tc->nb_parameters = (sizeof(void*)+sizeof(int)-1)/sizeof(int);
      //     // seconds two ints are used to store a pointer to the key of the task.
      //     tc->nb_locals     = self.nb_parameters + (sizeof(void*)+sizeof(int)-1)/sizeof(int);

      //     // If we have parameters and locals, we need to define the corresponding dereference arrays
      //     // tc->params[0] = &detail::parsec_taskclass_param0;
      //     // tc->params[1] = &detail::parsec_taskclass_param1;

      //     // tc->locals[0] = &detail::parsec_taskclass_param0;
      //     // tc->locals[1] = &detail::parsec_taskclass_param1;
      //     // tc->locals[2] = &detail::parsec_taskclass_param2;
      //     // tc->locals[3] = &detail::parsec_taskclass_param3;
      //   }
      //   tc->make_key = make_key;
      //   tc->key_functions = &tasks_hash_fcts;
      //   tc->task_snprintf = parsec_ttg_task_snprintf;


        
      //   tc->incarnations = (__parsec_chore_t *)malloc(2 * sizeof(__parsec_chore_t));
      //   ((__parsec_chore_t *)tc->incarnations)[0].type = PARSEC_DEV_CPU;
      //   ((__parsec_chore_t *)tc->incarnations)[0].evaluate = NULL;
      //   ((__parsec_chore_t *)tc->incarnations)[0].hook = &static_reducer_op<i>;
      //   ((__parsec_chore_t *)tc->incarnations)[1].type = PARSEC_DEV_NONE;
      //   ((__parsec_chore_t *)tc->incarnations)[1].evaluate = NULL;
      //   ((__parsec_chore_t *)tc->incarnations)[1].hook = NULL;
        
      //   /* the reduction task does not alter the termination detection because the target task will execute */
      //   tc->release_task = &parsec_release_task_to_mempool;
      //   tc->complete_execution = NULL;
      // }
    }

    /// define the reducer function to be called when additional inputs are
    /// received on a streaming terminal
    ///   @tparam <i> the index of the input terminal that is used as a streaming terminal
    ///   @param[in] reducer: a function of prototype `void(input_type<i> &a, const input_type<i> &b)`
    ///                       that function should aggregate b into a
    ///   @param[in] size: the default number of inputs that are received in this streaming terminal,
    ///                    for each task
    template <std::size_t i, typename Reducer>
    void set_input_reducer(Reducer &&reducer, std::size_t size) {
      set_input_reducer<i>(std::forward<Reducer>(reducer));
      set_static_argstream_size<i>(size);
    }

    // Returns reference to input terminal i to facilitate connection --- terminal
    // cannot be copied, moved or assigned
    template <std::size_t i>
    std::tuple_element_t<i, input_terminals_type> *in() {
      return &std::get<i>(input_terminals);
    }

    // Returns reference to output terminal for purpose of connection --- terminal
    // cannot be copied, moved or assigned
    template <std::size_t i>
    std::tuple_element_t<i, output_terminalsT> *out() {
      return &std::get<i>(output_terminals);
    }

    // Manual injection of a task with all input arguments specified as a tuple
    template <typename Key = keyT>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !ttg::meta::is_empty_tuple_v<input_values_tuple_type>, void> invoke(
        const Key &key, const input_values_tuple_type &args) {
      TTG_OP_ASSERT_EXECUTABLE();
      if constexpr(!std::is_same_v<Key, key_type>) {
        key_type k = key; /* cast that type into the key type we know */
        invoke(k, args);
      } else {
        /* trigger non-void inputs */
        set_args(ttg::meta::nonvoid_index_seq<actual_input_tuple_type>{}, key, args);
        /* trigger void inputs */
        using void_index_seq = ttg::meta::void_index_seq<actual_input_tuple_type>;
        set_args(void_index_seq{}, key, ttg::detail::make_void_tuple<void_index_seq::size()>());
      }
    }

    // Manual injection of a key-free task and all input arguments specified as a tuple
    template <typename Key = keyT>
    std::enable_if_t<ttg::meta::is_void_v<Key> && !ttg::meta::is_empty_tuple_v<input_values_tuple_type>, void> invoke(
        const input_values_tuple_type &args) {
      TTG_OP_ASSERT_EXECUTABLE();
      /* trigger non-void inputs */
      set_args(ttg::meta::nonvoid_index_seq<actual_input_tuple_type>{}, args);
      /* trigger void inputs */
      using void_index_seq = ttg::meta::void_index_seq<actual_input_tuple_type>;
      set_args(void_index_seq{}, ttg::detail::make_void_tuple<void_index_seq::size()>());
    }

    // Manual injection of a task that has no arguments
    template <typename Key = keyT>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && ttg::meta::is_empty_tuple_v<input_values_tuple_type>, void> invoke(
        const Key &key) {
      TTG_OP_ASSERT_EXECUTABLE();

      if constexpr(!std::is_same_v<Key, key_type>) {
        key_type k = key; /* cast that type into the key type we know */
        invoke(k);
      } else {
        /* trigger void inputs */
        using void_index_seq = ttg::meta::void_index_seq<actual_input_tuple_type>;
        set_args(void_index_seq{}, key, ttg::detail::make_void_tuple<void_index_seq::size()>());
      }
    }

    // Manual injection of a task that has no key or arguments
    template <typename Key = keyT>
    std::enable_if_t<ttg::meta::is_void_v<Key> && ttg::meta::is_empty_tuple_v<input_values_tuple_type>, void> invoke() {
      TTG_OP_ASSERT_EXECUTABLE();
      /* trigger void inputs */
      using void_index_seq = ttg::meta::void_index_seq<actual_input_tuple_type>;
      set_args(void_index_seq{}, ttg::detail::make_void_tuple<void_index_seq::size()>());
    }

    // overrides TTBase::invoke()
    void invoke() override {
      if constexpr (ttg::meta::is_void_v<keyT> && ttg::meta::is_empty_tuple_v<input_values_tuple_type>)
        invoke<keyT>();
      else
        TTBase::invoke();
    }

  private:
    template<typename Key, typename Arg, typename... Args, std::size_t I, std::size_t... Is>
    void invoke_arglist(std::index_sequence<I, Is...>, const Key& key, Arg&& arg, Args&&... args) {
      using arg_type = std::decay_t<Arg>;
      if constexpr (ttg::meta::is_ptr_v<arg_type>) {
        /* add a reference to the object */
        auto copy = ttg_starpu::detail::get_copy(arg);
        copy->add_ref();
        /* reset readers so that the value can flow without copying */
        copy->reset_readers();
        auto& val = *arg;
        set_arg_impl<I>(key, val, copy);
        ttg_starpu::detail::release_data_copy(copy);
        if constexpr (std::is_rvalue_reference_v<Arg>) {
          /* if the ptr was moved in we reset it */
          arg.reset();
        }
      } else if constexpr (!ttg::meta::is_ptr_v<arg_type>) {
        set_arg<I>(key, std::forward<Arg>(arg));
      }
      if constexpr (sizeof...(Is) > 0) {
        /* recursive next argument */
        invoke_arglist(std::index_sequence<Is...>{}, key, std::forward<Args>(args)...);
      }
    }

  public:
    // Manual injection of a task with all input arguments specified as variadic arguments
    template <typename Key = keyT, typename Arg, typename... Args>
    std::enable_if_t<!ttg::meta::is_void_v<Key> && !ttg::meta::is_empty_tuple_v<input_values_tuple_type>, void> invoke(
        const Key &key, Arg&& arg, Args&&... args) {
      static_assert(sizeof...(Args)+1 == std::tuple_size_v<actual_input_tuple_type>,
                    "Number of arguments to invoke must match the number of task inputs.");
      TTG_OP_ASSERT_EXECUTABLE();
      /* trigger non-void inputs */
      invoke_arglist(ttg::meta::nonvoid_index_seq<actual_input_tuple_type>{}, key,
                     std::forward<Arg>(arg), std::forward<Args>(args)...);
      //set_args(ttg::meta::nonvoid_index_seq<actual_input_tuple_type>{}, key, args);
      /* trigger void inputs */
      using void_index_seq = ttg::meta::void_index_seq<actual_input_tuple_type>;
      set_args(void_index_seq{}, key, ttg::detail::make_void_tuple<void_index_seq::size()>());
    }

    void set_defer_writer(bool value) {
      m_defer_writer = value;
    }

    bool get_defer_writer(bool value) {
      return m_defer_writer;
    }

   public:
    void make_executable() override {
      world.impl().register_tt_profiling(this);
      register_static_op_function();
      ::ttg::TTBase::make_executable();
    }

    /// keymap accessor
    /// @return the keymap
    const decltype(keymap) &get_keymap() const { return keymap; }

    /// keymap setter
    template <typename Keymap>
    void set_keymap(Keymap &&km) {
      keymap = km;
    }

    /// priority map accessor
    /// @return the priority map
    const decltype(priomap) &get_priomap() const { return priomap; }

    /// priomap setter
    /// @arg pm a function that maps a key to an integral priority value.
    template <typename Priomap>
    void set_priomap(Priomap &&pm) {
      priomap = std::forward<Priomap>(pm);
    }

    /// device map setter
    /// The device map provides a hint on which device a task should execute.
    /// TTG may not be able to honor the request and the corresponding task
    /// may execute on a different device.
    /// @arg pm a function that provides a hint on which device the task should execute.
    template<typename Devicemap>
    void set_devicemap(Devicemap&& dm) {
    }

    /// device map accessor
    /// @return the device map
    auto get_devicemap() { return devicemap; }

    /// add a shared constraint
    /// the constraint must provide a valid override of `check_key(key)`
    template<typename Constraint>
    void add_constraint(std::shared_ptr<Constraint> c) {
      std::size_t cid = constraints_check.size();
      if constexpr(ttg::meta::is_void_v<keyT>) {
        c->add_listener([this, cid](){ this->release_constraint(cid); }, this);
        constraints_check.push_back([c, this](){ return c->check(this); });
        constraints_complete.push_back([c, this](const keyT& key){ c->complete(this); return true; });
      } else {
        c->add_listener([this, cid](const std::span<keyT>& keys){ this->release_constraint(cid, keys); }, this);
        constraints_check.push_back([c, this](const keyT& key){ return c->check(key, this); });
        constraints_complete.push_back([c, this](const keyT& key){ c->complete(key, this); return true; });
      }
    }

    /// add a constraint
    /// the constraint must provide a valid override of `check_key(key)`
    template<typename Constraint>
    void add_constraint(Constraint&& c) {
      // need to make this a shared_ptr since it's shared between different callbacks
      this->add_constraint(std::make_shared<Constraint>(std::forward<Constraint>(c)));
    }

    /// add a shared constraint
    /// the constraint must provide a valid override of `check_key(key, map(key))`
    /// ths overload can be used to provide different key mapping functions for each TT
    template<typename Constraint, typename Mapper>
    void add_constraint(std::shared_ptr<Constraint> c, Mapper&& map) {
      static_assert(std::is_same_v<typename Constraint::key_type, keyT>);
      std::size_t cid = constraints_check.size();
      if constexpr(ttg::meta::is_void_v<keyT>) {
        c->add_listener([this, cid](){ this->release_constraint(cid); }, this);
        constraints_check.push_back([map, c, this](){ return c->check(map(), this); });
        constraints_complete.push_back([map, c, this](){ c->complete(map(), this); return true; });
      } else {
        c->add_listener([this, cid](const std::span<keyT>& keys){ this->release_constraint(cid, keys); }, this);
        constraints_check.push_back([map, c, this](const keyT& key){ return c->check(key, map(key), this); });
        constraints_complete.push_back([map, c, this](const keyT& key){ c->complete(key, map(key), this); return true; });
      }
    }

    /// add a shared constraint
    /// the constraint must provide a valid override of `check_key(key, map(key))`
    /// ths overload can be used to provide different key mapping functions for each TT
    template<typename Constraint, typename Mapper>
    void add_constraint(Constraint c, Mapper&& map) {
      // need to make this a shared_ptr since it's shared between different callbacks
      this->add_constraint(std::make_shared<Constraint>(std::forward<Constraint>(c)), std::forward<Mapper>(map));
    }

    // Register the static_op function to associate it to instance_id
    void register_static_op_function(void) {
    //   int rank;
    //   MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    //   ttg::trace("ttg_starpu(", rank, ") Inserting into static_id_to_op_map at ", get_instance_id());
      // static_set_arg_fct_call_t call = std::make_pair(&TT::static_set_arg, this);
      // auto &world_impl = world.impl();
      // static_map_mutex.lock();
      // static_id_to_op_map.insert(std::make_pair(get_instance_id(), call));
      // if (delayed_unpack_actions.count(get_instance_id()) > 0) {
      //   auto tp = world_impl.taskpool();

      //   // ttg::trace("ttg_starpu(", rank, ") There are ", delayed_unpack_actions.count(get_instance_id()),
      //   //            " messages delayed with op_id ", get_instance_id());

      //   auto se = delayed_unpack_actions.equal_range(get_instance_id());
      //   std::vector<static_set_arg_fct_arg_t> tmp;
      //   for (auto it = se.first; it != se.second;) {
      //     assert(it->first == get_instance_id());
      //     tmp.push_back(std::move(it->second));
      //     it = delayed_unpack_actions.erase(it);
      //   }
      //   static_map_mutex.unlock();

      //   // for (auto& it : tmp) {
      //   //   if(ttg::tracing())
      //   //     ttg::print("ttg_starpu(", rank, ") Unpacking delayed message (", ", ", get_instance_id(), ", ",
      //   //                std::get<1>(it).get(), ", ", std::get<2>(it), ")");
      //   //   // int rc = detail::static_unpack_msg(&parsec_ce, world_impl.parsec_ttg_tag(), std::get<1>(it).get(), std::get<2>(it),
      //   //   //                                    std::get<0>(it), NULL);
      //   //   // assert(rc == 0);
      //   // }

      //   tmp.clear();
      // } else {
      //   static_map_mutex.unlock();
      // }
    }
  };

#include "ttg/make_tt.h"

}  // namespace ttg_starpu

/**
 * The PaRSEC backend tracks data copies so we make a copy of the data
 * if the data is not being tracked yet or if the data is not const, i.e.,
 * the user may mutate the data after it was passed to send/broadcast.
 */
template <>
struct ttg::detail::value_copy_handler<ttg::Runtime::StarPU> {
 private:
  ttg_starpu::detail::ttg_data_copy_t *copy_to_remove = nullptr;
  bool do_release = true;

 public:
  value_copy_handler() = default;
  value_copy_handler(const value_copy_handler& h) = delete;
  value_copy_handler(value_copy_handler&& h)
  : copy_to_remove(h.copy_to_remove)
  {
    h.copy_to_remove = nullptr;
  }

  value_copy_handler& operator=(const value_copy_handler& h) = delete;
  value_copy_handler& operator=(value_copy_handler&& h)
  {
    std::swap(copy_to_remove, h.copy_to_remove);
    return *this;
  }

  ~value_copy_handler() {
    if (nullptr != copy_to_remove) {
      ttg_starpu::detail::remove_data_copy(copy_to_remove, ttg_starpu::detail::starpu_ttg_caller);
      if (do_release) {
        ttg_starpu::detail::release_data_copy(copy_to_remove);
      }
    }
  }

  template <typename Value>
  inline std::conditional_t<std::is_reference_v<Value>,Value,Value&&> operator()(Value &&value) {
    constexpr auto value_is_rvref = std::is_rvalue_reference_v<decltype(value)>;
    using value_type = std::remove_reference_t<Value>;
    static_assert(value_is_rvref ||
                  std::is_copy_constructible_v<std::decay_t<Value>>,
                  "Data sent without being moved must be copy-constructible!");

    auto caller = ttg_starpu::detail::starpu_ttg_caller;
    if (nullptr == caller) {
      throw std::runtime_error("ERROR: ttg::send or ttg::broadcast called outside of a task!");
    }

    ttg_starpu::detail::ttg_data_copy_t *copy;
    copy = ttg_starpu::detail::find_copy_in_task(caller, &value);
    value_type *value_ptr = &value;
    if (nullptr == copy) {
      /**
       * the value is not known, create a copy that we can track
       * depending on Value, this uses either the copy or move constructor
       */
      copy = ttg_starpu::detail::create_new_datacopy(std::forward<Value>(value));
      bool inserted = ttg_starpu::detail::add_copy_to_task(copy, caller);
      assert(inserted);
      value_ptr = reinterpret_cast<value_type *>(copy->get_ptr());
      copy_to_remove = copy;
    } else {
      if constexpr (value_is_rvref) {
        /* this copy won't be modified anymore so mark it as read-only */
        copy->reset_readers();
      }
    }
    /* We're coming from a writer so mark the data as modified.
     * That way we can force a pushout in prepare_send if we move to read-only tasks (needed by PaRSEC). */
    caller->data_flags = ttg_starpu::detail::ttg_starpu_data_flags::IS_MODIFIED;
    if constexpr (value_is_rvref)
      return std::move(*value_ptr);
    else
      return *value_ptr;
  }

  template<typename Value>
  inline std::add_lvalue_reference_t<Value> operator()(ttg_starpu::detail::persistent_value_ref<Value> vref) {
    auto caller = ttg_starpu::detail::starpu_ttg_caller;
    if (nullptr == caller) {
      throw std::runtime_error("ERROR: ttg::send or ttg::broadcast called outside of a task!");
    }
    ttg_starpu::detail::ttg_data_copy_t *copy;
    copy = ttg_starpu::detail::find_copy_in_task(caller, &vref.value_ref);
    if (nullptr == copy) {
      // no need to create a new copy since it's derived from the copy already
      copy = const_cast<ttg_starpu::detail::ttg_data_copy_t *>(static_cast<const ttg_starpu::detail::ttg_data_copy_t *>(&vref.value_ref));
      bool inserted = ttg_starpu::detail::add_copy_to_task(copy, caller);
      assert(inserted);
      copy_to_remove = copy; // we want to remove the copy from the task once done sending
      do_release = true; // we don't release the copy since we didn't allocate it
      copy->add_ref(); // add a reference so that TTG does not attempt to delete this object
      copy->add_ref(); // add another reference so that TTG never attempts to free this copy
      if (copy->num_readers() == 0) {
        /* add at least one reader (the current task) */
        copy->increment_readers<false>();
      }
    }
    return vref.value_ref;
  }

  template <typename Value>
  inline const Value &operator()(const Value &value) {
    auto caller = ttg_starpu::detail::starpu_ttg_caller;
    if (nullptr == caller) {
      throw std::runtime_error("ERROR: ttg::send or ttg::broadcast called outside of a task!");
    }
    ttg_starpu::detail::ttg_data_copy_t *copy;
    copy = ttg_starpu::detail::find_copy_in_task(caller, &value);
    const Value *value_ptr = &value;
    if (nullptr == copy) {
      /**
       * the value is not known, create a copy that we can track
       * depending on Value, this uses either the copy or move constructor
       */
      copy = ttg_starpu::detail::create_new_datacopy(value);
      bool inserted = ttg_starpu::detail::add_copy_to_task(copy, caller);
      assert(inserted);
      value_ptr = reinterpret_cast<Value *>(copy->get_ptr());
      copy_to_remove = copy;
    }
    caller->data_flags = ttg_starpu::detail::ttg_starpu_data_flags::NONE;
    return *value_ptr;
  }

};

#endif  // STARPU_TTG_H_INCLUDED
// clang-format on
