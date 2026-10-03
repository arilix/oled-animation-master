// Pac-Man style endless runner (Mario-esque jump gameplay)
// Hardware: ESP32-C3 + SH1106 128x64 I2C OLED + 1 touch button
//
// IMPORTANT: ESP32-C3 has NO built-in capacitive touch peripheral
// (unlike classic ESP32 / S2 / S3), so touchRead() is not available here.
// Use an external touch module (e.g. TTP223) whose digital OUT pin goes
// HIGH while touched, wired to TOUCH_PIN below.
//
// Wiring:
//   OLED SDA -> GPIO6, OLED SCL -> GPIO7 (same as esp32c3.ino)
//   Touch module OUT -> GPIO4, VCC -> 3V3, GND -> GND

#include <Wire.h>
#include <U8g2lib.h>

// ---------- Pins ----------
static const uint8_t OLED_SDA = 6;
static const uint8_t OLED_SCL = 7;
static const uint8_t OLED_ADDR = 0x3C;
static const uint8_t TOUCH_PIN = 4;

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ---------- Screen / world constants ----------
static const int16_t SCREEN_W = 128;
static const int16_t SCREEN_H = 64;
static const int16_t GROUND_Y = 54;     // baseline y where feet/ground sit
static const int16_t PACMAN_X = 20;     // fixed horizontal position
static const int16_t PACMAN_R = 7;

static const float GRAVITY = 0.9f;
static const float JUMP_VELOCITY = -9.3f;

static const uint8_t MAX_OBJECTS = 6;
static const int16_t GHOST_W = 14;
static const int16_t GHOST_H = 12;
static const int16_t DOT_R = 2;

enum ObjType : uint8_t { OBJ_NONE = 0, OBJ_GHOST, OBJ_DOT };

struct WorldObject {
  ObjType type;
  float x;
};

WorldObject objects[MAX_OBJECTS];

enum GameState { WAIT_START, PLAYING, GAME_OVER };
GameState state = WAIT_START;

float pacmanY;       // center y of pacman
float pacmanVY;
bool grounded;

uint32_t score;
uint32_t highScore = 0;
float gameSpeed;
float distanceAccum;
uint32_t nextSpawnAt; // distance (in px scrolled) at which to spawn next object

bool lastTouch = false;

// ---------- Input ----------
bool touchEdge() {
  bool now = digitalRead(TOUCH_PIN) == HIGH;
  bool edge = now && !lastTouch;
  lastTouch = now;
  return edge;
}

// ---------- Drawing ----------
void drawPacman(int16_t cx, int16_t cy) {
  bool mouthOpen = (millis() / 150) % 2 == 0;
  u8g2.setDrawColor(1);
  u8g2.drawDisc(cx, cy, PACMAN_R);
  if (mouthOpen) {
    u8g2.setDrawColor(0);
    u8g2.drawTriangle(cx, cy,
                       cx + PACMAN_R + 2, cy - PACMAN_R,
                       cx + PACMAN_R + 2, cy + PACMAN_R);
    u8g2.setDrawColor(1);
  }
}

void drawGhost(int16_t x) {
  int16_t top = GROUND_Y - GHOST_H;
  // body
  u8g2.setDrawColor(1);
  u8g2.drawRBox(x, top, GHOST_W, GHOST_H, 3);
  // zigzag skirt at bottom (cut out 3 notches)
  u8g2.setDrawColor(0);
  int16_t notchW = GHOST_W / 3;
  for (uint8_t i = 0; i < 3; i++) {
    int16_t nx = x + i * notchW;
    u8g2.drawTriangle(nx, GROUND_Y,
                       nx + notchW / 2, GROUND_Y - 3,
                       nx + notchW, GROUND_Y);
  }
  // eyes
  u8g2.drawDisc(x + 4, top + 5, 1);
  u8g2.drawDisc(x + GHOST_W - 4, top + 5, 1);
  u8g2.setDrawColor(1);
}

void drawDot(int16_t x) {
  u8g2.drawDisc(x, GROUND_Y - PACMAN_R, DOT_R);
}

void drawGround() {
  u8g2.drawHLine(0, GROUND_Y + 1, SCREEN_W);
}

void drawHUD() {
  char buf[24];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 9, "SCORE");
  u8g2.drawStr(2, 18, buf);
}

// ---------- Game logic ----------
void resetGame() {
  pacmanY = GROUND_Y - PACMAN_R;
  pacmanVY = 0;
  grounded = true;
  score = 0;
  gameSpeed = 2.2f;
  distanceAccum = 0;
  nextSpawnAt = 60;
  for (uint8_t i = 0; i < MAX_OBJECTS; i++) objects[i].type = OBJ_NONE;
}

bool spawnObject(ObjType type, float x) {
  for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
    if (objects[i].type == OBJ_NONE) {
      objects[i].type = type;
      objects[i].x = x;
      return true;
    }
  }
  return false;
}

void updatePlaying() {
  // input -> jump
  if (touchEdge() && grounded) {
    pacmanVY = JUMP_VELOCITY;
    grounded = false;
  }

  // physics
  pacmanVY += GRAVITY;
  pacmanY += pacmanVY;
  float floorY = GROUND_Y - PACMAN_R;
  if (pacmanY >= floorY) {
    pacmanY = floorY;
    pacmanVY = 0;
    grounded = true;
  }

  // difficulty ramp
  gameSpeed = 2.2f + score / 400.0f;
  if (gameSpeed > 5.5f) gameSpeed = 5.5f;

  // move & spawn objects
  distanceAccum += gameSpeed;
  if (distanceAccum >= nextSpawnAt) {
    distanceAccum = 0;
    nextSpawnAt = 55 + random(0, 45);
    ObjType t = (random(0, 100) < 60) ? OBJ_DOT : OBJ_GHOST;
    spawnObject(t, SCREEN_W + 4);
  }

  float pLeft = PACMAN_X - PACMAN_R, pRight = PACMAN_X + PACMAN_R;
  float pTop = pacmanY - PACMAN_R, pBot = pacmanY + PACMAN_R;

  for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
    if (objects[i].type == OBJ_NONE) continue;
    objects[i].x -= gameSpeed;

    if (objects[i].type == OBJ_GHOST) {
      float gLeft = objects[i].x, gRight = objects[i].x + GHOST_W;
      float gTop = GROUND_Y - GHOST_H, gBot = GROUND_Y;
      bool overlap = pRight > gLeft && pLeft < gRight && pBot > gTop && pTop < gBot;
      if (overlap) {
        state = GAME_OVER;
        if (score > highScore) highScore = score;
      }
      if (gRight < 0) objects[i].type = OBJ_NONE;
    } else if (objects[i].type == OBJ_DOT) {
      float dCx = objects[i].x, dCy = GROUND_Y - PACMAN_R;
      float dx = dCx - PACMAN_X, dy = dCy - pacmanY;
      float distSq = dx * dx + dy * dy;
      float rr = (DOT_R + PACMAN_R) * (DOT_R + PACMAN_R);
      if (distSq <= rr) {
        objects[i].type = OBJ_NONE;
        score += 10;
      } else if (dCx + DOT_R < 0) {
        objects[i].type = OBJ_NONE;
      }
    }
  }

  score += 1; // distance points
}

// ---------- Screens ----------
void drawStartScreen() {
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(14, 26, "PAC-RUNNER");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(14, 42, "Touch to Start");
  drawPacman(PACMAN_X, GROUND_Y - PACMAN_R);
  drawGround();
}

void drawGameOverScreen() {
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(20, 24, "GAME OVER");
  u8g2.setFont(u8g2_font_5x7_tf);
  char buf[32];
  snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
  u8g2.drawStr(24, 38, buf);
  snprintf(buf, sizeof(buf), "Best:  %lu", (unsigned long)highScore);
  u8g2.drawStr(24, 48, buf);
  u8g2.drawStr(14, 60, "Touch to Retry");
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TOUCH_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR * 2);
  u8g2.begin();

  randomSeed(analogRead(0));
  resetGame();

  Serial.println("Pac-Runner ready");
}

void loop() {
  switch (state) {
    case WAIT_START:
      if (touchEdge()) {
        resetGame();
        state = PLAYING;
      }
      break;
    case PLAYING:
      updatePlaying();
      break;
    case GAME_OVER:
      if (touchEdge()) {
        state = WAIT_START;
      }
      break;
  }

  u8g2.clearBuffer();
  switch (state) {
    case WAIT_START:
      drawStartScreen();
      break;
    case PLAYING:
      drawGround();
      for (uint8_t i = 0; i < MAX_OBJECTS; i++) {
        if (objects[i].type == OBJ_GHOST) drawGhost((int16_t)objects[i].x);
        else if (objects[i].type == OBJ_DOT) drawDot((int16_t)objects[i].x);
      }
      drawPacman(PACMAN_X, (int16_t)pacmanY);
      drawHUD();
      break;
    case GAME_OVER:
      drawGameOverScreen();
      break;
  }
  u8g2.sendBuffer();

  delay(30); // ~33 FPS
}
