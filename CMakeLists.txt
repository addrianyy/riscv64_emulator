cmake_minimum_required(VERSION 3.20)
project(riscv64_emulator)

add_subdirectory(deps/baselib)
add_subdirectory(deps/asmlib_x64)
add_subdirectory(deps/asmlib_a64)

add_executable(riscv64_emulator "")

add_subdirectory(src)

target_link_libraries(riscv64_emulator PUBLIC baselib)
target_include_directories(riscv64_emulator PUBLIC src)
target_compile_features(riscv64_emulator PUBLIC cxx_std_20)

#target_compile_definitions(riscv64_emulator PRIVATE PRINT_EXECUTION_LOG)
#target_compile_definitions(riscv64_emulator PRIVATE JIT_LOG_GENERATED_BLOCKS)