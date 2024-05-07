import sys
import struct

from capstone import *
from capstone.arm64_const import ARM64_GRP_BRANCH_RELATIVE

command_line_arguments = sys.argv[1:]
if len(command_line_arguments) != 1:
    raise Exception("usage: ViewJit.py [jit_dump.bin]")

class X64BlockHandler:
    def __init__(self) -> None:
        self.capstone_engine = Cs(CS_ARCH_X86, CS_MODE_64)
        self.capstone_engine.skipdata = True
        self.capstone_engine.detail = True

    def pad_mnemonic(self, mnemonic: str):
        while len(mnemonic) < 6:
            mnemonic = mnemonic + " "
        return mnemonic

    def handle_block(self, pc, code):
        print(f"PC 0x{pc:x}:")
        for instr in self.capstone_engine.disasm(code, 0):
            print(f"    {self.pad_mnemonic(instr.mnemonic)} {instr.op_str}")

        print()

class AArch64BlockHandler:
    def __init__(self) -> None:
        self.capstone_engine = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        self.capstone_engine.skipdata = True
        self.capstone_engine.detail = True

    def pad_mnemonic(self, mnemonic: str):
        while len(mnemonic) < 6:
            mnemonic = mnemonic + " "
        return mnemonic

    def handle_block(self, pc, code):
        source_labels = {}
        target_labels = {}

        for instr in self.capstone_engine.disasm(code, 0):
            try:
                if CS_GRP_JUMP in instr.groups and ARM64_GRP_BRANCH_RELATIVE not in instr.groups:
                    pass
                elif ARM64_GRP_BRANCH_RELATIVE in instr.groups:
                    target_off = int(instr.op_str.replace('#', ''), 16)
                    target_addr = target_off
                    source_labels.update({instr.address: "lbl_{:06x}".format(target_addr)})
                    target_labels.update({target_addr: "lbl_{:06x}".format(target_addr)})
                else:
                    pass
            except Exception:
                pass

        print(f"PC 0x{pc:x}:")
        for instr in self.capstone_engine.disasm(code, 0):
            target_label = target_labels.get(instr.address)
            source_label = source_labels.get(instr.address)

            if target_label:
                print(f"  {target_label}:")

            additional_info = ""
            if source_label:
                additional_info = f"   // {source_label}"

            print(f"    {self.pad_mnemonic(instr.mnemonic)} {instr.op_str}{additional_info}")

        print()


def read_block(file, handler):
    header_bytes = file.read(16)
    if header_bytes is None or len(header_bytes) != 16:
        return False

    pc, size = struct.unpack("QQ", header_bytes)
    code_block = file.read(size)
    if code_block is None or len(code_block) != size:
        print(f"block {pc:x} is truncated")
        return False

    handler.handle_block(pc, code_block)

    return True


with open(command_line_arguments[0], "rb") as f:
    header_bytes = f.read(8)
    if header_bytes is None or len(header_bytes) != 8:
        raise Exception("invalid jit dump header")
    
    magic, arch = struct.unpack("II", header_bytes)

    assert magic == 0xab773acf, "invalid jit dump magic"

    block_handler = None

    match arch:
        case 1:
            block_handler = AArch64BlockHandler()
            print("architecture: aarch64")
        case 2:
            block_handler = X64BlockHandler()
            print("architecture: x64")
        case _:
            raise Exception(f"invalid jit dump architecture {arch}")

    print()

    while read_block(f, block_handler):
        pass