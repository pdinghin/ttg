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
  //TODO: Implement with starpu implementation when device support is ready
  inline std::tuple<int, void*> find_device_copy(void* data_handle) {

    return {0, nullptr};
  }

} // namespace ttg_starpu::detail

#endif // TTG_STARPU_DATA_H
