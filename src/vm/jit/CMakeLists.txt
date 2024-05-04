add_subdirectory(aarch64)

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
)