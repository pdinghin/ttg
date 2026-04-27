# SPDX-License-Identifier: BSD-3-Clause
# update the Boost version that we can tolerate
if(USE_PARSEC)
    if (NOT DEFINED Boost_OLDEST_BOOST_VERSION)
        set(Boost_OLDEST_BOOST_VERSION ${TTG_OLDEST_BOOST_VERSION})
    else()
        if (${Boost_OLDEST_BOOST_VERSION} VERSION_LESS ${TTG_OLDEST_BOOST_VERSION})
            if (DEFINED CACHE{Boost_OLDEST_BOOST_VERSION})
                set(Boost_OLDEST_BOOST_VERSION "${TTG_OLDEST_BOOST_VERSION}" CACHE STRING "Oldest Boost version to use" FORCE)
            else()
                set(Boost_OLDEST_BOOST_VERSION ${TTG_OLDEST_BOOST_VERSION})
            endif()
        endif()
    endif()
    # Boost can be discovered by every (sub)package but only the top package can *build* it ...
    # in either case must declare the components used by TTG
    set(required_components
            headers
            callable_traits
    )
    set(optional_components
    )
    if (TTG_PARSEC_USE_BOOST_SERIALIZATION)
        list(APPEND optional_components
                serialization
                iostreams
        )
    endif()
    if (BUILD_EXAMPLES)
        list(APPEND optional_components
                bimap   # dependency of graph
                foreach # dependency of graph
                graph   # used for generation of inputs for spmm
                property_map # dependency of graph
                xpressive # dependency of graph
        )
    endif()

    # if not allowed to fetch Boost make all Boost optional
    if (NOT DEFINED Boost_FETCH_IF_MISSING AND TTG_FETCH_BOOST)
        set(Boost_FETCH_IF_MISSING 1)
    endif()
    if (NOT Boost_FETCH_IF_MISSING)
        foreach(__component IN LISTS required_components)
        list(APPEND optional_components
                ${__component}
        )
        endforeach()
        set(required_components )
    endif()

    if (DEFINED Boost_REQUIRED_COMPONENTS)
        list(APPEND Boost_REQUIRED_COMPONENTS
                ${required_components})
        list(REMOVE_DUPLICATES Boost_REQUIRED_COMPONENTS)
    else()
        set(Boost_REQUIRED_COMPONENTS "${required_components}" CACHE STRING "Components of Boost to discovered or built")
    endif()
    if (DEFINED Boost_OPTIONAL_COMPONENTS)
        list(APPEND Boost_OPTIONAL_COMPONENTS
                ${optional_components}
        )
        list(REMOVE_DUPLICATES Boost_OPTIONAL_COMPONENTS)
    else()
        set(Boost_OPTIONAL_COMPONENTS "${optional_components}" CACHE STRING "Optional components of Boost to discovered or built")
    endif()
# Bring ValeevGroup cmake toolkit, if not yet available
    include(FetchVGCMakeKit)
    include(${vg_cmake_kit_SOURCE_DIR}/modules/FindOrFetchBoost.cmake)
else()

    macro(boost_install)
    endmacro()
    macro(export)
    endmacro()

    include(FetchContent)

    set(Boost_ALL_COMPONENTS_NONMODULAR
        headers chrono context graph filesystem iostreams locale log math_tr1 
        math_c99 mpi program_options python random regex serialization thread timer wave
    )
    set(Boost_ALL_COMPONENTS
        algorithm align any atomic array assert bind chrono circular_buffer compute 
        concept_check config container container_hash conversion core date_time 
        describe detail dynamic_bitset endian exception filesystem function 
        functional function_types fusion headers integer intrusive io iostreams 
        iterator lexical_cast logic math move mp11 mpl multiprecision multi_index 
        numeric_conversion numeric_interval numeric_ublas optional parameter 
        phoenix pool predef preprocessor property_tree proto random range ratio 
        regex serialization smart_ptr spirit static_assert system test thread 
        throw_exception tokenizer tti tuple typeof type_index type_traits 
        callable_traits unordered utility uuid variant variant2 winapi
    )

    macro(component_to_targets _comp _targets)
        if ("${${_comp}}" STREQUAL "test")
            set(${_targets} unit_test_framework)
        else()
            set(${_targets} ${${_comp}})
        endif()
    endmacro()

    set(Boost_REQUIRED_COMPONENTS ${Boost_ALL_COMPONENTS})



    set(BOOST_REQUIRED_VERSION "1.87.0")
    string(REPLACE "." "_" BOOST_VERSION_UNDERSCORES ${BOOST_REQUIRED_VERSION})

    if (NOT TARGET Boost_headers)
        message(STATUS "Boost 1.87: Fetching archive...")

        include(FetchContent)

        FetchContent_Declare(
            Boost
            URL "https://archives.boost.io/release/1.87.0/source/boost_1_87_0.tar.gz"
            URL_HASH SHA256=f55c340aa49763b1925ccf02b2e83f35fdcf634c9d5164a2acb87540173c741d
            DOWNLOAD_EXTRACT_TIMESTAMP ON
        )

        FetchContent_MakeAvailable(Boost)
        FetchContent_GetProperties(Boost SOURCE_DIR boost_SOURCE_DIR)

        if(NOT TARGET Boost_headers)
            add_library(Boost_headers INTERFACE)
            add_library(Boost::headers ALIAS Boost_headers)
        endif()


        target_include_directories(Boost_headers INTERFACE "${boost_SOURCE_DIR}")
        
        file(GLOB _boost_libs_dirs "${boost_SOURCE_DIR}/libs/*")
        foreach(_lib_dir ${_boost_libs_dirs})
            if(IS_DIRECTORY "${_lib_dir}/include")
                target_include_directories(Boost_headers INTERFACE "${_lib_dir}/include")
            endif()
        endforeach()

        target_compile_definitions(Boost_headers INTERFACE BOOST_ALL_NO_LIB)

        set(EXPORT_NAMES "ttg" "tiledarray" "btas")
        foreach(exp ${EXPORT_NAMES})
            install(TARGETS Boost_headers EXPORT ${exp} OPTIONAL)
        endforeach()

        foreach(comp IN LISTS Boost_ALL_COMPONENTS)
            component_to_targets(comp tgt)
            if(NOT TARGET Boost::${tgt})
                set(iface_name "boost_${tgt}_iface")
                add_library(${iface_name} INTERFACE)
                add_library(Boost::${tgt} ALIAS ${iface_name})
                target_link_libraries(${iface_name} INTERFACE Boost_headers)
                
                foreach(exp ${EXPORT_NAMES})
                    install(TARGETS ${iface_name} EXPORT ${exp} OPTIONAL)
                endforeach()
            endif()
        endforeach()

        set(Boost_FOUND TRUE CACHE BOOL "" FORCE)
    endif()


    cmake_language(EVAL CODE "macro(export)\n _export(\${ARGN})\n endmacro()")

endif()



































 include(FetchContent)

    set(BOOST_REQUIRED_VERSION "1.87.0")
    string(REPLACE "." "_" BOOST_VERSION_UNDERSCORES ${BOOST_REQUIRED_VERSION})

    if (NOT TARGET Boost::headers)
        find_package(Boost ${BOOST_REQUIRED_VERSION} QUIET COMPONENTS random serialization system iostreams)
        
        if (NOT TARGET Boost::headers)
            message(STATUS "Boost ${BOOST_REQUIRED_VERSION} not found. Downloading...")

            FetchContent_Declare(
                Boost
                URL "https://archives.boost.io/release/${BOOST_REQUIRED_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORES}.tar.gz"
                URL_HASH SHA256=f55c340aa49763b1925ccf02b2e83f35fdcf634c9d5164a2acb87540173c741d
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            )

            FetchContent_GetProperties(Boost)
            if(NOT boost_POPULATED)
                FetchContent_Populate(Boost)
            endif()
            # -----------------------------------------------------------------------------

            if (NOT TARGET Boost_headers)
                add_library(Boost_headers INTERFACE)
                add_library(Boost::headers ALIAS Boost_headers)
                
                target_include_directories(Boost_headers INTERFACE 
                    "$<BUILD_INTERFACE:${boost_SOURCE_DIR}>"
                )

                set(_boost_fake_components "random" "serialization" "system" "iostreams" "filesystem")
                foreach(_comp IN LISTS _boost_fake_components)
                    if (NOT TARGET Boost::${_comp})
                        add_library(Boost_${_comp} INTERFACE)
                        add_library(Boost::${_comp} ALIAS Boost_${_comp})
                        target_link_libraries(Boost_${_comp} INTERFACE Boost_headers)
                    endif()
                endforeach()
            endif()
        endif()
    endif()

    if (TARGET Boost_headers)
        target_compile_definitions(Boost_headers INTERFACE BOOST_ALL_NO_LIB)
        target_compile_definitions(Boost_headers INTERFACE BOOST_SYSTEM_NO_DEPRECATED)
    endif()