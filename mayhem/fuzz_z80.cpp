// Fuzzes the Z80 emulator: the input is a memory image the CPU executes (all documented and
// undocumented opcodes, DD/FD/ED/CB prefixes, interrupts) for a bounded number of instructions.
#include "emudevs/Z80.h"
#include "emu_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
  if (size < 3 || size > 2 + 65536)
    return -1;

  static EmuBus bus;
  int pc;
  emuLoad(&bus, data, size, &pc);

  fabgl::Z80 cpu;
  cpu.setCallbacks(&bus, emuReadByte, emuWriteByte, emuReadWord, emuWriteWord, emuReadIO, emuWriteIO);
  cpu.reset();
  cpu.setPC(pc);

  for (int i = 0; i < EMU_MAX_STEPS; ++i) {
    cpu.step();
    // let the image itself decide when to raise interrupts, so they are reproducible from the input
    uint8_t sel = bus.mem[(i * 7) & 0xffff];
    if (sel == 0xfd)
      cpu.IRQ(bus.mem[(i * 13) & 0xffff]);
    else if (sel == 0xfe)
      cpu.NMI();
    if (cpu.getStatus() != 0)
      break;
  }
  return 0;
}
