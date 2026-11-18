// Fuzzes the MOS 6502 emulator: the input is a memory image the CPU executes (official and
// undocumented opcode slots, decimal mode, IRQ/NMI) for a bounded number of instructions.
#include "emudevs/MOS6502.h"
#include "emu_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
  if (size < 3 || size > 2 + 65536)
    return -1;

  static EmuBus bus;
  int pc;
  emuLoad(&bus, data, size, &pc);

  fabgl::MOS6502 cpu;
  // zero page and stack page get their own callbacks in this emulator; map them onto the same image
  cpu.setCallbacks(&bus, emuReadByte, emuWriteByte, emuReadByte, emuWriteByte, emuReadByte, emuWriteByte);
  cpu.reset();
  cpu.setPC(pc);

  for (int i = 0; i < EMU_MAX_STEPS; ++i) {
    cpu.step();
    uint8_t sel = bus.mem[(i * 7) & 0xffff];
    if (sel == 0xfd)
      cpu.IRQ();
    else if (sel == 0xfe)
      cpu.NMI();
  }
  return 0;
}
