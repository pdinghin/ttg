include(FindPkgConfig)
set(STARPU_TARBALL "https://files.inria.fr/starpu/starpu-${STARPU_VERSION}/starpu-${STARPU_VERSION}.tar.gz")
set(STARPU_FOUND FALSE)
add_library(MyProject::StarPU INTERFACE IMPORTED GLOBAL)

if(PKG_CONFIG_FOUND)
  pkg_search_module(STARPU IMPORTED_TARGET starpu-1.4)
  if(TARGET PkgConfig::STARPU)
    target_link_libraries(MyProject::StarPU INTERFACE PkgConfig::STARPU)
    message(STATUS "Found StarPU and mapped to MyProject::StarPU")
  endif()
endif()
if(STARPU_FOUND)
else()

  set(STARPU_INSTALL_DIR "${PROJECT_BINARY_DIR}/_deps/starpu")
  add_library(StarPU_Local STATIC IMPORTED GLOBAL)
  set(STARPU_LIB_PATH "${STARPU_INSTALL_DIR}/lib/libstarpu-1.4${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(STARPU_INCLUDE_DIR "${STARPU_INSTALL_DIR}/include/starpu/1.4")
  set_target_properties(StarPU_Local PROPERTIES
  IMPORTED_LOCATION "${STARPU_LIB_PATH}"
  INTERFACE_INCLUDE_DIRECTORIES "${STARPU_INCLUDE_DIR}"
  )

  #set the path to install starpu
  set(STARPU_SOURCE "${PROJECT_SOURCE_DIR}/starpu-${STARPU_VERSION}")

  include(ExternalProject)
  ExternalProject_Add(starpu
    PREFIX "${PROJECT_BINARY_DIR}"
    URL ${STARPU_TARBALL}
    SOURCE_DIR ${STARPU_SOURCE}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
                      --prefix=${STARPU_INSTALL_DIR}
                      ${CONFIGURE_DEBUG_ARG}
                      --enable-fast
                      --enable-blas-lib=none
                      --disable-mlr
                      --disable-opencl
                      --disable-build-examples
                      --disable-build-tests
                      --disable-build-doc
                      --disable-fortran
                      --disable-starpufft
                      ${CONFIGURE_CUDA_ARG}
                      ${_STARPU_PARSED_ARGS}
                      CC=${CMAKE_C_COMPILER}
                      CFLAGS=${CMAKE_C_FLAGS}
                      CXX=${CMAKE_CXX_COMPILER}
                      CXXFLAGS=${CMAKE_CXX_FLAGS}
    BUILD_COMMAND     ${CMAKE_MAKE_PROGRAM} -j${PARALLEL_JOBS}
    )
  add_dependencies(StarPU_Local starpu_build)
  target_link_libraries(MyProject::StarPU INTERFACE StarPU_Local)
  message(STATUS "Using Local StarPU (via ExternalProject)")
  set(STARPU_FOUND TRUE)
endif()
