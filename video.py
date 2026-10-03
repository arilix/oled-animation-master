import cv2
import numpy as np

# --- CONFIGURATION ---
VIDEO_PATH = "video/2.mp4"
HEADER_OUTPUT_FILE = "frames_data_2.h"
COPY_PASTE_OUTPUT_FILE = "esp32c3_copy_paste.ino"
WIDTH = 128
HEIGHT = 64
OLED_SDA = 6
OLED_SCL = 7
OLED_ADDR = "0x3C"

# SH1106/OLED modules are often mounted upside down. If the result is still
# reversed, change these two flags and regenerate the header.
FLIP_VERTICAL = True
FLIP_HORIZONTAL = False

# Use every frame from the source video by default. This keeps motion smoother
# than dropping frames during conversion.
TARGET_FPS = None
FRAME_DELAY = 33

# Keep the conversion close to the original simple Otsu pipeline. The stronger
# filters made the image look blocky on small OLEDs.
RESIZE_MODE = "stretch"  # "stretch" or "crop"
APPLY_EQUALIZE = False
APPLY_BLUR = False
TEMPORAL_SMOOTHING = 0.0

# "otsu" is cleaner for tiny OLED animation. Use "dither" if you want more
# grayscale-like texture and do not mind checkerboard pixels.
MONOCHROME_MODE = "otsu"


def resize_to_oled(frame):
    """Resize video frame to the OLED resolution."""
    if RESIZE_MODE != "crop":
        return cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_AREA)

    src_h, src_w = frame.shape[:2]
    target_ratio = WIDTH / HEIGHT
    src_ratio = src_w / src_h

    if src_ratio > target_ratio:
        crop_w = int(src_h * target_ratio)
        left = (src_w - crop_w) // 2
        frame = frame[:, left : left + crop_w]
    elif src_ratio < target_ratio:
        crop_h = int(src_w / target_ratio)
        top = (src_h - crop_h) // 2
        frame = frame[top : top + crop_h, :]

    return cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_AREA)


def apply_orientation(gray):
    if FLIP_VERTICAL and FLIP_HORIZONTAL:
        return cv2.flip(gray, -1)
    if FLIP_VERTICAL:
        return cv2.flip(gray, 0)
    if FLIP_HORIZONTAL:
        return cv2.flip(gray, 1)
    return gray


def ordered_dither(gray):
    bayer4 = np.array(
        [
            [0, 8, 2, 10],
            [12, 4, 14, 6],
            [3, 11, 1, 9],
            [15, 7, 13, 5],
        ],
        dtype=np.float32,
    )
    threshold_map = ((bayer4 + 0.5) / 16.0) * 255.0
    threshold_map = np.tile(
        threshold_map,
        (HEIGHT // threshold_map.shape[0] + 1, WIDTH // threshold_map.shape[1] + 1),
    )[:HEIGHT, :WIDTH]
    return np.where(gray.astype(np.float32) > threshold_map, 255, 0).astype(np.uint8)


def make_monochrome(gray):
    if MONOCHROME_MODE == "dither":
        return ordered_dither(gray)

    _, binary = cv2.threshold(gray, 0, 255, cv2.THRESH_BINARY | cv2.THRESH_OTSU)
    return binary


def pack_row_major_msb(binary):
    flat = binary.reshape(-1)
    byte_array = []

    for i in range(0, len(flat), 8):
        byte_val = 0
        for bit in range(8):
            if flat[i + bit] > 0:
                byte_val |= 1 << (7 - bit)
        byte_array.append(f"0x{byte_val:02x}")

    return byte_array


def frame_indices(total_frames, input_fps):
    if total_frames <= 0:
        return

    if TARGET_FPS is None or TARGET_FPS >= input_fps:
        for index in range(total_frames):
            yield index
        return

    if input_fps <= 0:
        input_fps = TARGET_FPS or 30

    duration = total_frames / input_fps
    output_count = max(1, round(duration * TARGET_FPS))
    for out_index in range(output_count):
        timestamp = out_index / TARGET_FPS
        yield min(total_frames - 1, round(timestamp * input_fps))


def process_frame(frame, previous_gray):
    frame_res = resize_to_oled(frame)
    gray = cv2.cvtColor(frame_res, cv2.COLOR_BGR2GRAY)
    if APPLY_EQUALIZE:
        gray = cv2.equalizeHist(gray)
    if APPLY_BLUR:
        gray = cv2.GaussianBlur(gray, (3, 3), 0)
    gray = apply_orientation(gray)

    if previous_gray is not None and TEMPORAL_SMOOTHING > 0:
        gray = cv2.addWeighted(
            previous_gray,
            TEMPORAL_SMOOTHING,
            gray,
            1.0 - TEMPORAL_SMOOTHING,
            0,
        )

    binary = make_monochrome(gray)
    return binary, gray


def write_frame_data(f, frames_list):
    num_frames = len(frames_list)
    bytes_per_frame = (WIDTH * HEIGHT) // 8

    f.write("#include <Arduino.h>\n")
    f.write("#include <pgmspace.h>\n\n")

    f.write(f"#define FRAME_WIDTH {WIDTH}\n")
    f.write(f"#define FRAME_HEIGHT {HEIGHT}\n")
    f.write(f"#define FRAME_DELAY {FRAME_DELAY}\n")
    f.write(f"#define NUM_FRAMES {num_frames}\n\n")

    f.write(f"const unsigned char frames[{num_frames}][{bytes_per_frame}] PROGMEM = {{\n")

    for i, frame_bytes in enumerate(frames_list):
        f.write(f"  {{ // Frame {i}\n    ")

        lines = []
        for j in range(0, len(frame_bytes), 16):
            lines.append(", ".join(frame_bytes[j : j + 16]))

        f.write(",\n    ".join(lines))
        f.write("\n  }")
        if i < num_frames - 1:
            f.write(",")
        f.write("\n")

    f.write("};\n")


def write_sketch_footer(f):
    f.write(
        f"""
static const uint8_t OLED_SDA = {OLED_SDA};
static const uint8_t OLED_SCL = {OLED_SCL};
static const uint8_t OLED_ADDR = {OLED_ADDR};

// SH1106 1.3 inch I2C OLED. Change this constructor if your OLED is SSD1306.
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

static const uint16_t kBytesPerFrame = (FRAME_WIDTH * FRAME_HEIGHT) / 8;

static void drawFrameU8g2(uint16_t frameIndex) {{
  uint8_t *dst = u8g2.getBufferPtr();
  if (!dst) return;

  memset(dst, 0, kBytesPerFrame);

  // Source format: row-major, 1-bit pixels, MSB first in each byte.
  // U8g2 buffer format: page-major (8 rows/page), each byte is one vertical column.
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


def write_header(frames_list):
    print(f"Formatting {len(frames_list)} frames into header and copy-paste sketch...")

    with open(HEADER_OUTPUT_FILE, "w") as f:
        write_frame_data(f, frames_list)

    with open(COPY_PASTE_OUTPUT_FILE, "w") as f:
        f.write("// Single-file sketch: copy-paste directly into Arduino IDE\n")
        f.write("#include <Wire.h>\n")
        f.write("#include <U8g2lib.h>\n\n")
        write_frame_data(f, frames_list)
        f.write("\n")
        write_sketch_footer(f)


def convert_video():
    global FRAME_DELAY

    cap = cv2.VideoCapture(VIDEO_PATH)
    if not cap.isOpened():
        raise FileNotFoundError(f"Cannot open video: {VIDEO_PATH}")

    input_fps = cap.get(cv2.CAP_PROP_FPS) or TARGET_FPS or 30
    output_fps = TARGET_FPS or input_fps
    FRAME_DELAY = max(1, round(1000 / output_fps))
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    frames_list = []
    previous_gray = None

    print(
        f"Extracting {VIDEO_PATH} at {output_fps:.2f} FPS "
        f"(source: {input_fps:.2f} FPS, {total_frames} frames)..."
    )

    for index in frame_indices(total_frames, input_fps):
        cap.set(cv2.CAP_PROP_POS_FRAMES, index)
        ret, frame = cap.read()
        if not ret:
            continue

        binary, previous_gray = process_frame(frame, previous_gray)
        frames_list.append(pack_row_major_msb(binary))

    cap.release()

    if not frames_list:
        raise RuntimeError("No frames were extracted from the video.")

    write_header(frames_list)
    print(f"Success! Saved to {HEADER_OUTPUT_FILE} and {COPY_PASTE_OUTPUT_FILE}.")
    print(f"Delay: {FRAME_DELAY} ms. SDA: GPIO {OLED_SDA}. SCL: GPIO {OLED_SCL}.")


if __name__ == "__main__":
    convert_video()
