// Interactive Mochi Face for ESP32-C3 + SH1106 128x64 OLED.
// Style: big rounded eyes + tiny mouth on black background (see reference
// photo). Idle: eyes open, occasional blink, slight bounce.
// Touch GPIO4 (external touch module, e.g. TTP223 digital OUT -> HIGH when
// touched -- ESP32-C3 has no built-in touch peripheral): face goes "manja"
// (affectionate) -- eyes turn into sparkle stars, ambient twinkles appear,
// and floating hearts rise around it.
//
// Wiring: OLED SDA->GPIO6, SCL->GPIO7 (I2C, addr 0x3C). Touch OUT->GPIO4.

#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

static const uint8_t OLED_SDA = 6;
static const uint8_t OLED_SCL = 7;
static const uint8_t OLED_ADDR = 0x3C;
static const uint8_t TOUCH_PIN = 4;

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------- face geometry ----------
// Small rounded-square eyes with lots of black margin, like a tiny robot
// face module (see reference photo) -- not big blob/stadium eyes.
static const int16_t EYE_W = 20;
static const int16_t EYE_H = 24;
static const int16_t EYE_R = 5;       // corner radius -> soft rounded square
static const int16_t EYE_GAP = 20;
static const int16_t EYE_TOP = 15;
static const int16_t LEFT_EYE_X = (128 - (EYE_W * 2 + EYE_GAP)) / 2;
static const int16_t RIGHT_EYE_X = LEFT_EYE_X + EYE_W + EYE_GAP;
static const int16_t EYE_CENTER_X_L = LEFT_EYE_X + EYE_W / 2;
static const int16_t EYE_CENTER_X_R = RIGHT_EYE_X + EYE_W / 2;
static const int16_t MOUTH_Y = EYE_TOP + EYE_H + 6;

enum FaceState { IDLE, MANJA, SETTLE };
FaceState state = IDLE;

uint32_t stateChangedAt = 0;
uint32_t lastBlinkAt = 0;
uint32_t nextBlinkGap = 3000;
bool blinking = false;
uint32_t blinkStartAt = 0;

struct Heart {
  bool active;
  float x, y;
  uint32_t bornAt;
};
static const uint8_t MAX_HEARTS = 6;
Heart hearts[MAX_HEARTS];
uint32_t lastHeartAt = 0;

// ---------- input ----------
bool isTouching() {
  return digitalRead(TOUCH_PIN) == HIGH;
}

// ---------- hearts ----------
void drawTinyHeart(int16_t x, int16_t y) {
  u8g2.drawDisc(x - 1, y, 1);
  u8g2.drawDisc(x + 1, y, 1);
  u8g2.drawTriangle(x - 2, y, x + 2, y, x, y + 3);
}

void spawnHeart() {
  for (uint8_t i = 0; i < MAX_HEARTS; i++) {
    if (!hearts[i].active) {
      hearts[i].active = true;
      hearts[i].x = random(6, 122);
      hearts[i].y = 62;
      hearts[i].bornAt = millis();
      return;
    }
  }
}

void updateAndDrawHearts() {
  u8g2.setDrawColor(1);
  for (uint8_t i = 0; i < MAX_HEARTS; i++) {
    if (!hearts[i].active) continue;
    uint32_t age = millis() - hearts[i].bornAt;
    if (age > 1600) {
      hearts[i].active = false;
      continue;
    }
    float t = age / 1600.0f;
    int16_t y = (int16_t)(hearts[i].y - t * 58.0f);
    int16_t x = (int16_t)(hearts[i].x + 4.0f * sinf(t * 6.0f + hearts[i].x));
    drawTinyHeart(x, y);
  }
}

// ---------- eyes ----------
void drawOpenEye(int16_t x, int16_t yOffset) {
  u8g2.drawRBox(x, EYE_TOP + yOffset, EYE_W, EYE_H, EYE_R);
}

// Soft closed "merem" eye: small smiling curve, same footprint as the open
// rounded-square eye so the face doesn't jump around when it closes.
void drawClosedEye(int16_t cx, int16_t cy) {
  int16_t halfW = EYE_W / 2 - 1;
  int16_t depth = 5;
  int16_t prevX = 0, prevY = 0;
  for (int16_t i = -halfW; i <= halfW; i++) {
    float ratio = (float)i / halfW;
    int16_t y = cy + (int16_t)(depth * (1.0f - ratio * ratio));
    int16_t x = cx + i;
    if (i > -halfW) {
      u8g2.drawLine(prevX, prevY, x, y);
      u8g2.drawLine(prevX, prevY - 1, x, y - 1);
    }
    prevX = x;
    prevY = y;
  }
}

// Excited "starstruck" eye: 8-point sparkle, shown while being touched.
void drawSparkleEye(int16_t cx, int16_t cy) {
  u8g2.drawLine(cx, cy - 9, cx, cy + 9);
  u8g2.drawLine(cx - 6, cy, cx + 6, cy);
  u8g2.drawLine(cx - 4, cy - 4, cx - 1, cy - 1);
  u8g2.drawLine(cx + 4, cy - 4, cx + 1, cy - 1);
  u8g2.drawLine(cx - 4, cy + 4, cx - 1, cy + 1);
  u8g2.drawLine(cx + 4, cy + 4, cx + 1, cy + 1);
  u8g2.drawDisc(cx, cy, 1);
}

// Tiny ambient twinkle marks scattered near the face while excited.
void drawAmbientSparkles() {
  const int16_t sx[] = {18, 108, 100, 24};
  const int16_t sy[] = {12, 16, 46, 44};
  for (uint8_t i = 0; i < 4; i++) {
    uint32_t cycle = (millis() / 260 + i * 53) % 4;
    if (cycle != 0) continue;
    int16_t x = sx[i];
    int16_t y = sy[i];
    u8g2.drawLine(x - 2, y, x + 2, y);
    u8g2.drawLine(x, y - 2, x, y + 2);
  }
}

void drawMouth(bool happy) {
  if (happy) {
    u8g2.drawLine(64 - 3, MOUTH_Y, 64 - 1, MOUTH_Y + 2);
    u8g2.drawLine(64 - 1, MOUTH_Y + 2, 64 + 1, MOUTH_Y);
    u8g2.drawLine(64 + 1, MOUTH_Y, 64 + 3, MOUTH_Y + 2);
  } else {
    u8g2.drawRBox(64 - 2, MOUTH_Y, 4, 2, 1);
  }
}

// ---------- state machine ----------
void updateState() {
  bool touching = isTouching();

  if (touching && state != MANJA) {
    state = MANJA;
    stateChangedAt = millis();
    lastHeartAt = 0;
  } else if (!touching && state == MANJA) {
    state = SETTLE;
    stateChangedAt = millis();
  } else if (state == SETTLE && millis() - stateChangedAt > 900) {
    state = IDLE;
    stateChangedAt = millis();
  }

  if (state != MANJA) {
    if (!blinking && millis() - lastBlinkAt > nextBlinkGap) {
      blinking = true;
      blinkStartAt = millis();
    }
    if (blinking && millis() - blinkStartAt > 160) {
      blinking = false;
      lastBlinkAt = millis();
      nextBlinkGap = 2500 + random(0, 3000);
    }
  }

  if (state == MANJA && millis() - lastHeartAt > 380) {
    spawnHeart();
    lastHeartAt = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TOUCH_PIN, INPUT);
  randomSeed(analogRead(0));

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR * 2);
  u8g2.begin();

  for (uint8_t i = 0; i < MAX_HEARTS; i++) hearts[i].active = false;

  Serial.println("Mochi face ready");
}

void loop() {
  updateState();

  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  if (state == MANJA || state == SETTLE) {
    int16_t eyeCy = EYE_TOP + EYE_H / 2 + 1;
    drawSparkleEye(EYE_CENTER_X_L, eyeCy);
    drawSparkleEye(EYE_CENTER_X_R, eyeCy);
    drawMouth(true);
    drawAmbientSparkles();
    updateAndDrawHearts();
  } else {
    int16_t bounce = (int16_t)(sinf(millis() / 700.0f) * 1.2f);
    if (blinking) {
      int16_t eyeCy = EYE_TOP + EYE_H / 2 + 1 + bounce;
      drawClosedEye(EYE_CENTER_X_L, eyeCy);
      drawClosedEye(EYE_CENTER_X_R, eyeCy);
    } else {
      drawOpenEye(LEFT_EYE_X, bounce);
      drawOpenEye(RIGHT_EYE_X, bounce);
    }
    drawMouth(false);
  }

  u8g2.sendBuffer();
  delay(25);
}
