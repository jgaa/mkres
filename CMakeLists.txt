cmake_minimum_required(VERSION 3.24)

set(MKRES_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT DEFINED MKRES_VERSION)
    set(MKRES_VERSION 0.1.0)
endif()

set(MKRES_VERSION_STR \"${MKRES_VERSION}\")
add_definitions(-DMKRES_VERSION_STR=${MKRES_VERSION_STR})

project(mkres
    DESCRIPTION "A small command line program to embed files as data in C++ headers."
    HOMEPAGE_URL https://github.com/jgaa/mkres
    VERSION ${MKRES_VERSION}
    LANGUAGES CXX)
set(MKRES_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

option(MKRES_WITH_GZIP "Enable gzip compression" ON)
option(MKRES_WITH_TESTS "Enable Tests" ON)
option(MKRES_WITH_EXAMPLES "Enable Examples" ON)

if (MKRES_WITH_TESTS)
    find_package(GTest REQUIRED)
endif()

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.75)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    program_options
    )

include(GNUInstallDirs)

add_executable(${PROJECT_NAME} src/main.cpp src/gzipranges.hpp)

if (MKRES_WITH_GZIP)
    find_package(ZLIB REQUIRED)
    target_include_directories(${PROJECT_NAME} PUBLIC ${ZLIB_INCLUDE_DIRS})
    set(zlib_libs ${ZLIB_LIBRARIES})
    add_definitions(-DMKRES_WITH_GZIP=1)
endif()

target_link_libraries(${PROJECT_NAME} stdc++fs ${Boost_LIBRARIES} ${zlib_libs} )
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}_Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if (MKRES_WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
