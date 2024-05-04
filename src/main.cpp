#include "ElfLoader.hpp"

#include <base/Error.hpp>
#include <base/Log.hpp>
#include <base/Print.hpp>
#include <base/time/Stopwatch.hpp>

#include <vm/Cpu.hpp>
#include <vm/Vm.hpp>

int main() {
  vm::Vm vm{32 * 1024 * 1024};

  const auto image = ElfLoader::load("/Users/adrian/dev/rvtest/a.out", vm.memory());
  log_info("loaded elf at {:x} with size {:x}", image.base, image.size);

  {
    const auto max_executable_address = image.base + image.size;
    vm.use_jit(std::make_shared<vm::jit::CodeBuffer>(vm::jit::CodeBuffer::Type::Singlethreaded,
                                                     16 * 1024 * 1024, max_executable_address));
  }

  vm::Cpu cpu;
  cpu.set_reg(vm::Register::Sp, image.base - 8);
  cpu.set_reg(vm::Register::Pc, image.entrypoint);

  base::Stopwatch stopwatch;

  auto exit = vm.run(cpu);

  log_info("{:x}: {}", cpu.pc(), cpu.reg(exit.target_register));
  log_info("took {}", stopwatch.elapsed());
}
