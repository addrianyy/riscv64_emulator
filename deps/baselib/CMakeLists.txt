cmake_minimum_required(VERSION 3.20)

add_subdirectory(fmt)

add_library(baselib "")
add_subdirectory(src)

target_include_directories(baselib PUBLIC src)
target_compile_features(baselib PUBLIC cxx_std_20)
target_link_libraries(baselib PUBLIC fmt)
