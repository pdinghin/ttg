# SPDX-License-Identifier: BSD-3-Clause
# update the Boost version that we can tolerate

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
if(USE_PARSEC)
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
    include(FetchContent)

    set(BOOST_REQUIRED_VERSION "1.87.0")

    if (NOT TARGET Boost::headers)
        set(_boost_needed_components "random" "serialization" "system" "iostreams")
        
        find_package(Boost ${BOOST_REQUIRED_VERSION} QUIET COMPONENTS ${_boost_needed_components})
        
        if (TARGET Boost::headers)
            message(STATUS "Found Boost system-wide: ${Boost_VERSION}")
        endif ()
    endif ()
    if (NOT TARGET Boost::headers)
        message(STATUS "Boost ${BOOST_REQUIRED_VERSION} not found or incomplete. Downloading official archive...")

        FetchContent_Declare(
            Boost
            URL https://github.com/boostorg/boost/releases/download/boost-${BOOST_REQUIRED_VERSION}/boost-${BOOST_REQUIRED_VERSION}.tar.gz
            URL_HASH SHA256=af91168074693a7431e13337e6d97c050a4f5f590637f90400030221319c5c2d
        )

        FetchContent_MakeAvailable(Boost)

        FetchContent_GetProperties(Boost SOURCE_DIR BOOST_SOURCE_DIR)

        if (NOT TARGET Boost::headers)
            add_library(Boost_headers INTERFACE)
            add_library(Boost::headers ALIAS Boost_headers)
            
            target_include_directories(Boost_headers INTERFACE 
                "$<BUILD_INTERFACE:${BOOST_SOURCE_DIR}>"
            )
            set(_boost_fake_components "random" "serialization" "system" "iostreams" "filesystem")
            
            foreach(_comp IN LISTS _boost_fake_components)
                if (NOT TARGET Boost::${_comp})
                    add_library(Boost_${_comp} INTERFACE)
                    add_library(Boost::${_comp} ALIAS Boost_${_comp})
                    target_link_libraries(Boost_${_comp} INTERFACE Boost::headers)
                endif()
            endforeach()
        endif()
    endif()

    if (NOT TARGET Boost::headers)
        message(FATAL_ERROR "FindOrFetchBoost could not make Boost::headers target available")
    else()
        target_compile_definitions(Boost::headers INTERFACE BOOST_ALL_NO_LIB)
        target_compile_definitions(Boost::headers INTERFACE BOOST_SYSTEM_NO_DEPRECATED)
    endif()
endif()
