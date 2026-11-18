// Fuzzes the Intel 8080 emulator: the input is a memory image the CPU executes for a bounded
// number of instructions, with interrupts (RST n) raised from the image contents.
#include "emudevs/i8080.h"
#include "emu_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
  if (size < 3 || size > 2 + 65536)
    return -1;

  static EmuBus bus;
  int pc;
  emuLoad(&bus, data, size, &pc);

  fabgl::i8080 cpu;
  cpu.setCallbacks(&bus, emuReadByte, emuWriteByte, emuReadWord, emuWriteWord, emuReadIO, emuWriteIO);
  cpu.reset();
  cpu.setPC(pc);

  for (int i = 0; i < EMU_MAX_STEPS; ++i) {
    cpu.step();
    uint8_t sel = bus.mem[(i * 7) & 0xffff];
    if (sel >= 0xf8)
      cpu.interruptRST(sel & 7);
  }
  return 0;
}
