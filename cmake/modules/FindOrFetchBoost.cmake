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
endif()
# Bring ValeevGroup cmake toolkit, if not yet available
if(USE_PARSEC)
    include(FetchVGCMakeKit)
    include(${vg_cmake_kit_SOURCE_DIR}/modules/FindOrFetchBoost.cmake)
else()
    include(FetchContent)

    set(BOOST_VERSION "1.87.0")
    set(BOOST_TAG "boost-${BOOST_VERSION}")

    message(STATUS "Fetching Boost ${BOOST_VERSION}...")

    FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG        ${BOOST_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )

    FetchContent_GetProperties(Boost)

    if(NOT boost_POPULATED)
        FetchContent_Populate(Boost)
        
        if(NOT TARGET Boost::headers)
            add_library(Boost_headers INTERFACE)
            add_library(Boost::headers ALIAS Boost_headers)
            
            target_include_directories(Boost_headers INTERFACE 
                "$<BUILD_INTERFACE:${boost_SOURCE_DIR}>"
            )
            
            set(_boost_libs_root "${boost_SOURCE_DIR}/libs")
            
            file(GLOB _boost_includes RELATIVE ${boost_SOURCE_DIR} "${boost_SOURCE_DIR}/libs/*/include")
            
            foreach(_inc IN LISTS _boost_includes)
                target_include_directories(Boost_headers INTERFACE 
                    "$<BUILD_INTERFACE:${boost_SOURCE_DIR}/${_inc}>")
            endforeach()
        endif()
    endif()

    set(Boost_FOUND TRUE)
endif()
if (Boost_BUILT_FROM_SOURCE)
    set(TTG_BUILT_BOOST_FROM_SOURCE 1)
endif()

if (TARGET Boost::headers)
    set(TTG_HAS_BOOST 1)
    file(GLOB _boost_module_includes "${boost_SOURCE_DIR}/libs/*/include")

    get_target_property(_boost_headers_real_target Boost::headers ALIASED_TARGET)
    if (NOT _boost_headers_real_target)
        set(_boost_headers_real_target Boost::headers)
    endif()

    foreach(_inc_path IN LISTS _boost_module_includes)
        set_property(TARGET ${_boost_headers_real_target} APPEND PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${_inc_path}>")
    endforeach()
endif()
