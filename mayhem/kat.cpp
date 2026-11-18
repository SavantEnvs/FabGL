// Known-answer probe for mayhem/test.sh: runs fixed inputs through the real FabGL code (CPU emulators,
// PS/2 scancode decoder, terminal escape-sequence engine) and prints the computed values, which
// test.sh compares against the expected strings. Built with the project's normal flags, dynamically
// linked, so a neutered (_exit(0)) program cannot produce the expected output.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <deque>
#include <new>

#include "esp_shim.h"

#define private public
#define protected public
#include "terminal.h"
#include "devdrivers/keyboard.h"
#undef private
#undef protected

#include "devdrivers/kbdlayouts.h"
#include "emudevs/Z80.h"
#include "emudevs/i8080.h"
#include "emudevs/MOS6502.h"
#include "fabfonts.h"
#include "emu_common.h"

namespace fabgl {
extern const uint8_t * g_ps2Feed;
extern size_t g_ps2FeedLen;
extern size_t g_ps2FeedPos;
}

using namespace fabgl;

static void load(EmuBus * bus, uint16_t at, std::initializer_list<uint8_t> code)
{
  memset(bus, 0, sizeof(*bus));
  int i = 0;
  for (uint8_t b : code)
    bus->mem[at + i++] = b;
}

static void katZ80()
{
  static EmuBus bus;
  // LD A,5 ; LD B,7 ; ADD A,B ; LD (0x8000),A ; HALT
  load(&bus, 0x100, { 0x3E, 0x05, 0x06, 0x07, 0x80, 0x32, 0x00, 0x80, 0x76 });
  Z80 cpu;
  cpu.setCallbacks(&bus, emuReadByte, emuWriteByte, emuReadWord, emuWriteWord, emuReadIO, emuWriteIO);
  cpu.reset();
  cpu.setPC(0x100);
  for (int i = 0; i < 100 && !cpu.getStatus(); ++i)
    cpu.step();
  printf("z80 a=%d b=%d mem=%d halted=%d\n", cpu.readRegByte(Z80_A), cpu.readRegByte(Z80_B), bus.mem[0x8000], cpu.getStatus() != 0);
}

static void katI8080()
{
  static EmuBus bus;
  // MVI A,0x25 ; ADI 0x17 ; STA 0x8000 ; MOV B,A ; HLT
  load(&bus, 0x200, { 0x3E, 0x25, 0xC6, 0x17, 0x32, 0x00, 0x80, 0x47, 0x76 });
  i8080 cpu;
  cpu.setCallbacks(&bus, emuReadByte, emuWriteByte, emuReadWord, emuWriteWord, emuReadIO, emuWriteIO);
  cpu.reset();
  cpu.setPC(0x200);
  for (int i = 0; i < 4; ++i)
    cpu.step();
  printf("i8080 a=%d b=%d mem=%d\n", cpu.regs_a(), cpu.regs_b(), bus.mem[0x8000]);
}

static void katMOS6502()
{
  static EmuBus bus;
  // CLC ; LDA #$12 ; ADC #$30 ; STA $0300 ; SED ; CLC ; LDA #$19 ; ADC #$01 ; STA $0301
  load(&bus, 0x400, { 0x18, 0xA9, 0x12, 0x69, 0x30, 0x8D, 0x00, 0x03, 0xF8, 0x18, 0xA9, 0x19, 0x69, 0x01, 0x8D, 0x01, 0x03 });
  MOS6502 cpu;
  cpu.setCallbacks(&bus, emuReadByte, emuWriteByte, emuReadByte, emuWriteByte, emuReadByte, emuWriteByte);
  cpu.reset();
  cpu.setPC(0x400);
  for (int i = 0; i < 9; ++i)
    cpu.step();
  printf("mos6502 bin=%d bcd=%d pc=%d\n", bus.mem[0x300], bus.mem[0x301], cpu.getPC());
}

static void katKeyboard()
{
  static Keyboard kbd;
  kbd.enableVirtualKeys(true, true);
  kbd.setLayout(&USLayout);
  kbd.m_scancodeSet = 2;
  // Left shift down, 'A' make, 'A' break, left shift up
  static const uint8_t feed[] = { 0x12, 0x1C, 0xF0, 0x1C, 0xF0, 0x12 };
  g_ps2Feed = feed;
  g_ps2FeedLen = sizeof(feed);
  g_ps2FeedPos = 0;
  VirtualKeyItem item;
  while (g_ps2FeedPos < g_ps2FeedLen) {
    if (kbd.blockingGetVirtualKey(&item))
      kbd.postVirtualKeyItem(item);
  }
  while (kbd.getNextVirtualKey(&item, 0))
    printf("key vk=%d down=%d ascii=%d shift=%d\n", (int)item.vk, item.down, item.ASCII, item.SHIFT);
}

namespace {
class TextCtrl : public TextualDisplayController {
public:
  TextCtrl() { m_screenWidth = 640; m_screenHeight = 400; m_viewPortWidth = 640; m_viewPortHeight = 400; }
  void setResolution(char const *, int, int, bool) override {}
  void begin() override {}
  int colorsCount() override { return 16; }
  int getColumns() override { return 80; }
  int getRows() override { return 25; }
  void adjustMapSize(int * columns, int * rows) override {
    if (*columns > 80) *columns = 80;
    if (*rows > 25) *rows = 25;
  }
  void setTextMap(uint32_t const *, int) override {}
  void enableCursor(bool) override {}
  void setCursorPos(int, int) override {}
  void setCursorForeground(Color) override {}
  void setCursorBackground(Color) override {}
  FontInfo const * getFont() override { return &FONT_8x16; }
};
}

static Keyboard g_termKeyboard;
// Terminal::end() spins until its task drains the queue; there is no task here, so drain on yield.
static Terminal * g_term;
static void drainOnYield() { if (g_term && g_term->m_inputQueue) g_term->m_inputQueue->items.clear(); }

static void feedTerminal(Terminal * term, const char * s)
{
  for (; *s; ++s)
    term->write((uint8_t)*s);
  while (!term->m_inputQueue->items.empty())
    term->consumeInputQueue();
}

static void katTerminal()
{
  TextCtrl ctrl;
  void * mem = calloc(1, sizeof(Terminal));
  Terminal * term = new (mem) Terminal;
  g_term = term;
  g_shimYieldHook = drainOnYield;
  term->begin(&ctrl, 80, 25, &g_termKeyboard);
  term->m_inputQueue->length = (size_t)-1;
  term->m_outputQueue = xQueueCreate(1, sizeof(uint8_t));
  term->m_outputQueue->length = (size_t)-1;

  feedTerminal(term, "AB\r\nC");
  uint32_t const * map = term->m_glyphsBuffer.map;
  int cols = term->m_glyphsBuffer.columns;
  int col, row;
  col = term->m_emuState.cursorX; row = term->m_emuState.cursorY;
  printf("term text=%c%c%c cursor=%d,%d\n", glyphMapItem_getIndex(map[0]), glyphMapItem_getIndex(map[1]), glyphMapItem_getIndex(map[cols]), col, row);

  feedTerminal(term, "\x1b[5;10H" "Z");
  col = term->m_emuState.cursorX; row = term->m_emuState.cursorY;
  printf("term cup glyph=%c at(10,5) cursor=%d,%d\n", glyphMapItem_getIndex(map[4 * cols + 9]), col, row);

  // DA1 request: the terminal answers on its output queue
  feedTerminal(term, "\x1b[c");
  std::string reply;
  uint8_t b;
  while (xQueueReceive(term->m_outputQueue, &b, 0))
    reply.push_back((char)b);
  printf("term da1 reply=%zu bytes first=%d\n", reply.size(), reply.empty() ? -1 : (int)(uint8_t)reply[0]);

  term->~Terminal();
  g_term = nullptr;
  free(mem);
}

int main()
{
  setvbuf(stdout, nullptr, _IOLBF, 0);
  katZ80();
  katI8080();
  katMOS6502();
  katKeyboard();
  katTerminal();
  return 0;
}
