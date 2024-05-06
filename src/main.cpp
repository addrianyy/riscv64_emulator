#include "ElfLoader.hpp"

#include <base/Error.hpp>
#include <base/Log.hpp>
#include <base/Print.hpp>
#include <base/time/Stopwatch.hpp>

#include <vm/Cpu.hpp>
#include <vm/Vm.hpp>

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    log_info("usage: riscv64_emulator [elf image path]");
    return 1;
  }

  const auto elf_path = argv[1];

  vm::Vm vm{32 * 1024 * 1024};

  log_info("loading {}...", elf_path);
  const auto image = ElfLoader::load(elf_path, vm.memory());
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

  const auto exit = vm.run(cpu);
  const auto execution_time = stopwatch.elapsed();

  log_info("exited the VM in {} with reason: {}", execution_time, exit.reason);
  log_info("pc: {:#x}", cpu.pc());
  if (exit.reason == vm::Exit::Reason::MemoryReadFault ||
      exit.reason == vm::Exit::Reason::MemoryWriteFault) {
    log_info("faulty address: {:#x}", exit.faulty_address);
    if (exit.reason == vm::Exit::Reason::MemoryWriteFault) {
      log_info("written value: {}", cpu.reg(exit.target_register));
    }
  }
}
