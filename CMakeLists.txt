cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED MKRES_VERSION)
    set(MKRES_VERSION 0.0.1)
endif()
set(MKRES_VERSION_STR \"${MKRES_VERSION}\")
add_definitions(-DMKRES_VERSION_STR=${MKRES_VERSION_STR})

project(mkres
    DESCRIPTION "A small command line program to embed files as data in C++ headers."
    HOMEPAGE_URL https://github.com/jgaa/mkres
    VERSION ${MKRES_VERSION}
    LANGUAGES CXX)
set(MKRES_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

option(MKRES_WITH_TESTS "Enable Tests" ON)
option(MKRES_WITH_EXAMPLES "Enable Examples" ON)

if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.75)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    program_options
    )

include(GNUInstallDirs)

add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} stdc++fs ${Boost_LIBRARIES} )
target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}_Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

