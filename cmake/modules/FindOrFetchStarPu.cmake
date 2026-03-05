include(FindPkgConfig)
set(STARPU_VERSION "1.4")
set(_STARPU_TARBALL "https://files.inria.fr/starpu/starpu-${STARPU_VERSION}/starpu-${STARPU_VERSION}.tar.gz")
set(_STARPU_SOURCE "${PROJECT_SOURCE_DIR}/starpu-${STARPU_VERSION}")
set(STARPU_FOUND FALSE)

if(PKG_CONFIG_FOUND)
  pkg_search_module(STARPU IMPORTED_TARGET starpu-1.4)
endif()
if(STARPU_FOUND)
else()
  ExternalProject_Add(starpu
    PREFIX "${PROJECT_BINARY_DIR}"
    URL ${STARPU_TARBALL}
    SOURCE_DIR ${STARPU_SOURCE}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
                      --prefix=${CMAKE_INSTALL_PREFIX}
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

  set(STARPU_FOUND TRUE)
endif()
