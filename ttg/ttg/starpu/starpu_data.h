// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_DATA_H
#define TTG_STARPU_DATA_H

#include "ttg/buffer.h"
#include <starpu.h>

namespace ttg_starpu::detail {
  
  template<typename Value, typename Fn>
  void foreach_starpu_data(Value&& value, Fn&& fn) {
    if constexpr (ttg::detail::has_buffer_apply_v<Value>) {
      ttg::detail::buffer_apply(value, [&]<typename B>(B&& b){
        starpu_data_handle_t* data_handle = detail::get_starpu_data(b);
        if (nullptr != data_handle) {
          fn(data_handle);
        }
      });
    }
  }
  // Find the latest data copy on a device.
  inline std::tuple<int, void*> find_device_copy(void* data_handle) {
    if (!data_handle) return {0, nullptr};

    auto* handle = static_cast<starpu_data_handle_t*>(data_handle);
    void* ptr = nullptr;
    if (handle != nullptr) {
      ptr = starpu_data_get_local_ptr(*handle);
    }

    return {0, ptr};
  }

} // namespace ttg_starpu::detail

#endif // TTG_STARPU_DATA_H
