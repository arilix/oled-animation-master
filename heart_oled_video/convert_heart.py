"""
Convert ../heart_animation.mp4 into a monochrome PROGMEM frame array for the
128x64 SH1106 OLED, and emit a self-contained Arduino sketch (same pattern as
the project's video.py pipeline, but letterboxed since the source video is
square and the OLED is 2:1 wide).
"""

import cv2
import numpy as np

VIDEO_PATH = "../heart_animation.mp4"
SKETCH_OUTPUT_FILE = "heart_oled_video.ino"
WIDTH = 128
HEIGHT = 64
OLED_SDA = 6
OLED_SCL = 7
OLED_ADDR = "0x3C"

BIN_THRESHOLD = 40  # fixed threshold: background is near-black, glow line is bright


def letterbox_gray(frame):
    """Fit the (square) source frame to OLED height, centered, black bars L/R."""
    h, w = frame.shape[:2]
    scale = HEIGHT / h
    new_w = max(1, round(w * scale))
    resized = cv2.resize(frame, (new_w, HEIGHT), interpolation=cv2.INTER_AREA)
    gray = cv2.cvtColor(resized, cv2.COLOR_BGR2GRAY)

    canvas = np.zeros((HEIGHT, WIDTH), dtype=np.uint8)
    x_off = (WIDTH - new_w) // 2
    if x_off >= 0:
        canvas[:, x_off:x_off + new_w] = gray[:, :WIDTH - x_off] if new_w > WIDTH else gray
    else:
        canvas[:, :] = gray[:, -x_off:-x_off + WIDTH]
    return canvas


def pack_row_major_msb(binary):
    flat = binary.reshape(-1)
    out = []
    for i in range(0, len(flat), 8):
        byte_val = 0
        for bit in range(8):
            if flat[i + bit] > 0:
                byte_val |= 1 << (7 - bit)
        out.append(f"0x{byte_val:02x}")
    return out


def write_frame_data(f, frames_list, frame_delay):
    num_frames = len(frames_list)
    bytes_per_frame = (WIDTH * HEIGHT) // 8

    f.write("#include <Arduino.h>\n#include <pgmspace.h>\n\n")
    f.write(f"#define FRAME_WIDTH {WIDTH}\n")
    f.write(f"#define FRAME_HEIGHT {HEIGHT}\n")
    f.write(f"#define FRAME_DELAY {frame_delay}\n")
    f.write(f"#define NUM_FRAMES {num_frames}\n\n")
    f.write(f"const unsigned char frames[{num_frames}][{bytes_per_frame}] PROGMEM = {{\n")

    for i, frame_bytes in enumerate(frames_list):
        f.write(f"  {{ // Frame {i}\n    ")
        lines = [", ".join(frame_bytes[j:j + 16]) for j in range(0, len(frame_bytes), 16)]
        f.write(",\n    ".join(lines))
        f.write("\n  }")
        f.write(",\n" if i < num_frames - 1 else "\n")

    f.write("};\n")


def write_sketch(frames_list, frame_delay):
    with open(SKETCH_OUTPUT_FILE, "w") as f:
        f.write("// Heart animation converted from heart_animation.mp4 (single-file sketch)\n")
        f.write("#include <Wire.h>\n#include <U8g2lib.h>\n\n")
        write_frame_data(f, frames_list, frame_delay)
        f.write("\n")
        f.write(
            f"""static const uint8_t OLED_SDA = {OLED_SDA};
static const uint8_t OLED_SCL = {OLED_SCL};
static const uint8_t OLED_ADDR = {OLED_ADDR};

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const uint16_t kBytesPerFrame = (FRAME_WIDTH * FRAME_HEIGHT) / 8;

static void drawFrameU8g2(uint16_t frameIndex) {{
  uint8_t *dst = u8g2.getBufferPtr();
  if (!dst) return;
  memset(dst, 0, kBytesPerFrame);

  for (uint8_t y = 0; y < FRAME_HEIGHT; y++) {{
    const uint16_t srcRowBase = y * (FRAME_WIDTH / 8);
    const uint16_t dstRowBase = (y >> 3) * FRAME_WIDTH;
    const uint8_t dstBit = y & 0x07;
    for (uint8_t x = 0; x < FRAME_WIDTH; x++) {{
      const uint16_t srcIndex = srcRowBase + (x >> 3);
      const uint8_t srcByte = pgm_read_byte(&frames[frameIndex][srcIndex]);
      const bool pixelOn = (srcByte & (0x80 >> (x & 0x07))) != 0;
      if (pixelOn) {{
        dst[dstRowBase + x] |= 1U << dstBit;
      }}
    }}
  }}
}}

void setup() {{
  Serial.begin(115200);
  delay(500);
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.setI2CAddress(OLED_ADDR * 2);
  u8g2.begin();
  Serial.print("Heart frames: ");
  Serial.println(NUM_FRAMES);
}}

void loop() {{
  for (uint16_t i = 0; i < NUM_FRAMES; i++) {{
    drawFrameU8g2(i);
    u8g2.sendBuffer();
    delay(FRAME_DELAY);
  }}
}}
"""
        )


def convert():
    cap = cv2.VideoCapture(VIDEO_PATH)
    if not cap.isOpened():
        raise FileNotFoundError(f"Cannot open {VIDEO_PATH}")

    fps = cap.get(cv2.CAP_PROP_FPS) or 30
    frame_delay = max(1, round(1000 / fps))
    frames_list = []

    while True:
        ret, frame = cap.read()
        if not ret:
            break
        gray = letterbox_gray(frame)
        _, binary = cv2.threshold(gray, BIN_THRESHOLD, 255, cv2.THRESH_BINARY)
        frames_list.append(pack_row_major_msb(binary))

    cap.release()
    if not frames_list:
        raise RuntimeError("No frames extracted")

    write_sketch(frames_list, frame_delay)
    print(f"{len(frames_list)} frames -> {SKETCH_OUTPUT_FILE}")
    print(f"Delay: {frame_delay} ms")


if __name__ == "__main__":
    convert()
