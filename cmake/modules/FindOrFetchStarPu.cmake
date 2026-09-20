include(FindPkgConfig)
include(ExternalProject)

set(STARPU_CUDA_FLAGS "--disable-cuda") 

if(TTG_ENABLE_CUDA)
  find_package(CUDAToolkit)
  if(CUDAToolkit_FOUND)
    message(STATUS "CUDA Toolkit found for StarPU: ${CUDAToolkit_LIBRARY_DIR}")
    set(STARPU_CUDA_FLAGS 
        "--enable-cuda"
        "--with-cuda-dir=${CUDAToolkit_TARGET_DIR}"
        "--with-cuda-lib-dir=${CUDAToolkit_LIBRARY_DIR}"
    )
  else()
    message(WARNING "CUDA requested for StarPU, but CUDAToolkit was not found. Building without CUDA.")
  endif()
endif()

if(NOT DEFINED STARPU_VERSION)
  set(STARPU_VERSION "1.4.3") 
endif()

add_library(MyProject::StarPU INTERFACE IMPORTED GLOBAL)
set(STARPU_FOUND FALSE)

if(PKG_CONFIG_FOUND)
  pkg_search_module(STARPU IMPORTED_TARGET starpu-1.4)
  if(TARGET PkgConfig::STARPU)
    target_link_libraries(MyProject::StarPU INTERFACE PkgConfig::STARPU)
    message(STATUS "Found system StarPU and mapped to MyProject::StarPU")
    set(STARPU_FOUND TRUE)
  endif()
endif()

if(NOT STARPU_FOUND)
  set(STARPU_TARBALL "https://files.inria.fr/starpu/starpu-${STARPU_VERSION}/starpu-${STARPU_VERSION}.tar.gz")
  set(STARPU_INSTALL_DIR "${CMAKE_BINARY_DIR}/starpu_install")
  
  file(MAKE_DIRECTORY "${STARPU_INSTALL_DIR}/include")

  set(STARPU_LIB_PATH "${STARPU_INSTALL_DIR}/lib/libstarpu-1.4${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(STARPU_INCLUDE_DIR "${STARPU_INSTALL_DIR}/include")

  add_library(StarPU_Local STATIC IMPORTED GLOBAL)
  set_target_properties(StarPU_Local PROPERTIES
    IMPORTED_LOCATION "${STARPU_LIB_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${STARPU_INCLUDE_DIR}"
  )

  # Determine parallel jobs fallback
  if(NOT DEFINED PARALLEL_JOBS)
    set(PARALLEL_JOBS 4)
  endif()

  ExternalProject_Add(starpu_ext
    PREFIX "${CMAKE_BINARY_DIR}/starpu_build"
    URL ${STARPU_TARBALL}
    INSTALL_DIR ${STARPU_INSTALL_DIR}
    DOWNLOAD_EXTRACT_TIMESTAMP ON
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
                      --prefix=${STARPU_INSTALL_DIR} 
                      --enable-fast
                      --enable-blas-lib=none
                      --disable-mlr
                      --disable-opencl
                      --disable-build-examples
                      --disable-build-tests
                      --disable-build-doc
                      --disable-fortran
                      --disable-starpufft
                      ${STARPU_CUDA_FLAGS}        
                      CC=${CMAKE_C_COMPILER}
                      CFLAGS=${CMAKE_C_FLAGS}
                      CXX=${CMAKE_CXX_COMPILER}
                      CXXFLAGS=${CMAKE_CXX_FLAGS}
    BUILD_COMMAND     ${CMAKE_MAKE_PROGRAM} -j${PARALLEL_JOBS}
    INSTALL_COMMAND   ${CMAKE_MAKE_PROGRAM} install
  )

  add_dependencies(StarPU_Local starpu_ext)
  
  target_link_libraries(MyProject::StarPU INTERFACE StarPU_Local)
  
  add_dependencies(MyProject::StarPU starpu_ext)
  
  message(STATUS "Using Local StarPU (via ExternalProject) with CUDA flags: ${STARPU_CUDA_FLAGS}")
  set(STARPU_FOUND TRUE)
endif()