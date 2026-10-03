// Heart animation, "buatan sendiri": computed live on the ESP32-C3 from the
// parametric Heart Curve, no pre-rendered frame data at all.
//
//   x(t) = 16 sin^3(t)
//   y(t) = 13 cos(t) - 5 cos(2t) - 2 cos(3t) - cos(4t)     0 <= t < 2*pi
//
// Two phases, looping forever:
//   1. Draw-in  : curve traced stroke-by-stroke with a bright tip.
//   2. Heartbeat: full heart pulses (double-thump) with twinkling sparkles.
//
// Hardware: SH1106 128x64 I2C OLED, SDA=GPIO6, SCL=GPIO7 (same as esp32c3.ino)

#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

static const uint8_t OLED_SDA = 6;
static const uint8_t OLED_SCL = 7;
static const uint8_t OLED_ADDR = 0x3C;

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const uint8_t NUM_PTS = 90;
static const float SCALE = 1.8f;
static const int16_t CX = 64;
static const int16_t CY = 32;

static const uint16_t DRAW_MS = 1800;
static const uint16_t BEAT_MS = 3300;
static const uint16_t HOLD_MS = 600;
static const uint16_t CYCLE_MS = DRAW_MS + BEAT_MS + HOLD_MS;
static const float BEAT_PERIOD_S = 1.1f;

// Precomputed curve offsets (pixels, at scale = 1.0) from center (CX, CY).
float baseX[NUM_PTS];
float baseY[NUM_PTS];

static const uint8_t NUM_SPARKLES = 10;
int16_t sparkleX[NUM_SPARKLES];
int16_t sparkleY[NUM_SPARKLES];

void precomputeCurve() {
  const float twoPi = 2.0f * PI;
  float yMin = 1e9f, yMax = -1e9f;
  float rawY[NUM_PTS];
  float rawX[NUM_PTS];

  for (uint8_t i = 0; i < NUM_PTS; i++) {
    float t = twoPi * i / (NUM_PTS - 1);
    float sx = 16.0f * sinf(t) * sinf(t) * sinf(t);
    float sy = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
    rawX[i] = sx;
    rawY[i] = sy;
    if (sy < yMin) yMin = sy;
    if (sy > yMax) yMax = sy;
  }

  float centerY = (yMin + yMax) / 2.0f;
  for (uint8_t i = 0; i < NUM_PTS; i++) {
    baseX[i] = rawX[i] * SCALE;
    baseY[i] = (rawY[i] - centerY) * SCALE;
  }
}

void seedSparkles() {
  randomSeed(analogRead(0));
  for (uint8_t i = 0; i < NUM_SPARKLES; i++) {
    sparkleX[i] = random(4, 124);
    sparkleY[i] = random(2, 62);
  }
}

float heartbeatScale(float tSec) {
  float phase = fmodf(tSec, BEAT_PERIOD_S) / BEAT_PERIOD_S;
  float d1 = (phase - 0.06f) / 0.028f;
  float d2 = (phase - 0.20f) / 0.035f;
  float bump1 = 0.14f * expf(-(d1 * d1));
  float bump2 = 0.07f * expf(-(d2 * d2));
  return 1.0f + bump1 + bump2;
}

void drawCurve(float scale, uint8_t nPts, bool withTip) {
  int16_t px = 0, py = 0;
  for (uint8_t i = 0; i < nPts; i++) {
    int16_t x = CX + (int16_t)lroundf(baseX[i] * scale);
    int16_t y = CY - (int16_t)lroundf(baseY[i] * scale);
    if (i > 0) {
      u8g2.drawLine(px, py, x, y);
    }
    px = x;
    py = y;
  }
  if (withTip) {
    u8g2.drawDisc(px, py, 2);
  }
}

void drawSparkles(uint32_t elapsedMs) {
  for (uint8_t i = 0; i < NUM_SPARKLES; i++) {
    uint32_t twinkle = (elapsedMs / 250 + i * 37) % 5;
    if (twinkle == 0) {
      u8g2.drawPixel(sparkleX[i], sparkleY[i]);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR * 2);
  u8g2.begin();

  precomputeCurve();
  seedSparkles();

  Serial.println("Heart (native) ready");
}

void loop() {
  uint32_t t = millis() % CYCLE_MS;

  u8g2.clearBuffer();

  if (t < DRAW_MS) {
    float progress = (float)t / DRAW_MS;
    uint8_t nPts = (uint8_t)fmaxf(2, progress * NUM_PTS);
    drawCurve(1.0f, nPts, true);
  } else if (t < DRAW_MS + BEAT_MS) {
    uint32_t beatElapsed = t - DRAW_MS;
    float scale = heartbeatScale(beatElapsed / 1000.0f);
    drawCurve(scale, NUM_PTS, false);
    drawSparkles(beatElapsed);
  } else {
    drawCurve(1.0f, NUM_PTS, false);
  }

  u8g2.sendBuffer();
  delay(30); // ~33 FPS
}
