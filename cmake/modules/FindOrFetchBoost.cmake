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
        unordered utility uuid variant variant2 winapi
    )
    macro(boost_install)
    endmacro()
    macro(component_to_targets _comp _targets)
        if ("${${_comp}}" STREQUAL "test")
            set(${_targets} unit_test_framework)
        else()
            set(${_targets} ${${_comp}})
        endif()
    endmacro()

    macro(intersection _out _in1 _in2)
        set(${_out})
        foreach(x IN LISTS ${_in1})
            if(x IN_LIST ${_in2})
                list(APPEND ${_out} ${x})
            endif()
        endforeach()
    endmacro()

    set(BOOST_REQUIRED_VERSION "1.87.0")
    string(REPLACE "." "_" BOOST_VERSION_UNDERSCORES ${BOOST_REQUIRED_VERSION})

    if(NOT Boost_REQUIRED_COMPONENTS)
        set(Boost_REQUIRED_COMPONENTS headers unordered random serialization system filesystem iostreams)
    endif()

    find_package(Boost ${BOOST_REQUIRED_VERSION} QUIET COMPONENTS ${Boost_REQUIRED_COMPONENTS})

    if (NOT Boost_FOUND AND NOT TARGET Boost::headers)
        message(STATUS "Boost ${BOOST_REQUIRED_VERSION} not found. Fetching via FetchContent...")

        FetchContent_Declare(
            Boost
            URL "https://archives.boost.io/release/${BOOST_REQUIRED_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORES}.tar.gz"
            URL_HASH SHA256=f55c340aa49763b1925ccf02b2e83f35fdcf634c9d5164a2acb87540173c741d
            DOWNLOAD_EXTRACT_TIMESTAMP ON
        )

        set(BOOST_INCLUDE_LIBRARIES ${Boost_REQUIRED_COMPONENTS} CACHE STRING "" FORCE)
        set(BOOST_INSTALL OFF CACHE BOOL "" FORCE)

        FetchContent_MakeAvailable(Boost)
        FetchContent_GetProperties(Boost SOURCE_DIR boost_SOURCE_DIR)

        foreach(lib IN LISTS Boost_REQUIRED_COMPONENTS)
            component_to_targets(lib __lib_targets)
            foreach(tgt IN LISTS __lib_targets)
                if (NOT TARGET Boost::${tgt})
                    if (TARGET boost_${tgt})
                        add_library(Boost::${tgt} ALIAS boost_${tgt})
                    else()
                        add_library(Boost_${tgt}_interface INTERFACE)
                        add_library(Boost::${tgt} ALIAS Boost_${tgt}_interface)
                        target_include_directories(Boost_${tgt}_interface INTERFACE "${boost_SOURCE_DIR}")
                    endif()
                endif()
            endforeach()
        endforeach()
    endif()

    if (TARGET Boost_headers)
        target_compile_definitions(Boost_headers INTERFACE BOOST_ALL_NO_LIB)
        message(STATUS "FindOrFetchBoost: Boost ${BOOST_REQUIRED_VERSION} is ready (Header-only mode).")

    elseif(TARGET Boost::headers)
        get_target_property(_is_alias Boost::headers ALIAS_FOR)
        
        if(_is_alias)
            target_compile_definitions(${_is_alias} INTERFACE BOOST_ALL_NO_LIB)
        else()
            set_target_properties(Boost::headers PROPERTIES INTERFACE_COMPILE_DEFINITIONS "BOOST_ALL_NO_LIB")
        endif()
    endif()
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