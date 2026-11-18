// Host definitions for the hardware-bound pieces the fuzzed code links against: the PS/2 controller
// singletons and the DAC/SDL sound generator. Nothing here parses untrusted input.
#include "esp_shim.h"
#include <vector>
#include "comdrivers/ps2controller.h"
#include "devdrivers/soundgen.h"

bool (*g_shimQueueEmptyHook)(void * arg, uint8_t * byteOut) = nullptr;
void * g_shimQueueEmptyArg = nullptr;
void (*g_shimYieldHook)() = nullptr;

namespace fabgl {

void stubsReleaseWaveforms();

Keyboard * PS2Controller::s_keyboard = nullptr;
Mouse * PS2Controller::s_mouse = nullptr;
bool PS2Controller::s_keyboardAllocated = false;
bool PS2Controller::s_mouseAllocated = false;
bool PS2Controller::s_initDone = false;
bool PS2Controller::s_parityError[2] = {false, false};
bool PS2Controller::s_syncError[2] = {false, false};
bool PS2Controller::s_CLKTimeOutError[2] = {false, false};

// Bytes the harness wants the "PS/2 port" to deliver; getData() on an empty feed reports a CLK timeout.
const uint8_t * g_ps2Feed = nullptr;
size_t g_ps2FeedLen = 0;
size_t g_ps2FeedPos = 0;

bool PS2Controller::dataAvailable(int) { return g_ps2FeedPos < g_ps2FeedLen; }
// Past the end of the feed a device that is mid-packet would keep waiting; hand back 0x00 (the PS/2
// "buffer overrun / detection error" byte) so the decoder finishes that packet instead of blocking.
int PS2Controller::getData(int, int) {
  s_CLKTimeOutError[0] = false;
  if (g_ps2FeedPos >= g_ps2FeedLen) { ++g_ps2FeedPos; return 0; }
  return g_ps2Feed[g_ps2FeedPos++];
}
void PS2Controller::sendData(uint8_t, int) {}
void PS2Controller::disableRX(int) {}
void PS2Controller::enableRX(int) {}
bool PS2Controller::lock(int, int) { return true; }
void PS2Controller::unlock(int) {}
void PS2Controller::begin(gpio_num_t, gpio_num_t, gpio_num_t, gpio_num_t) {}

SoundGenerator::SoundGenerator(int sampleRate, gpio_num_t gpio, SoundGenMethod genMethod) { m_sampleRate = sampleRate; m_channels = nullptr; m_play = false; }
SoundGenerator::~SoundGenerator() {}
// No audio device: attached generators are only kept alive (the caller still configures them after
// attach()) and released by the harness between inputs.
static std::vector<WaveformGenerator *> s_attached;
void stubsReleaseWaveforms() { for (auto w : s_attached) delete w; s_attached.clear(); }
void SoundGenerator::attach(WaveformGenerator * value) { s_attached.push_back(value); }
bool SoundGenerator::play(bool value) { m_play = value; return !value; }

#define STUB_GEN(T) \
  T::T() {} \
  void T::setFrequency(int) {} \
  int T::getSample() { return 0; }
STUB_GEN(SineWaveformGenerator)
STUB_GEN(SquareWaveformGenerator)
STUB_GEN(TriangleWaveformGenerator)
STUB_GEN(SawtoothWaveformGenerator)
STUB_GEN(NoiseWaveformGenerator)
STUB_GEN(VICNoiseGenerator)
void SquareWaveformGenerator::setDutyCycle(int) {}

}
