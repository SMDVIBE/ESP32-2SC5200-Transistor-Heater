/*
  ESP32-C3 2SC5200 + ST7789 170x320
  DISPLAY VERSION WITHOUT TFT_eSPI

  The previous TFT_eSPI version crashes before setup().
  Therefore this version uses the ESP32 hardware SPI directly.

  ESP32:
    GPIO5  = heater PWM
    GPIO0  = MCP6002 U3B pin 7 / old Arduino A1
    GPIO3  = MCP6002 U3A pin 1 / old Arduino A0

  ST7789:
    SCLK = GPIO4
    MOSI = GPIO6
    DC   = GPIO7
    RST  = GPIO1
    CS   = GPIO10

  IMPORTANT:
    Control algorithm is unchanged from the last working ESP32 test.
    Only the display driver has been changed.
*/

#include <Arduino.h>
#include <SPI.h>

// ---------------- Heater pins ----------------
#define PIN_PWM_BASE        5
#define PIN_SENS_BASE       0
#define PIN_SENS_CURRENT    3

// ---------------- Temperature buttons ----------------
#define PIN_BUTTON_UP       21
#define PIN_BUTTON_DOWN     20
#define TEMP_MIN_C          150.0f
#define TEMP_MAX_C          250.0f
#define TEMP_STEP_C         5.0f

// ---------------- ST7789 pins ----------------
#define TFT_SCLK  4
#define TFT_MOSI  6
#define TFT_DC    7
#define TFT_RST   1
#define TFT_CS    10

#define TFT_W 320
#define TFT_H 170

SPIClass *tftSPI = &SPI;

// ============================================================
// Minimal ST7789 driver
// ============================================================

// UI semantic colors
const uint16_t STATUS_RED   = 0xF800;
const uint16_t STATUS_GREEN = 0x07E0;
const uint16_t STATUS_WHITE = 0xFFFF;
const uint16_t POWER_GREEN  = 0x07E0;
const uint16_t POWER_YELLOW = 0xFFE0;
const uint16_t POWER_RED    = 0xF800;

void tftCmd(uint8_t cmd)
{
  digitalWrite(TFT_DC, LOW);
  digitalWrite(TFT_CS, LOW);
  tftSPI->transfer(cmd);
  digitalWrite(TFT_CS, HIGH);
}

void tftData(const uint8_t *data, size_t len)
{
  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
  tftSPI->writeBytes(data, len);
  digitalWrite(TFT_CS, HIGH);
}

void tftData8(uint8_t data)
{
  tftData(&data, 1);
}

void tftReset()
{
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(20);
  digitalWrite(TFT_RST, LOW);
  delay(50);
  digitalWrite(TFT_RST, HIGH);
  delay(120);
}

void tftInit()
{
  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_DC, HIGH);

  tftSPI->begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tftSPI->beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));

  tftReset();

  tftCmd(0x01); // SWRESET
  delay(150);

  tftCmd(0x11); // SLPOUT
  delay(120);

  // COLMOD = 16-bit RGB565
  tftCmd(0x3A);
  tftData8(0x55);

  // MADCTL: 90-degree landscape orientation
  tftCmd(0x36);
  tftData8(0x60);

  // Porch control
  tftCmd(0xB2);
  {
    uint8_t d[] = {0x0C,0x0C,0x00,0x33,0x33};
    tftData(d, sizeof(d));
  }

  tftCmd(0xB7);
  tftData8(0x35);

  tftCmd(0xBB);
  tftData8(0x19);

  tftCmd(0xC0);
  tftData8(0x2C);

  tftCmd(0xC2);
  tftData8(0x01);

  tftCmd(0xC3);
  tftData8(0x12);

  tftCmd(0xC4);
  tftData8(0x20);

  tftCmd(0xC6);
  tftData8(0x0F);

  tftCmd(0xD0);
  {
    uint8_t d[] = {0xA4,0xA1};
    tftData(d, sizeof(d));
  }

  tftCmd(0xE0);
  {
    uint8_t d[] = {
      0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,
      0x4C,0x18,0x0D,0x0B,0x1F,0x23
    };
    tftData(d, sizeof(d));
  }

  tftCmd(0xE1);
  {
    uint8_t d[] = {
      0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,
      0x51,0x2F,0x1F,0x1F,0x20,0x23
    };
    tftData(d, sizeof(d));
  }

  tftCmd(0x21); // INVON
  delay(10);

  tftCmd(0x13); // NORON
  delay(10);

  tftCmd(0x29); // DISPON
  delay(120);

  tftSPI->endTransaction();
}

void setWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  // 320x170 landscape after MADCTL rotation.
  // 35-pixel Y offset is used by this 170x320 ST7789 module.
  const uint16_t XOFF = 0;
  const uint16_t YOFF = 35;

  x0 += XOFF;
  x1 += XOFF;
  y0 += YOFF;
  y1 += YOFF;

  tftSPI->beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));

  tftCmd(0x2A);
  uint8_t xa[] = {
    uint8_t(x0 >> 8), uint8_t(x0),
    uint8_t(x1 >> 8), uint8_t(x1)
  };
  tftData(xa, 4);

  tftCmd(0x2B);
  uint8_t ya[] = {
    uint8_t(y0 >> 8), uint8_t(y0),
    uint8_t(y1 >> 8), uint8_t(y1)
  };
  tftData(ya, 4);

  tftCmd(0x2C);

  digitalWrite(TFT_DC, HIGH);
  digitalWrite(TFT_CS, LOW);
}
void endWrite()
{
  digitalWrite(TFT_CS, HIGH);
  tftSPI->endTransaction();
}

void fillScreen(uint16_t color)
{
  setWindow(0, 0, TFT_W - 1, TFT_H - 1);

  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;

  static uint8_t buf[256];
  for (int i = 0; i < 256; i += 2) {
    buf[i] = hi;
    buf[i + 1] = lo;
  }

  uint32_t pixels = (uint32_t)TFT_W * TFT_H;

  while (pixels) {
    uint16_t n = pixels > 128 ? 128 : pixels;
    tftSPI->writeBytes(buf, n * 2);
    pixels -= n;
  }

  endWrite();
}

void fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  if (x >= TFT_W || y >= TFT_H) return;
  if (x + w > TFT_W) w = TFT_W - x;
  if (y + h > TFT_H) h = TFT_H - y;

  setWindow(x, y, x + w - 1, y + h - 1);

  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;

  uint8_t buf[128];
  for (int i = 0; i < 128; i += 2) {
    buf[i] = hi;
    buf[i + 1] = lo;
  }

  uint32_t pixels = (uint32_t)w * h;

  while (pixels) {
    uint16_t n = pixels > 64 ? 64 : pixels;
    tftSPI->writeBytes(buf, n * 2);
    pixels -= n;
  }

  endWrite();
}

// ============================================================
// ============================================================
// ============================================================
// COMPACT LANDSCAPE UI: 320 x 170
// ============================================================
//
// Only these values are shown:
//
//   SET       TEMP       CURRENT
//   200°C     146°C      4.02A
//
// Labels use a complete 5x7 ASCII font for every required letter.
// Degree sign is drawn separately so it is always visible.
// ============================================================

struct Glyph {
  char c;
  uint8_t p[5];
};

const Glyph glyphs[] PROGMEM = {
  {'A',{0x7E,0x11,0x11,0x11,0x7E}},
  {'B',{0x7F,0x49,0x49,0x49,0x36}},
  {'C',{0x3E,0x41,0x41,0x41,0x22}},
  {'D',{0x7F,0x41,0x41,0x22,0x1C}},
  {'E',{0x7F,0x49,0x49,0x49,0x41}},
  {'G',{0x3E,0x41,0x49,0x49,0x7A}},
  {'H',{0x7F,0x08,0x08,0x08,0x7F}},
  {'I',{0x00,0x41,0x7F,0x41,0x00}},
  {'I',{0x00,0x41,0x7F,0x41,0x00}},
  {'L',{0x7F,0x40,0x40,0x40,0x40}},
  {'M',{0x7F,0x02,0x0C,0x02,0x7F}},
  {'N',{0x7F,0x06,0x18,0x60,0x7F}},
  {'O',{0x3E,0x41,0x41,0x41,0x3E}},
  {'P',{0x7F,0x09,0x09,0x09,0x06}},
  {'R',{0x7F,0x09,0x19,0x29,0x46}},
  {'S',{0x46,0x49,0x49,0x49,0x31}},
  {'T',{0x01,0x01,0x7F,0x01,0x01}},
  {'U',{0x3F,0x40,0x40,0x40,0x3F}},
  {'V',{0x1F,0x20,0x40,0x20,0x1F}},
  {'W',{0x3F,0x40,0x30,0x40,0x3F}},
  {'X',{0x63,0x14,0x08,0x14,0x63}},
};

const uint8_t digits5x7[10][5] PROGMEM = {
  {0x3E,0x51,0x49,0x45,0x3E}, // 0
  {0x00,0x42,0x7F,0x40,0x00}, // 1
  {0x42,0x61,0x51,0x49,0x46}, // 2
  {0x21,0x41,0x45,0x4B,0x31}, // 3
  {0x18,0x14,0x12,0x7F,0x10}, // 4
  {0x27,0x45,0x45,0x45,0x39}, // 5
  {0x3C,0x4A,0x49,0x49,0x30}, // 6
  {0x01,0x71,0x09,0x05,0x03}, // 7
  {0x36,0x49,0x49,0x49,0x36}, // 8
  {0x06,0x49,0x49,0x29,0x1E}  // 9
};

const uint8_t* findGlyph(char c)
{
  for (uint8_t i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); i++) {
    if ((char)pgm_read_byte(&glyphs[i].c) == c)
      return glyphs[i].p;
  }
  return nullptr;
}

void drawGlyph(uint16_t x, uint16_t y, char c,
               uint8_t scale, uint16_t color)
{
  if (c >= '0' && c <= '9') {
    const uint8_t *g = digits5x7[c - '0'];

    for (uint8_t col = 0; col < 5; col++) {
      uint8_t bits = pgm_read_byte(&g[col]);
      for (uint8_t row = 0; row < 7; row++) {
        if (bits & (1 << row))
          fillRect(x + col * scale, y + row * scale,
                   scale, scale, color);
      }
    }
    return;
  }

  const uint8_t *g = findGlyph(c);
  if (!g) return;

  for (uint8_t col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(&g[col]);
    for (uint8_t row = 0; row < 7; row++) {
      if (bits & (1 << row))
        fillRect(x + col * scale, y + row * scale,
                 scale, scale, color);
    }
  }
}

void drawText(const char *s, uint16_t x, uint16_t y,
              uint8_t scale, uint16_t color)
{
  uint16_t step = 6 * scale;

  while (*s) {
    drawGlyph(x, y, *s, scale, color);
    x += step;
    s++;
  }
}

void drawDigits(const char *s, uint16_t x, uint16_t y,
                uint8_t scale, uint16_t color)
{
  uint16_t step = 6 * scale;

  while (*s) {
    if (*s >= '0' && *s <= '9')
      drawGlyph(x, y, *s, scale, color);

    x += step;
    s++;
  }
}

void drawDegree(uint16_t x, uint16_t y, uint16_t color)
{
  // Larger degree symbol: 9x9 px.
  fillRect(x + 2, y,     5, 2, color);
  fillRect(x,     y + 2, 2, 5, color);
  fillRect(x + 7, y + 2, 2, 5, color);
  fillRect(x + 2, y + 7, 5, 2, color);
}

void drawTempValue(int value, uint16_t x, uint16_t y, uint16_t color)
{
  char s[8];
  snprintf(s, sizeof(s), "%d", value);

  drawDigits(s, x, y, 3, color);

  // Compact temperature: 200°C.
  drawDegree(x + 58, y + 1, color);
  drawText("C", x + 70, y + 1, 2, color);
}

void drawLargeTempValue(int value, uint16_t x, uint16_t y,
                        uint16_t color)
{
  char s[8];
  snprintf(s, sizeof(s), "%d", value);

  // Scale 4: three digits = 72 px.
  drawDigits(s, x, y, 4, color);

  // Complete group width is about 99 px, so it stays
  // safely inside the widened center column (93..227).
  drawDegree(x + 77, y + 3, color);
  drawText("C", x + 89, y + 6, 2, color);
}

void renderCurrentValue(float currentA, uint16_t x, uint16_t y,
                      uint16_t color)
{
  int hundredths = (int)(currentA * 100.0f + 0.5f);
  if (hundredths < 0) hundredths = 0;
  if (hundredths > 999) hundredths = 999;

  int whole = hundredths / 100;
  int frac = hundredths % 100;

  char s[4];

  snprintf(s, sizeof(s), "%d", whole);
  drawDigits(s, x, y, 3, color);

  // Decimal point.
  fillRect(x + 24, y + 18, 4, 4, color);

  snprintf(s, sizeof(s), "%02d", frac);
  drawDigits(s, x + 32, y, 2, color);

  drawText("A", x + 69, y + 2, 2, color);
}// ============================================================
// ============================================================
// STATIC + NON-BLINKING LANDSCAPE UI
// ============================================================
//
// Screen: 320 x 170
//
// Column 1: x = 0..93
// Column 2: x = 96..229
// Column 3: x = 232..319
//
// All labels and values are positioned from the CENTER of their
// column, so changing a value does not move it onto a divider.
// ============================================================

extern float uBaseReal;

bool displayInitialized = false;

int shownSetTemp = -1000;
int shownCurrentTemp = -1000;
int shownCurrentHundredths = -1000;
int shownPWM = -1;
int shownStatus = -1;
int shownUbeMilliV = -1;
uint32_t lastCurrentDisplay = 0;

const uint16_t GREEN  = 0x07E0;
const uint16_t YELLOW = 0xFFE0;
const uint16_t CYAN   = 0x07FF;

// Exact centers between the divider lines.
const int CENTER_SET     = 46;
const int CENTER_TEMP    = 162;
const int CENTER_CURRENT = 275;

// Widths of our fixed-size pixel fonts.
const int LABEL_SCALE = 2;
const int VALUE_SCALE = 3;
const int TEMP_SCALE  = 4;

int textWidth5x7(const char *s, uint8_t scale)
{
  int n = strlen(s);
  if (n <= 0) return 0;
  return n * 6 * scale - scale; // last character has no trailing gap
}

int tempValueWidth(int scale)
{
  // 3 digits + degree + C.
  // 3 digits: 15*scale? Actual group is 3*6*scale = 18*scale
  // Degree starts after 18*scale + 4 px gap and is 7 px wide.
  // C starts at x+69 for compact or x+88 for large.
  if (scale == 4)
    return 99; // exact large 200°C group
  return 75;   // compact 200°C group
}

int currentValueWidth()
{
  // 4.02A at current renderer:
  // whole digit block + decimal + 2 fractional digits + A.
  return 79;
}

void drawCenteredText(const char *s, int centerX, int y,
                      uint8_t scale, uint16_t color)
{
  int w = textWidth5x7(s, scale);
  int x = centerX - w / 2;
  drawText(s, x, y, scale, color);
}

void drawPowerBar(int pwm)
{
  if (pwm < 0) pwm = 0;
  if (pwm > 192) pwm = 192;

  const int barX = 28;
  const int barY = 153;
  const int barW = 264;
  const int barH = 12;
  const int PWM_DISPLAY_MAX = 192;

  int innerW = barW - 4;
  int fillW = (innerW * pwm + PWM_DISPLAY_MAX / 2)
              / PWM_DISPLAY_MAX;

  static int lastFillW = -1;
  static uint16_t lastPowerColor = 0;

  // Green < 50%, yellow 50..80%, red > 80%.
  uint16_t powerColor;
  if (pwm < 96)
    powerColor = POWER_GREEN;
  else if (pwm < 154)
    powerColor = POWER_YELLOW;
  else
    powerColor = POWER_RED;

  // Draw the frame only once.
  if (lastFillW < 0) {
    fillRect(barX, barY, barW, 2, CYAN);
    fillRect(barX, barY + barH - 2, barW, 2, CYAN);
    fillRect(barX, barY, 2, barH, CYAN);
    fillRect(barX + barW - 2, barY, 2, barH, CYAN);
    fillRect(barX + 2, barY + 2, innerW, barH - 4, 0x0000);

    lastFillW = 0;
    lastPowerColor = powerColor;
  }

  // If the color threshold changed, first clear the whole inner bar.
  // This is important when PWM is decreasing: otherwise pixels from the
  // previous (yellow/red) bar can remain visible to the right.
  if (powerColor != lastPowerColor) {
    fillRect(barX + 2, barY + 2, innerW, barH - 4, 0x0000);

    if (fillW > 0) {
      fillRect(barX + 2, barY + 2, fillW, barH - 4, powerColor);
    }

    lastPowerColor = powerColor;
    lastFillW = fillW;
    return;
  }

  // Increase: draw only the newly added part.
  if (fillW > lastFillW) {
    fillRect(barX + 2 + lastFillW, barY + 2,
             fillW - lastFillW, barH - 4, powerColor);
  }
  // Decrease: erase only the removed part.
  else if (fillW < lastFillW) {
    fillRect(barX + 2 + fillW, barY + 2,
             lastFillW - fillW, barH - 4, 0x0000);
  }

  lastFillW = fillW;
  lastPowerColor = powerColor;
}

void drawPowerLabels()
{
  // Small labels above the beginning/end of the power bar.
  drawText("MIN", 28, 139, 1, CYAN);
  drawCenteredText("POWER", 160, 139, 1, CYAN);
  drawText("MAX", 267, 139, 1, CYAN); // PWM 192 = 100%
}

void drawUbeValue(float ube)
{
  int mV = (int)(ube * 1000.0f + 0.5f);

  if (mV < 0) mV = 0;
  if (mV > 999) mV = 999;

  // Do absolutely nothing if the displayed UBE value has not changed.
  if (mV == shownUbeMilliV)
    return;

  const uint8_t scale = 2;
  const int x = 104;

  // UBE itself is static. Draw it only once so it can never blink.
  static bool ubeLabelDrawn = false;
  if (!ubeLabelDrawn) {
    drawText("UBE", x, 113, scale, STATUS_WHITE);
    ubeLabelDrawn = true;
  }

  // Clear ONLY the numeric/value part. Never erase UBE.
  fillRect(x + 39, 111, 90, 26, 0x0000);

  // Draw the value at the same fixed position.
  drawText("0", x + 42, 113, scale, STATUS_WHITE);

  // Decimal point is drawn manually.
  fillRect(x + 56, 126, 4, 4, STATUS_WHITE);

  char frac[4];
  snprintf(frac, sizeof(frac), "%03d", mV % 1000);
  drawDigits(frac, x + 62, 113, scale, STATUS_WHITE);

  drawText("V", x + 101, 114, scale, STATUS_WHITE);

  shownUbeMilliV = mV;
}

void drawSystemStatus(int currentTemp, int setTemp)
{
  // 0 = HEATING, 1 = HOLD.
  int status = (currentTemp < setTemp - 2) ? 0 : 1;

  if (status == shownStatus)
    return;

  // Clear a dedicated status band. It starts well above the text so that
  // the top pixels of HOLD/HEATING can never be clipped.
  // It ends before the UBE area.
  fillRect(0, 76, 320, 36, 0x0000);

  if (status == 0) {
    drawCenteredText("HEATING", 160, 83, 2, STATUS_RED);
  } else {
    drawCenteredText("HOLD", 160, 83, 2, STATUS_GREEN);
  }

  shownStatus = status;
}

void drawStaticUI()
{
  fillScreen(0x0000);


  // All three labels are mathematically centered in their columns.
  drawCenteredText("SET",     CENTER_SET,      8, LABEL_SCALE, STATUS_GREEN);
  drawCenteredText("TEMP",    CENTER_TEMP,     8, LABEL_SCALE, POWER_YELLOW);
  drawCenteredText("CURRENT", CENTER_CURRENT,  8, LABEL_SCALE, CYAN);

  // Lower section labels are static. Dynamic status/bar are drawn
  // from updateDisplay(), after the heater variables are available.
  drawPowerLabels();

  displayInitialized = true;
}

void drawSetValue(int value)
{
  // Clear only the value area, never the label or divider.
  fillRect(3, 28, 90, 48, 0x0000);

  // 200°C group is 75 px wide.
  // Center of left column = 46 -> x = 8.
  int x = CENTER_SET - tempValueWidth(VALUE_SCALE) / 2;

  drawTempValue(value, x, 34, STATUS_GREEN);

  shownSetTemp = value;
}

void drawCurrentTempValue(int value)
{
  // Center column is 134 px wide.
  // Large 331°C group is 99 px wide.
  // Center = 161 -> x = 111.
  fillRect(96, 28, 132, 48, 0x0000);

  int x = CENTER_TEMP - tempValueWidth(TEMP_SCALE) / 2;

  drawLargeTempValue(value, x, 34, POWER_YELLOW);

  shownCurrentTemp = value;
}

void drawCurrentValue(float currentA)
{
  int hundredths = (int)(currentA * 100.0f + 0.5f);

  if (hundredths < 0) hundredths = 0;
  if (hundredths > 999) hundredths = 999;

  // Do absolutely nothing while the displayed value is unchanged.
  if (hundredths == shownCurrentHundredths)
    return;

  // Only this value area is cleared when the actual displayed number changes.
  fillRect(232, 28, 86, 48, 0x0000);

  int x = CENTER_CURRENT - currentValueWidth() / 2;
  renderCurrentValue(currentA, x, 34, CYAN);

  shownCurrentHundredths = hundredths;
}

void updateDisplay(int setTemp, int currentTemp, float currentA, int pwm, int statusTemp)
{
  if (!displayInitialized)
    drawStaticUI();

  // Update only changed values. Labels and dividers never blink.
  if (setTemp != shownSetTemp)
    drawSetValue(setTemp);

  if (currentTemp != shownCurrentTemp)
    drawCurrentTempValue(currentTemp);

  drawCurrentValue(currentA);

  if (pwm != shownPWM) {
    drawPowerBar(pwm);
    shownPWM = pwm;
  }

  drawSystemStatus(statusTemp, setTemp);
  drawUbeValue(uBaseReal);
}

// Heater algorithm
// ============================================================

#define MAX_CURRENT 4.0f

enum Mode : uint8_t {
  MODE_OPERATING,
  MODE_MEAS
};

float currentTarget[2] = {3.0f, 1.0f};
// Temperature calibration for this specific transistor.
// Example: raw reading 13 C while actual ambient temperature is 26 C.
const float TEMP_OFFSET_C = 13.0f;

// Two-point temperature calibration.
// Point 1: ambient temperature after offset calibration.
// Point 2: Sn60Pb40 solder starts melting at about 183 C,
// while the previous display reading was about 165 C.
const float TEMP_CAL_REF_C = 26.0f;
const float TEMP_SLOPE = 1.065f;

float tempNow = 20.0f;
float tempTarget = 200.0f;
Mode mode = MODE_OPERATING;
uint8_t cnt = 255;
uint8_t pwmNow[2] = {55, 55};

uint16_t rawBase = 0;
uint16_t rawCurrent = 0;
float uBase = 0;
float uCurr = 0;
float iCurr = 0;
float uBaseReal = 0;

uint32_t lastSerial = 0;
uint32_t lastDisplay = 0;
uint32_t cycleCounter = 0;

// Button debounce / edge detection.
bool lastButtonUp = HIGH;
bool lastButtonDown = HIGH;
uint32_t lastButtonTime = 0;
const uint32_t BUTTON_DEBOUNCE_MS = 120;

void handleTempButtons()
{
  bool up = digitalRead(PIN_BUTTON_UP);
  bool down = digitalRead(PIN_BUTTON_DOWN);
  uint32_t now = millis();

  if (now - lastButtonTime >= BUTTON_DEBOUNCE_MS)
  {
    if (lastButtonUp == HIGH && up == LOW)
    {
      tempTarget += TEMP_STEP_C;
      if (tempTarget > TEMP_MAX_C) tempTarget = TEMP_MAX_C;
      lastButtonTime = now;
      lastDisplay = 0; // redraw SET value immediately
    }

    if (lastButtonDown == HIGH && down == LOW)
    {
      tempTarget -= TEMP_STEP_C;
      if (tempTarget < TEMP_MIN_C) tempTarget = TEMP_MIN_C;
      lastButtonTime = now;
      lastDisplay = 0; // redraw SET value immediately
    }
  }

  lastButtonUp = up;
  lastButtonDown = down;
}

float getAvgESP32(uint8_t pin, uint16_t &rawOut)
{
  uint32_t sum = 0;

  for (uint8_t i = 0; i < 32; i++)
    sum += analogRead(pin);

  rawOut = sum / 32;
  return (float)rawOut * (3.3f / 4095.0f);
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("BOOT: before ADC");

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SENS_BASE, ADC_11db);
  analogSetPinAttenuation(PIN_SENS_CURRENT, ADC_11db);

  pinMode(PIN_SENS_BASE, INPUT);
  pinMode(PIN_SENS_CURRENT, INPUT);

  // Buttons are connected between GPIO and GND.
  pinMode(PIN_BUTTON_UP, INPUT_PULLUP);
  pinMode(PIN_BUTTON_DOWN, INPUT_PULLUP);

  Serial.println("BOOT: before PWM");
  ledcAttach(PIN_PWM_BASE, 62500, 8);
  ledcWrite(PIN_PWM_BASE, pwmNow[MODE_OPERATING]);

  Serial.println("BOOT: before ST7789");

  tftInit();

  Serial.println("BOOT: ST7789 OK");

  fillScreen(0x0000);

  // Test screen first.
  fillRect(0, 0, 170, 40, 0x07FF);
  updateDisplay((int)tempTarget, (int)tempNow, iCurr, (int)pwmNow[mode], (int)tempNow);

  Serial.println("BOOT: DISPLAY OK");
}

void loop()
{
  // Temperature setting: GPIO21 = UP, GPIO20 = DOWN.
  handleTempButtons();

  uBase = getAvgESP32(PIN_SENS_BASE, rawBase) / 3.2f;
  uCurr = getAvgESP32(PIN_SENS_CURRENT, rawCurrent) / 13.0f;
  iCurr = uCurr * (1.05f / 0.05f);
  uBaseReal = uBase - uCurr;

  // ============================================================
  // EXACT ARDUINO CONTROL / MEASUREMENT SEQUENCE.
  //
  // 1) Read BASE/CURRENT and calculate current + VBE.
  // 2) Regulate PWM for the CURRENT TARGET of the current mode.
  // 3) Write that mode's PWM.
  // 4) Only when the mode counter expires:
  //      OPERATING -> MEAS: switch to the 1 A measurement mode.
  //      MEAS -> OPERATING: first calculate TEMP from the VBE
  //                         measured during this final MEAS loop,
  //                         then update the operating-current target.
  // 5) Counter values are exactly the Arduino values:
  //      OPERATING = 255 loops
  //      MEAS      = 4 loops
  //
  // IMPORTANT: there is NO "wait until current reaches 1 A" here.
  // The original Arduino also used exactly 4 measurement loops.
  // ============================================================

  if (iCurr < (currentTarget[mode] - 0.025f) &&
      pwmNow[mode] < 192)
  {
    pwmNow[mode]++;
  }

  if (iCurr > (currentTarget[mode] + 0.025f) &&
      pwmNow[mode] > 0)
  {
    pwmNow[mode]--;
  }

  ledcWrite(PIN_PWM_BASE, pwmNow[mode]);

  if (--cnt == 0)
  {
    if (mode == MODE_MEAS)
    {
      // Do NOT finish MEAS just because four 5-ms loops elapsed.
      // On ESP32 the current needs longer to move from ~4 A to 1 A.
      // Keep the same 1-A target and continue the existing PWM regulator
      // until the REAL measured current is inside the acceptance window.
      const float MEAS_I_MIN = 0.95f;
      const float MEAS_I_MAX = 1.05f;

      if (iCurr >= MEAS_I_MIN && iCurr <= MEAS_I_MAX)
      {
        // This is the actual measurement point.
        Serial.println();
        Serial.println("============= EXACT MEAS SAMPLE =============");
        Serial.print("MEAS I:          "); Serial.print(iCurr, 4); Serial.println(" A");
        Serial.print("MEAS I TARGET:   "); Serial.print(currentTarget[MODE_MEAS], 4); Serial.println(" A");
        Serial.print("MEAS PWM:        "); Serial.println(pwmNow[MODE_MEAS]);
        Serial.print("MEAS BASE RAW:   "); Serial.println(rawBase);
        Serial.print("MEAS CURRENT RAW:"); Serial.println(rawCurrent);
        Serial.print("MEAS uBase:      "); Serial.print(uBase * 1000.0f, 3); Serial.println(" mV");
        Serial.print("MEAS uCurr:      "); Serial.print(uCurr * 1000.0f, 3); Serial.println(" mV");
        Serial.print("MEAS uBaseReal:  "); Serial.print(uBaseReal * 1000.0f, 3); Serial.println(" mV");

        // Raw VBE temperature plus calibration offset.
        float tempRawCal = (0.775f - uBaseReal) * 430.0f + TEMP_OFFSET_C;

// Apply the slope around the calibrated ambient reference point.
// At 26 C the reading remains 26 C; higher temperatures are corrected.
tempNow = TEMP_CAL_REF_C + (tempRawCal - TEMP_CAL_REF_C) * TEMP_SLOPE;

        Serial.print("FORMULA TEMP:    ");
        Serial.print(tempNow, 2);
        Serial.println(" C");
        Serial.println("MEAS ACCEPTED:   YES");
        Serial.println("=============================================");

        if (tempNow < tempTarget &&
            currentTarget[MODE_OPERATING] < MAX_CURRENT)
        {
          currentTarget[MODE_OPERATING] *= 1.05f;
        }

        if (tempNow > tempTarget)
        {
          currentTarget[MODE_OPERATING] *= (1.0f / 1.05f);
        }

        if (currentTarget[MODE_OPERATING] > MAX_CURRENT)
          currentTarget[MODE_OPERATING] = MAX_CURRENT;

        mode = MODE_OPERATING;
        cnt = 255;
        cycleCounter++;
      }
      else
      {
        // Not yet at 1 A. Stay in MEAS.
        // Reset the short counter only as a housekeeping interval;
        // the actual completion condition is measured current.
        cnt = 4;
      }
    }
    else
    {
      // Normal HEAT phase: exactly the original 255-loop duration.
      mode = MODE_MEAS;

      // Carry the actual operating PWM into MEAS so the regulator
      // can ramp down from the real 4-A operating point.
      pwmNow[MODE_MEAS] = pwmNow[MODE_OPERATING];

      cnt = 4;
      cycleCounter++;
    }
  }

  uint32_t now = millis();

  if (now - lastDisplay >= 250)
  {
    lastDisplay = now;
    updateDisplay((int)tempTarget, (int)tempNow, iCurr, (int)pwmNow[mode], (int)tempNow);
  }

  if (now - lastSerial >= 500)
  {
    lastSerial = now;

    Serial.println();
    Serial.println("---------------- MONITOR ----------------");
    Serial.print("MODE: "); Serial.println(mode == MODE_OPERATING ? "HEAT" : "MEAS");
    Serial.print("TEMP: "); Serial.print(tempNow,2); Serial.println(" C");
    Serial.print("I: "); Serial.print(iCurr,4); Serial.println(" A");
    Serial.print("I TARGET: "); Serial.print(currentTarget[mode],4); Serial.println(" A");
    Serial.print("PWM: "); Serial.println(pwmNow[mode]);
    Serial.print("BASE RAW: "); Serial.println(rawBase);
    Serial.print("CURRENT RAW: "); Serial.println(rawCurrent);
    Serial.print("uBase: "); Serial.print(uBase*1000,3); Serial.println(" mV");
    Serial.print("uCurr: "); Serial.print(uCurr*1000,3); Serial.println(" mV");
    Serial.print("uBaseReal: "); Serial.print(uBaseReal*1000,3); Serial.println(" mV");
    Serial.print("CYCLE: "); Serial.println(cycleCounter);
    Serial.println("------------------------------------------");
  }

  delay(5);
}
