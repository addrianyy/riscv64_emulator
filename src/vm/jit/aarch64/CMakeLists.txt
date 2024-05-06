target_link_libraries(riscv64_emulator PRIVATE asmlib_a64)

target_sources(riscv64_emulator PRIVATE
    Registers.cpp
    Registers.cpp
    RegisterCache.cpp
    RegisterCache.hpp
    CodeGenerator.cpp
    CodeGenerator.hpp
    Utilities.cpp
    Utilities.hpp
    CodegenContext.cpp
    CodegenContext.hpp
    Exit.cpp
    Exit.hpp
    Executor.cpp
    Executor.hpp
    Trampoline.cpp
    Trampoline.hpp
)