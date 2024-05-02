add_subdirectory(jit)
add_subdirectory(private)

target_sources(riscv64_emulator PRIVATE
    Memory.cpp
    Memory.hpp
    RegisterState.cpp
    RegisterState.hpp
    Instruction.cpp
    Instruction.hpp
    Register.cpp
    Register.hpp
    Exit.cpp
    Exit.hpp
    Interpreter.cpp
    Interpreter.hpp
    Cpu.cpp
    Cpu.hpp
    Vm.cpp
    Vm.hpp
)