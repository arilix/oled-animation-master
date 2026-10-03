#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

// ESP32-C3 I2C pins (ubah sesuai diagram jika berbeda)
#define I2C_SDA 4
#define I2C_SCL 5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED tidak terdeteksi di alamat 0x3C");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32-C3 + OLED OK");
  display.println("I2C SDA: GPIO4");
  display.println("I2C SCL: GPIO5");
  display.display();
}

void loop() {
  // Blink indikator sederhana di pojok kanan atas
  static bool on = false;
  on = !on;

  display.fillRect(120, 0, 8, 8, on ? SSD1306_WHITE : SSD1306_BLACK);
  display.display();
  delay(500);
}
