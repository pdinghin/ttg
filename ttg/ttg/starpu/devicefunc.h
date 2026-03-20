// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_DEVICEFUNC_H
#define TTG_STARPU_DEVICEFUNC_H

#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <cassert>

#include <starpu.h>

#include "ttg/starpu/thread_local.h"
#include "ttg/starpu/starpu-ext.h"

namespace ttg_starpu {
  namespace detail {
    
    inline uint8_t get_starpu_access_mode(bool is_const, int scope, bool is_scratch = false) {
      // TODO: Replace with proper StarPU access modes readonly/writeonly/readwrite
      return 0;
    }
    
    template<typename... Views, std::size_t I, std::size_t... Is>
    void mark_device_out_impl(std::tuple<Views&...> &views, std::index_sequence<I, Is...>) {
      if constexpr (sizeof...(Is) > 0) {
        mark_device_out_impl(views, std::index_sequence<Is...>{});
      }
    }

    template<typename... Views, std::size_t I, std::size_t... Is>
    void post_device_out_impl(std::tuple<Views&...> &views, std::index_sequence<I, Is...>) {
      if constexpr (sizeof...(Is) > 0) {
        post_device_out_impl(views, std::index_sequence<Is...>{});
      }
    }
    
  } // namespace detail

  /* TODO: Implement device memory management for StarPU.
   * Full device integration will be implemented when StarPU device
   * support is finalized. */
  template<typename... Views>
  bool register_device_memory(std::tuple<Views&...> &views) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("register_device_memory may only be invoked from inside a task!");
    }
    
    // TODO: StarPU device support - implement device memory registration (actually no transfer needed)
    return true;
  }
  
  // TODO: Implement device memory management for StarPU spans.
  template<typename T, std::size_t N>
  bool register_device_memory(const ttg::span<T, N>& span) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("register_device_memory may only be invoked from inside a task!");
    }
    return true;
  }


   // TODO: Implement device output marking for StarPU.
  template<typename... Buffer>
  void mark_device_out(std::tuple<Buffer&...> &b) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("mark_device_out may only be invoked from inside a task!");
    }

  }

  
    // TODO: Implement post-device processing for StarPU. 
  template<typename... Buffer>
  void post_device_out(std::tuple<Buffer&...> &b) {
    // TODO: StarPU device support - implement post-device processing
  }

  /* TODO: Implement proper StarPU data handle extraction.(starpu_datat_handle_t)
   * Currently returns the impl_data from the buffer if available. */
  template<typename T>
  void* buffer_data(T&& buffer) {
    using view_type = std::remove_reference_t<T>;
    static_assert(ttg::meta::is_buffer_v<view_type> || ttg::meta::is_devicescratch_v<view_type>,
                  "buffer_data requires a buffer or devicescratch view");
    
    if constexpr (requires { buffer.impl_data; }) {
      return buffer.impl_data;
    } else {
      return static_cast<void*>(nullptr);
    }
  }

} // namespace ttg_starpu

#endif // TTG_STARPU_DEVICEFUNC_H
