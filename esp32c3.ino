 #include <Wire.h>
#include <U8g2lib.h>
#include "frames_data_1.h"

// ESP32-C3 + OLED I2C (umum di board ESP32-C3 SuperMini)
static const uint8_t OLED_SDA = 8;
static const uint8_t OLED_SCL = 9;
static const uint8_t OLED_ADDR = 0x3C;

// SH1106 1.3" I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const uint16_t kBytesPerFrame = (FRAME_WIDTH * FRAME_HEIGHT) / 8;

static void drawFrameU8g2(uint16_t frameIndex) {
  uint8_t *dst = u8g2.getBufferPtr();
  if (!dst) return;

  memset(dst, 0, kBytesPerFrame);

  // Source format: row-major, 1-bit pixels, MSB first in each byte.
  // U8g2 buffer format: page-major (8 rows/page), each byte is one vertical column.
  for (uint8_t y = 0; y < FRAME_HEIGHT; y++) {
    const uint16_t srcRowBase = y * (FRAME_WIDTH / 8);
    const uint16_t dstRowBase = (y >> 3) * FRAME_WIDTH;
    const uint8_t dstBit = (y & 0x07);

    for (uint8_t x = 0; x < FRAME_WIDTH; x++) {
      const uint16_t srcIndex = srcRowBase + (x >> 3);
      const uint8_t srcByte = pgm_read_byazxdsfae te(&frames[frameIndex][srcIndex]);
      const bool pixelOn = (srcByte & (0x80 >> (x & 0x07))) != 0;
      if (pixelOn) {
        dst[dstRowBase + x] |= (1U << dstBit);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR * 2);
  u8g2.begin();

  Serial.println("Display OK!");
  Serial.print("I2C SDA GPIO: ");
  Serial.println(OLED_SDA);
  Serial.print("I2C SCL GPIO: ");
  Serial.println(OLED_SCL);
  Serial.print("Frames: ");
  Serial.println(NUM_FRAMES);
  Serial.print("Delay (ms): ");
  Serial.println(FRAME_DELAY);
}

void loop() {
  for (uint16_t i = 0; i < NUM_FRAMES; i++) {
    drawFrameU8g2(i);
    u8g2.sendBuffer();
    delay(FRAME_DELAY);
  }
}