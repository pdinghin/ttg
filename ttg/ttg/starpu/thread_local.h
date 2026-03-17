// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_THREAD_LOCAL_H
#define TTG_STARPU_THREAD_LOCAL_H

namespace ttg_starpu {

namespace detail {

  // fwd decls
  struct starpu_ttg_task_base_t;
  struct ttg_data_copy_t;

  inline thread_local starpu_ttg_task_base_t *starpu_ttg_caller = nullptr;

  inline ttg_data_copy_t*& ttg_data_copy_container() {
    static thread_local ttg_data_copy_t *ptr = nullptr;
    return ptr;
  }

} // namespace detail
} // namespace ttg_starpu

#endif // TTG_STARPU_THREAD_LOCAL_H