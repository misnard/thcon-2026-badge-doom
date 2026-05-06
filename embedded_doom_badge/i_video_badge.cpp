#include <Arduino.h>
#include <Wire.h>

extern "C" {
#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "v_video.h"
}

static constexpr int OLED_W = 128;
static constexpr int OLED_H = 64;
static constexpr uint8_t SSD1306_ADDR_1 = 0x3C;
static constexpr uint8_t SSD1306_ADDR_2 = 0x3D;

struct Pins {
  int sda;
  int scl;
};

static const Pins pinCandidates[] = {
  {6, 7}, {5, 6}, {4, 5}, {8, 9}, {9, 10}, {10, 11},
  {1, 2}, {2, 3}, {18, 19}, {20, 21}, {21, 22}, {0, 1}
};

static uint8_t fb[OLED_W * OLED_H / 8];
static uint8_t lpalette[256 * 3];
static uint8_t oledAddr = SSD1306_ADDR_1;
static bool displayReady = false;
static uint8_t downTicks[256];

static inline void px(int x, int y, bool on) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
  uint16_t i = x + (y >> 3) * OLED_W;
  uint8_t m = 1 << (y & 7);
  if (on) fb[i] |= m;
  else fb[i] &= ~m;
}

static bool i2cAddressResponds(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool findDisplay() {
  for (const Pins &p : pinCandidates) {
    Wire.end();
    delay(5);
    Wire.begin(p.sda, p.scl);
    Wire.setClock(400000);
    delay(20);
    if (i2cAddressResponds(SSD1306_ADDR_1)) {
      oledAddr = SSD1306_ADDR_1;
      Serial.printf("SSD1306 at 0x%02x SDA=%d SCL=%d\n", oledAddr, p.sda, p.scl);
      return true;
    }
    if (i2cAddressResponds(SSD1306_ADDR_2)) {
      oledAddr = SSD1306_ADDR_2;
      Serial.printf("SSD1306 at 0x%02x SDA=%d SCL=%d\n", oledAddr, p.sda, p.scl);
      return true;
    }
  }
  return false;
}

static void oledCmd(uint8_t c) {
  Wire.beginTransmission(oledAddr);
  Wire.write(0x00);
  Wire.write(c);
  Wire.endTransmission();
}

static void oledInit() {
  static const uint8_t init[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0x2E, 0xAF
  };
  for (uint8_t c : init) oledCmd(c);
}

static void oledPush() {
  oledCmd(0x21);
  oledCmd(0);
  oledCmd(OLED_W - 1);
  oledCmd(0x22);
  oledCmd(0);
  oledCmd(7);

  constexpr int CHUNK = 16;
  for (int i = 0; i < (int)sizeof(fb); i += CHUNK) {
    Wire.beginTransmission(oledAddr);
    Wire.write(0x40);
    for (int j = 0; j < CHUNK; ++j) Wire.write(fb[i + j]);
    Wire.endTransmission();
  }
}

static void postKey(int key, bool down) {
  event_t event;
  event.type = down ? ev_keydown : ev_keyup;
  event.data1 = key;
  event.data2 = 0;
  event.data3 = 0;
  D_PostEvent(&event);
}

static int serialKeyToDoom(int c) {
  switch (c) {
    case 'a': case 'A': return KEY_LEFTARROW;
    case 'd': case 'D': return KEY_RIGHTARROW;
    case 'w': case 'W': return KEY_UPARROW;
    case 's': case 'S': return KEY_DOWNARROW;
    case 'f': case 'F': return KEY_RCTRL;
    case 'e': case 'E': return ' ';
    case '\r': case '\n': return KEY_ENTER;
    case 27: return KEY_ESCAPE;
    default: return 0;
  }
}

static void handleSerialInput() {
  while (Serial.available() > 0) {
    int key = serialKeyToDoom(Serial.read());
    if (!key) continue;
    if (!downTicks[(uint8_t)key]) postKey(key, true);
    downTicks[(uint8_t)key] = 4;
  }

  for (int i = 0; i < 256; ++i) {
    if (downTicks[i] && --downTicks[i] == 0) postKey(i, false);
  }
}

static void renderFrameToOled() {
  static const uint8_t bayer4[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5}
  };

  memset(fb, 0, sizeof(fb));
  const byte *screen = screens[0];

  for (int y = 0; y < OLED_H; ++y) {
    int sy = y * SCREENHEIGHT / OLED_H;
    const byte *src = screen + sy * SCREENWIDTH;
    for (int x = 0; x < OLED_W; ++x) {
      int sx = x * SCREENWIDTH / OLED_W;
      int col = src[sx] * 3;
      int lum = (lpalette[col] * 30 + lpalette[col + 1] * 59 + lpalette[col + 2] * 11) / 100;
      int threshold = bayer4[y & 3][x & 3] * 16 + 8;
      px(x, y, lum > threshold);
    }
  }
}

extern "C" void I_SetPalette(byte *palette) {
  memcpy(lpalette, palette, sizeof(lpalette));
}

extern "C" void I_UpdateNoBlit(void) {
}

extern "C" void I_InitGraphics(void) {
  displayReady = findDisplay();
  if (displayReady) {
    oledInit();
    memset(fb, 0, sizeof(fb));
    oledPush();
  } else {
    Serial.println("No SSD1306 display found");
  }
}

extern "C" void I_StartTic(void) {
  handleSerialInput();
}

extern "C" void I_ReadScreen(byte *scr) {
  memcpy(scr, screens[0], SCREENWIDTH * SCREENHEIGHT);
}

extern "C" void I_StartFrame(void) {
}

extern "C" void I_ShutdownGraphics(void) {
}

extern "C" void I_FinishUpdate(void) {
  if (!displayReady) return;
  renderFrameToOled();
  oledPush();
}
