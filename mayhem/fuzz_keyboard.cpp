// Fuzzes the PS/2 keyboard scancode decoder: set-2 scancodes (make/break, E0/E1 prefixes, Pause,
// PrintScreen) -> VirtualKeyItem through each national layout (dead keys, AltGr), plus the set-1
// conversion done when the application reads keys back.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>
#include <deque>

#include "esp_shim.h"

#define private public
#define protected public
#include "devdrivers/keyboard.h"
#undef private
#undef protected

#include "devdrivers/kbdlayouts.h"

namespace fabgl {
extern const uint8_t * g_ps2Feed;
extern size_t g_ps2FeedLen;
extern size_t g_ps2FeedPos;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
  using namespace fabgl;

  if (size < 2 || size > 4096)
    return -1;

  // zero-initialised like a global Keyboard in a sketch
  static Keyboard kbd;

  kbd.enableVirtualKeys(true, true);
  memset(kbd.m_VKMap, 0, sizeof(kbd.m_VKMap));
  kbd.m_CTRL = kbd.m_LALT = kbd.m_RALT = kbd.m_SHIFT = kbd.m_CAPSLOCK = kbd.m_GUI = false;
  kbd.m_NUMLOCK = kbd.m_SCROLLLOCK = false;
  kbd.m_lastDeadKey = VK_NONE;
  kbd.setLayout(SupportedLayouts::layouts()[data[0] % SupportedLayouts::count()]);
  kbd.m_scancodeSet = 1 + (data[1] & 1);

  g_ps2Feed = data + 2;
  g_ps2FeedLen = size - 2;
  g_ps2FeedPos = 0;

  VirtualKeyItem item;
  while (g_ps2FeedPos < g_ps2FeedLen) {
    if (kbd.blockingGetVirtualKey(&item))
      kbd.postVirtualKeyItem(item);
    while (kbd.getNextVirtualKey(&item, 0)) {
    }
  }

  kbd.enableVirtualKeys(false, false);
  return 0;
}
