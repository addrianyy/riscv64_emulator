add_subdirectory(vm)

target_sources(riscv64_emulator PRIVATE
    main.cpp
    ElfLoader.cpp
    ElfLoader.hpp
)