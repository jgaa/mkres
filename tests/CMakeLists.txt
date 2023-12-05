project(unittests LANGUAGES CXX)

add_executable(gzip_tests
    gzip_tests.cpp
    )

set_property(TARGET gzip_tests PROPERTY CXX_STANDARD 20)

message("MKRES_ROOT: ${MKRES_ROOT}")

include_directories(${MKRES_ROOT}/src)

target_include_directories(gzip_tests
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${MKRES_ROOT}/src # Don't work wtf!!
    )

target_link_libraries(gzip_tests
    ${GTEST_LIBRARIES}
    ${Boost_LIBRARIES}
    ${ZLIB_LIBRARIES}
    stdc++fs
    ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME gzip_tests COMMAND gzip_tests)
