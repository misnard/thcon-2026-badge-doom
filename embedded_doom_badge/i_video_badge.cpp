#include <Arduino.h>
#include <Wire.h>

extern "C" {
#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "doomstat.h"
#include "i_video.h"
#include "v_video.h"
}

#ifndef OLED_WIDTH
#define OLED_WIDTH 128
#endif

#ifndef OLED_HEIGHT
#define OLED_HEIGHT 64
#endif

#ifndef OLED_DEFAULT_GAMMA
#define OLED_DEFAULT_GAMMA 4
#endif

#ifndef OLED_AREA_SAMPLE
#define OLED_AREA_SAMPLE 1
#endif

static constexpr int OLED_W = OLED_WIDTH;
static constexpr int OLED_H = OLED_HEIGHT;
static_assert(OLED_W == 128, "SSD1306 badge driver expects a 128-pixel-wide panel");
static_assert(OLED_H == 32 || OLED_H == 64, "Set OLED_HEIGHT to 32 or 64");

static constexpr int OLED_PAGES = OLED_H / 8;
static constexpr uint8_t OLED_MUX_RATIO = OLED_H - 1;
static constexpr uint8_t OLED_COM_PINS = (OLED_H == 32) ? 0x02 : 0x12;
static constexpr uint8_t OLED_CONTRAST = (OLED_H == 32) ? 0x8F : 0xCF;
static constexpr uint8_t SSD1306_ADDR_1 = 0x3C;
static constexpr uint8_t SSD1306_ADDR_2 = 0x3D;

enum OledRenderMode {
  OLED_RENDER_ZOOM,
  OLED_RENDER_CROP,
  OLED_RENDER_FIT,
  OLED_RENDER_COUNT
};

struct SourceRect {
  int x;
  int y;
  int w;
  int h;
};

struct Pins {
  int sda;
  int scl;
};

static const Pins pinCandidates[] = {
  {6, 7}, {5, 6}, {4, 5}, {8, 9}, {9, 10}, {10, 11},
  {1, 2}, {2, 3}, {18, 19}, {20, 21}, {21, 22}, {0, 1}
};

static uint8_t fb[OLED_W * OLED_H / 8];
static uint8_t lumaPalette[256];
static uint8_t oledAddr = SSD1306_ADDR_1;
static bool displayReady = false;
static bool defaultGammaApplied = false;
static uint8_t downTicks[256];
static OledRenderMode renderMode = OLED_RENDER_ZOOM;
static int renderYOffset = 0;

static inline void px(int x, int y, bool on) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
  uint16_t i = x + (y >> 3) * OLED_W;
  uint8_t m = 1 << (y & 7);
  if (on) fb[i] |= m;
  else fb[i] &= ~m;
}

static int clampGamma(int gamma) {
  if (gamma < 0) return 0;
  if (gamma > 4) return 4;
  return gamma;
}

static void applyDefaultGamma() {
  if (defaultGammaApplied) return;

  int gamma = clampGamma(OLED_DEFAULT_GAMMA);
  if (usegamma == 0 && gamma > 0) usegamma = gamma;
  defaultGammaApplied = true;
}

static uint8_t rgbToGrayscale(uint8_t r, uint8_t g, uint8_t b) {
  return (299U * r + 587U * g + 114U * b + 500U) / 1000U;
}

static const char *renderModeName(OledRenderMode mode) {
  switch (mode) {
    case OLED_RENDER_ZOOM: return "zoom";
    case OLED_RENDER_CROP: return "crop";
    case OLED_RENDER_FIT: return "fit";
    default: return "unknown";
  }
}

static SourceRect sourceRectForMode(OledRenderMode mode) {
  SourceRect rect = {0, 0, SCREENWIDTH, SCREENHEIGHT};

  if (mode == OLED_RENDER_CROP) {
    rect.h = SCREENWIDTH * OLED_H / OLED_W;
  } else if (mode == OLED_RENDER_ZOOM) {
    rect.w = OLED_W * 2;
    rect.h = OLED_H * 2;
  }

  if (rect.w > SCREENWIDTH) rect.w = SCREENWIDTH;
  if (rect.h > SCREENHEIGHT) rect.h = SCREENHEIGHT;

  rect.x = (SCREENWIDTH - rect.w) / 2;
  rect.y = (SCREENHEIGHT - rect.h) / 2 + renderYOffset;
  if (rect.y < 0) rect.y = 0;
  if (rect.y > SCREENHEIGHT - rect.h) rect.y = SCREENHEIGHT - rect.h;

  return rect;
}

static OledRenderMode effectiveRenderMode() {
  if (gamestate != GS_LEVEL || menuactive || automapactive) return OLED_RENDER_FIT;
  return renderMode;
}

static void clampRenderYOffset() {
  SourceRect rect = sourceRectForMode(renderMode);
  int centeredY = (SCREENHEIGHT - rect.h) / 2;
  int maxOffset = SCREENHEIGHT - rect.h - centeredY;
  int minOffset = -centeredY;

  if (renderYOffset < minOffset) renderYOffset = minOffset;
  if (renderYOffset > maxOffset) renderYOffset = maxOffset;
}

static void cycleRenderMode() {
  renderMode = (OledRenderMode)((renderMode + 1) % OLED_RENDER_COUNT);
  clampRenderYOffset();
  Serial.printf("OLED render: %s\n", renderModeName(renderMode));
}

static void shiftRenderYOffset(int delta) {
  renderYOffset += delta;
  clampRenderYOffset();
  Serial.printf("OLED y offset: %d\n", renderYOffset);
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
    0xAE, 0xD5, 0x80, 0xA8, OLED_MUX_RATIO, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, OLED_COM_PINS,
    0x81, OLED_CONTRAST, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0x2E, 0xAF
  };
  for (uint8_t c : init) oledCmd(c);
}

static void oledPush() {
  constexpr int CHUNK = 16;
  for (int page = 0; page < OLED_PAGES; ++page) {
    oledCmd(0xB0 | page);
    oledCmd(0x00);
    oledCmd(0x10);

    const int pageOffset = page * OLED_W;
    for (int col = 0; col < OLED_W; col += CHUNK) {
      int count = OLED_W - col;
      if (count > CHUNK) count = CHUNK;

      Wire.beginTransmission(oledAddr);
      Wire.write(0x40);
      for (int j = 0; j < count; ++j) Wire.write(fb[pageOffset + col + j]);
      Wire.endTransmission();
    }
  }
}

static const char *doomKeyName(int key) {
  switch (key) {
    case KEY_LEFTARROW: return "left";
    case KEY_RIGHTARROW: return "right";
    case KEY_UPARROW: return "up";
    case KEY_DOWNARROW: return "down";
    case KEY_RCTRL: return "fire";
    case ' ': return "use";
    case KEY_ENTER: return "enter";
    case KEY_ESCAPE: return "escape";
    case KEY_F11: return "gamma";
    default: return "unknown";
  }
}

static void postKey(int key, bool down) {
  Serial.printf("ACTION: %s %s\n", down ? "keydown" : "keyup", doomKeyName(key));

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
    case 'g': case 'G': return KEY_F11;
    case '\r': case '\n': return KEY_ENTER;
    case 27: return KEY_ESCAPE;
    default: return 0;
  }
}

static bool handleDisplayCommand(int c) {
  switch (c) {
    case 'm':
    case 'M':
      cycleRenderMode();
      return true;
    case '[':
      shiftRenderYOffset(-8);
      return true;
    case ']':
      shiftRenderYOffset(8);
      return true;
    default:
      return false;
  }
}

static void handleSerialInput() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (handleDisplayCommand(c)) continue;

    int key = serialKeyToDoom(c);
    if (!key) continue;
    if (!downTicks[(uint8_t)key]) postKey(key, true);
    downTicks[(uint8_t)key] = 4;
  }

  for (int i = 0; i < 256; ++i) {
    if (downTicks[i] && --downTicks[i] == 0) postKey(i, false);
  }
}

#if OLED_AREA_SAMPLE
static int scaledLumaAt(const byte *screen, const SourceRect &rect, int x, int y) {
  int sx0 = rect.x + x * rect.w / OLED_W;
  int sx1 = rect.x + (x + 1) * rect.w / OLED_W;
  int sy0 = rect.y + y * rect.h / OLED_H;
  int sy1 = rect.y + (y + 1) * rect.h / OLED_H;

  if (sx1 <= sx0) sx1 = sx0 + 1;
  if (sy1 <= sy0) sy1 = sy0 + 1;
  if (sx1 > SCREENWIDTH) sx1 = SCREENWIDTH;
  if (sy1 > SCREENHEIGHT) sy1 = SCREENHEIGHT;

  int sum = 0;
  int count = 0;
  for (int sy = sy0; sy < sy1; ++sy) {
    const byte *src = screen + sy * SCREENWIDTH;
    for (int sx = sx0; sx < sx1; ++sx) {
      sum += lumaPalette[src[sx]];
      ++count;
    }
  }

  return (sum + count / 2) / count;
}
#else
static int scaledLumaAt(const byte *screen, const SourceRect &rect, int x, int y) {
  int sx = rect.x + x * rect.w / OLED_W;
  int sy = rect.y + y * rect.h / OLED_H;
  return lumaPalette[screen[sy * SCREENWIDTH + sx]];
}
#endif

static void renderFrameToOled() {
  static const uint8_t bayer8[8][8] = {
    { 0, 32,  8, 40,  2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44,  4, 36, 14, 46,  6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43,  1, 33,  9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47,  7, 39, 13, 45,  5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21}
  };

  memset(fb, 0, sizeof(fb));
  const byte *screen = screens[0];
  SourceRect rect = sourceRectForMode(effectiveRenderMode());

  for (int y = 0; y < OLED_H; ++y) {
    for (int x = 0; x < OLED_W; ++x) {
      int lum = scaledLumaAt(screen, rect, x, y);
      int threshold = bayer8[y & 7][x & 7] * 4 + 2;
      px(x, y, lum > threshold);
    }
  }
}

extern "C" void I_SetPalette(byte *palette) {
  applyDefaultGamma();

  const byte *gamma = gammatable[clampGamma(usegamma)];
  for (int i = 0; i < 256; ++i) {
    const byte *rgb = palette + i * 3;
    lumaPalette[i] = rgbToGrayscale(gamma[rgb[0]], gamma[rgb[1]], gamma[rgb[2]]);
  }
}

extern "C" void I_UpdateNoBlit(void) {
}

extern "C" void I_InitGraphics(void) {
  applyDefaultGamma();

  displayReady = findDisplay();
  if (displayReady) {
    Serial.printf("SSD1306 geometry %dx%d\n", OLED_W, OLED_H);
    Serial.printf("OLED render: %s\n", renderModeName(renderMode));
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
