// Fuzzes fabgl::Terminal's input stream: control codes, ESC/CSI/DCS/OSC sequences, VT52 and
// FabGL-extension sequences, plus the legacy terminfo translations selected by the first byte.
// The firmware consumes this stream in a FreeRTOS task; on the host the shim makes the queue a plain
// FIFO and this harness drains it synchronously. A read from the exhausted queue ends the stream.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <deque>
#include <new>

#include "esp_shim.h"

// Terminal::consumeInputQueue() / m_inputQueue are private; the harness drives them directly.
#define private public
#define protected public
#include "terminal.h"
#undef private
#undef protected

#include "fabfonts.h"

namespace fabgl { void stubsReleaseWaveforms(); }

namespace {

using namespace fabgl;

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

Terminal * g_term = nullptr;
// Terminals normally run with the PS/2 keyboard attached (LED and key-report sequences talk to it).
Keyboard g_keyboard;

// The input ran out inside a sequence. The firmware task would block here; instead complete the
// sequence with ST, BEL and CAN terminators, and after a bounded number give up like Terminal::end().
int g_padCount = 0;
bool onQueueEmpty(void *, uint8_t * byteOut) {
  static const uint8_t pad[] = { ASCII_ESC, '\\', ASCII_BEL, ASCII_CAN };
  if (!g_term || g_padCount >= 64) {
    if (g_term) g_term->m_endingState = true;
    return false;
  }
  *byteOut = pad[g_padCount++ % sizeof(pad)];
  return true;
}
void onYield() { if (g_term && g_term->m_inputQueue) g_term->m_inputQueue->items.clear(); }

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
  if (size < 1 || size > 8192)
    return -1;

  g_padCount = 0;
  g_shimQueueEmptyHook = onQueueEmpty;
  g_shimYieldHook = onYield;

  TextCtrl ctrl;
  // Sketches keep the Terminal in zero-initialised static storage; begin() reads some emulation-state
  // members (e.g. originMode) before reset() writes them, so mimic that instead of a bare heap object.
  void * mem = calloc(1, sizeof(Terminal));
  Terminal * term = new (mem) Terminal;
  g_term = term;

  if (!term->begin(&ctrl, 80, 25, &g_keyboard)) {
    g_term = nullptr;
    term->~Terminal();
    free(mem);
    return 0;
  }
  term->m_inputQueue->length = (size_t)-1;
  // like connectLocally() without the keyboard: replies (DA, DSR, ...) land in the output queue
  term->m_outputQueue = xQueueCreate(1, sizeof(uint8_t));
  term->m_outputQueue->length = (size_t)-1;

  // first byte: legacy terminal personality (queued ahead of the data, like the firmware does)
  term->setTerminalType((TermType)(data[0] % 8));

  for (size_t i = 1; i < size; ++i)
    term->write(data[i]);

  while (!term->m_endingState && !term->m_inputQueue->items.empty())
    term->consumeInputQueue();

  term->~Terminal();
  free(mem);
  g_term = nullptr;
  fabgl::stubsReleaseWaveforms();
  return 0;
}
