// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_DEVICEFUNC_H
#define TTG_STARPU_DEVICEFUNC_H

#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <cassert>

#include <starpu.h>

#include "ttg/starpu/thread_local.h"
#include "ttg/starpu/starpu-ext.h"
#include "ttg/starpu/buffer.h"
#include "ttg/starpu/devicescratch.h"

namespace ttg_starpu {
  namespace detail {

    inline uint8_t get_starpu_access_mode(bool is_const, ttg::scope scope, bool is_scratch = false) {
      if (is_scratch) {
        return static_cast<uint8_t>(STARPU_RW | TTG_STARPU_FLOW_ACCESS_TMP);
      }
      if (is_const) {
        return static_cast<uint8_t>(STARPU_R);
      }
      if (scope == ttg::scope::Allocate) {
        return static_cast<uint8_t>(STARPU_W);
      }
      return static_cast<uint8_t>(STARPU_RW);
    }

    template<typename... Views, std::size_t I, std::size_t... Is>
    bool register_device_memory_impl(std::tuple<Views&...> &views, std::index_sequence<I, Is...>) {
      using view_type = std::remove_reference_t<std::tuple_element_t<I, std::tuple<Views&...>>>;
      static_assert(ttg::meta::is_buffer_v<view_type> || ttg::meta::is_devicescratch_v<view_type>,
                    "register_device_memory only supports buffers and devicescratch views");

      auto& view = std::get<I>(views);
      (void)view;

      if constexpr (sizeof...(Is) > 0) {
        return register_device_memory_impl(views, std::index_sequence<Is...>{});
      }
      return true;
    }

    template<typename... Views, std::size_t I, std::size_t... Is>
    void mark_device_out_impl(std::tuple<Views&...> &views, std::index_sequence<I, Is...>) {
      (void)views;
      if constexpr (sizeof...(Is) > 0) {
        mark_device_out_impl(views, std::index_sequence<Is...>{});
      }
    }

    template<typename... Views, std::size_t I, std::size_t... Is>
    void post_device_out_impl(std::tuple<Views&...> &views, std::index_sequence<I, Is...>) {
      (void)views;
      if constexpr (sizeof...(Is) > 0) {
        post_device_out_impl(views, std::index_sequence<Is...>{});
      }
    }

  } // namespace detail

  template<typename... Views>
  bool register_device_memory(std::tuple<Views&...> &views) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("register_device_memory may only be invoked from inside a task!");
    }

    if constexpr (sizeof...(Views) > 0) {
      return detail::register_device_memory_impl(views, std::index_sequence_for<Views...>{});
    }
    return true;
  }

  template<typename T, std::size_t N>
  bool register_device_memory(const ttg::span<T, N>& span) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("register_device_memory may only be invoked from inside a task!");
    }

    for (std::size_t i = 0; i < span.size(); ++i) {
      auto& view = span[i];
      (void)view;
    }
    return true;
  }

  template<typename... Buffer>
  void mark_device_out(std::tuple<Buffer&...> &b) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("mark_device_out may only be invoked from inside a task!");
    }

    detail::mark_device_out_impl(b, std::index_sequence_for<Buffer...>{});
  }

  template<typename... Buffer>
  void post_device_out(std::tuple<Buffer&...> &b) {
    if (nullptr == ttg_starpu::detail::starpu_ttg_caller) {
      throw std::runtime_error("post_device_out may only be invoked from inside a task!");
    }

    detail::post_device_out_impl(b, std::index_sequence_for<Buffer...>{});
  }

  template<typename T>
  void* buffer_data(T&& buffer) {
    using view_type = std::remove_reference_t<T>;
    static_assert(ttg::meta::is_buffer_v<view_type> || ttg::meta::is_devicescratch_v<view_type>,
                  "buffer_data requires a buffer or devicescratch view");

    return detail::get_starpu_data(buffer);
  }

} // namespace ttg_starpu

#endif // TTG_STARPU_DEVICEFUNC_H
