// Shared scaffolding for the CPU-emulator harnesses: a 64 KiB flat memory image that the fuzz input
// fills, with deterministic I/O ports. Callbacks mask addresses to 16 bits like a real machine's bus.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct EmuBus {
  uint8_t mem[65536];
  uint32_t ioCounter;
};

static inline int emuReadByte(void * ctx, int addr) { return ((EmuBus *)ctx)->mem[addr & 0xffff]; }
static inline void emuWriteByte(void * ctx, int addr, int value) { ((EmuBus *)ctx)->mem[addr & 0xffff] = (uint8_t)value; }
static inline int emuReadWord(void * ctx, int addr) { return emuReadByte(ctx, addr) | (emuReadByte(ctx, addr + 1) << 8); }
static inline void emuWriteWord(void * ctx, int addr, int value) { emuWriteByte(ctx, addr, value); emuWriteByte(ctx, addr + 1, value >> 8); }
static inline int emuReadIO(void * ctx, int addr) { return (addr ^ (((EmuBus *)ctx)->ioCounter++ * 31)) & 0xff; }
static inline void emuWriteIO(void *, int, int) {}

// input layout: [pc_lo, pc_hi, memory image...]
static inline void emuLoad(EmuBus * bus, const uint8_t * data, size_t size, int * pc) {
  memset(bus, 0, sizeof(*bus));
  *pc = data[0] | (data[1] << 8);
  size_t n = size - 2 < sizeof(bus->mem) ? size - 2 : sizeof(bus->mem);
  memcpy(bus->mem, data + 2, n);
}

static const int EMU_MAX_STEPS = 20000;
