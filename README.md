# OLED Animation Project

Display smooth animations on a 128x64 OLED display connected to an ESP32 microcontroller using frame data extracted from video files.

---

## 📋 Table of Contents

1. [Prerequisites](#prerequisites)
2. [Hardware Setup](#hardware-setup)
3. [Video Preparation](#video-preparation)
4. [Frame Extraction](#frame-extraction)
5. [Project Upload](#project-upload)
6. [Troubleshooting](#troubleshooting)

---

## 🔧 Prerequisites

### Software
- **Python 3.7+**
- **OpenCV**: Install with `pip install opencv-python`
- **PlatformIO**: Extension in VS Code or command-line tool
- **ESP32 Board Support**: Ensure ESP32 core is installed in PlatformIO

### Hardware
- **ESP32** microcontroller (ESP32-WROOM-32 or similar)
- **128x64 OLED Display** (SSD1306 controller)
- **USB Cable** for programming ESP32
- **Breadboard & Jumper Wires**

---

## 🖥️ Hardware Setup

### OLED Pin Connections to ESP32

Connect your OLED display to the ESP32 with the following pin mapping:

| OLED Pin | ESP32 Pin | Description |
|----------|-----------|-------------|
| GND      | GND       | Ground |
| VCC      | 3V3       | Power Supply (3.3V) |
| SCL      | GPIO 22   | I2C Clock |
| SDA      | GPIO 21   | I2C Data |

### Connection Diagram
```
OLED Display               ESP32
    |                        |
   GND -------- GND ---------+
   VCC -------- 3V3 ---------+
   SCL -------- GPIO 22 -----+
   SDA -------- GPIO 21 -----+
```

### Notes
- Use a breadboard to organize connections
- Keep wire lengths short (< 10cm) for stable I2C communication
- Ensure proper power supply (3.3V recommended for OLED)
- Pull-up resistors may be needed if display is unstable (typically 4.7kΩ on SCL/SDA to 3V3)

---

## 🎬 Video Preparation

### Video Requirements

1. **Aspect Ratio**: Must be **2:1** (width:height)
   - Example: 256×128, 512×256, 1024×512 pixels
   - Common: **512×256 pixels** (recommended)

2. **Duration**: Keep videos under 2-3 minutes (memory constraints on ESP32)

3. **Format**: MP4, AVI, MOV, or any format supported by OpenCV

### Resizing Your Video

If you have a video with a different aspect ratio, convert it first:

```bash
# Using FFmpeg - add black bars to create 2:1 aspect ratio
ffmpeg -i input_video.mp4 -vf "scale=512:256:force_original_aspect_ratio=decrease,pad=512:256:(ow-iw)/2:(oh-ih)/2" output_video.mp4

# Or crop to 2:1 ratio (removes content)
ffmpeg -i input_video.mp4 -vf "crop=512:256" output_video.mp4
```

### Placing Your Video

1. Place your prepared video file in the project root directory
2. Name it: `second_video.mp4` (or update `VIDEO_PATH` in `video.py`)

---

## 🎞️ Frame Extraction Process

### Step 1: Configure video.py

Open `video.py` and update if needed:

```python
VIDEO_PATH = 'second_video.mp4'      # Your input video file
OUTPUT_FILE = 'frames_data.h'         # Generated output file
WIDTH = 128                           # OLED width (do not change)
HEIGHT = 64                           # OLED height (do not change)
FRAME_DELAY = 16                      # Milliseconds between frames
```

### Step 2: Run Frame Extraction

```bash
# From the project root directory
python video.py
```

**What happens:**
- Reads each frame from your video
- Resizes to 128×64 pixels
- Converts to grayscale
- Applies Otsu's thresholding (automatic black/white optimization)
- Converts binary image data to C++ hex format
- Generates `frames_data.h` file

**Output:**
```
Extracting and processing frames using Otsu's Algorithm...
Formatting 301 frames into flawless C++...
Success! Saved to frames_data.h. Ready to compile in PlatformIO.
```

### Step 3: Copy Generated Data to main.cpp

1. Open the generated `frames_data.h` file
2. Copy the entire `const unsigned char frames[][]...` array
3. Open `src/main.cpp`
4. **REPLACE** the existing frames array with your new one
5. **Keep everything else in main.cpp unchanged**

---

## 📤 Project Upload

### Step 1: Select Board in PlatformIO

Make sure your `platformio.ini` is configured for ESP32:

```ini
[env:esp32]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 115200
```

### Step 2: Connect ESP32 to Computer

- Plug ESP32 via USB cable
- Check COM port assignment in Device Manager (Windows) or `ls /dev/ttyUSB*` (Linux)

### Step 3: Build & Upload

**Option A: Using PlatformIO in VS Code**
1. Click the PlatformIO icon in the sidebar
2. Click **Upload** or press `Ctrl+Alt+U`
3. Wait for compilation and upload to complete

**Option B: Using Terminal**
```bash
platformio run --target upload
```

### Step 4: Monitor Serial Output

```bash
platformio device monitor --baud 115200
```

You should see initialization messages on the serial monitor.

---

## ✅ Verification

Once uploaded:

1. **Check OLED Display**: Animation should play on your connected OLED
2. **Check Speed**: If animation is too fast/slow, adjust `FRAME_DELAY` in `video.py`
3. **Check Quality**: If image quality is poor, re-run `video.py` or adjust video lighting

---

## 🐛 Troubleshooting

### OLED Display Not Showing

- **Check connections**: Verify all 4 wires are firmly connected
- **Test I2C address**: Run an I2C scanner to confirm address is `0x3C`
- **Check power**: Ensure 3.3V is stable with a multimeter
- **Enable pull-ups**: If using long wires, add 4.7kΩ pull-up resistors to SCL/SDA

### Animation Looks Corrupted

1. Re-run `video.py` with your video file
2. Verify video file is valid (try playing it)
3. Ensure aspect ratio is exactly 2:1
4. Check available ESP32 flash memory

### Upload Fails

- Update PlatformIO: `platformio update`
- Check USB cable (try different port)
- Try erasing ESP32: `platformio run --target erase`
- Verify board selection in `platformio.ini`

### Video Processing Error

```bash
# Install required Python packages
pip install opencv-python numpy
```

### Memory Issues

If you get "partition full" errors:
- Reduce video length
- Reduce FRAME_DELAY (skip frames)
- Use a lower resolution source video

---

## 📊 Frame Data Size Reference

| Resolution | Bytes/Frame | Max Frames * |
|------------|-------------|-------------|
| 128×64     | 1024 bytes  | ~3000-4000  |

*Depends on available flash memory and partition configuration

---

## 🔄 Workflow Summary

```
1. Prepare video (2:1 aspect ratio)
   ↓
2. Run python video.py
   ↓
3. Generated frames_data.h
   ↓
4. Copy array to src/main.cpp
   ↓
5. Upload to ESP32 via PlatformIO
   ↓
6. Watch animation play on OLED!
```

---

## 📝 Notes

- The OLED display uses I2C communication (SCL on GPIO 22, SDA on GPIO 21)
- Frames are stored in PROGMEM to save RAM
- Otsu's thresholding automatically optimizes black/white levels per frame
- Each frame is 1024 bytes (128×64 ÷ 8 bits)

---

## 📚 Additional Resources

- [Adafruit SSD1306 Library](https://github.com/adafruit/Adafruit_SSD1306)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [OpenCV Python Documentation](https://docs.opencv.org/)

---

**Happy animating! 🎉**
