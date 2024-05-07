target_link_libraries(riscv64_emulator PRIVATE asmlib_x64)

target_sources(riscv64_emulator PRIVATE
    CodegenContext.cpp
    CodegenContext.hpp
    CodeGenerator.cpp
    CodeGenerator.hpp
    Executor.cpp
    Executor.hpp
    Exit.cpp
    Exit.hpp
    Registers.cpp
    Registers.hpp
    Trampoline.cpp
    Trampoline.hpp
    Abi.cpp
    Abi.hpp
)