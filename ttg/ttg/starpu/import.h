// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTG_STARPU_IMPORT_H
#define TTG_STARPU_IMPORT_H

#include "ttg/runtimes.h"

#if defined(TTG_SELECTED_DEFAULT_IMPL)
#error "A default TTG implementation has already been selected"
#endif  // defined(TTG_SELECTED_DEFAULT_IMPL)

#define TTG_SELECTED_DEFAULT_IMPL starpu
#define TTG_STARPU_IMPORTED 1
#define TTG_IMPL_NS ttg_starpu
#define TTG_IMPL_DEVICE_SUPPORT 1

namespace ttg_starpu {}

namespace ttg {

  /* Mark the ttg_starpu namespace as the default */
  using namespace ttg_starpu;

  constexpr const ttg::Runtime ttg_runtime = ttg::Runtime::StarPU;

}  // namespace ttg

#endif  // TTG_STARPU_IMPORT_H
