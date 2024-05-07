include(CheckCSourceCompiles)

check_c_source_compiles([=[
#if defined(__aarch64__) || defined(_M_ARM64)
int main() { return 0; }
#else
#error "x"
#endif
]=] architecture_aarch64)

check_c_source_compiles([=[
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
int main() { return 0; }
#else
#error "x"
#endif
]=] architecture_x64)

if (architecture_aarch64)
    add_subdirectory(aarch64)
    target_compile_definitions(riscv64_emulator PRIVATE VM_JIT_AARCH64)
    message(STATUS "detected aarch64: JIT is enabled")
elseif (architecture_x64)
    add_subdirectory(x64)
    target_compile_definitions(riscv64_emulator PRIVATE VM_JIT_X64)
    message(STATUS "detected x64: JIT is enabled")
else ()
    message(STATUS "unknown architecture: JIT is not supported")
endif()

target_sources(riscv64_emulator PRIVATE
    CodeBuffer.cpp
    CodeBuffer.hpp
    Executor.cpp
    Executor.hpp
    CodeDump.cpp
    CodeDump.hpp
    ExecutableBuffer.cpp
    ExecutableBuffer.hpp
    Exit.hpp
    Exit.cpp
    CreateExecutor.cpp
    CreateExecutor.hpp
    Utilities.cpp
    Utilities.hpp
)