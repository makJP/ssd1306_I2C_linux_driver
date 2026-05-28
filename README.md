## Overview

This project implements a character-level I2C kernel driver for the SSD1306 128×64 OLED module. The driver registers itself with the Linux I2C subsystem, initialises the display hardware through the standard SSD1306 command sequence, and exposes text-rendering, scrolling, contrast, and invert controls — all running in kernel space on a Raspberry Pi 4.

---

## Hardware

| Item | Detail |
|------|--------|
| Board | Raspberry Pi 4 |
| Display | SSD1306 OLED 128×64 |
| Protocol | I2C (400 kHz fast mode) |
| I2C bus | Bus 1 (`/dev/i2c-1`) |
| OLED address | `0x3C` |

### Wiring

```
RPi 4 Pin       SSD1306 Pin
-----------     -----------
Pin 1  (3.3V) → VCC
Pin 6  (GND)  → GND
Pin 3  (SDA1) → SDA
Pin 5  (SCL1) → SCL
```

---

## Project Structure

```
SSD1306_I2C_Device_Driver/
├── oled_driver.c   # kernel module source
├── Makefile        # kernel build system integration
└── README.md       # this file
```

---

## Prerequisites

Make sure the following are in place before building.

**Enable I2C on the Pi:**
```bash
sudo raspi-config
# Interface Options → I2C → Enable
```

**Install kernel headers:**
```bash
sudo apt update
sudo apt install raspberrypi-kernel-headers build-essential
```

**Verify the display is visible on the bus:**
```bash
sudo apt install i2c-tools
i2cdetect -y 1
# You should see 0x3C in the output grid
```

---

## Build

```bash
make
```

This compiles `oled_driver.c` against the currently running kernel tree and produces `oled_driver.ko`.

**Clean build artefacts:**
```bash
make clean
```

---

## Usage

**Load the driver:**
```bash
sudo insmod oled_driver.ko
```

When loaded, the driver initialises the display, starts a left horizontal scroll across the first three pages, and prints a welcome message. Check the kernel log to confirm:

```bash
dmesg | tail
# oled_driver: display probed OK
# oled_driver: loaded
```

**Unload the driver:**
```bash
sudo rmmod oled_driver
```

On removal the display shows a goodbye message, pauses briefly, clears the screen, and powers off. The kernel log will show:

```bash
dmesg | tail
# oled_driver: display removed
# oled_driver: unloaded
```

---

## How It Works

### Driver registration

The module calls `i2c_get_adapter()` to obtain a handle to I2C bus 1, registers the SSD1306 as a new I2C device at address `0x3C`, then calls `i2c_add_driver()` to bind the driver. The I2C core invokes `oled_probe()` when the device is matched.

### Initialisation sequence

`oled_init_display()` sends the standard SSD1306 startup commands in order — oscillator frequency, multiplex ratio, display offset, charge pump, addressing mode, COM scan direction, contrast, pre-charge period, and VCOMH level — before turning the display on and clearing the framebuffer.

### I2C framing

The SSD1306 distinguishes commands from data via a one-byte control prefix sent before every payload byte:

| Control byte | Meaning |
|---|---|
| `0x00` | Following byte is a command |
| `0x40` | Following byte is display RAM data |

`oled_send()` handles this automatically based on the `cmd_mode` flag.

### Font rendering

Characters are stored as 5-column bitmaps in `font5x7[]`, indexed from ASCII `0x20` (space). `oled_putchar()` subtracts `0x20` from the ASCII code, writes the five column bytes plus one blank spacer column, and handles line-wrap and newline automatically.

### Addressing mode

The driver uses **horizontal addressing mode** (`0x00`). After each byte written to display RAM the column pointer advances automatically, wrapping to the next page at the end of a row — which maps cleanly onto the sequential writes made by `oled_fill()` and the font renderer.

---

## Key Functions

| Function | Description |
|---|---|
| `oled_init_display()` | Sends startup command sequence, clears screen |
| `oled_set_pos(page, col)` | Moves the hardware pointer to a given page and column |
| `oled_newline()` | Advances to the next page (wraps at page 7) |
| `oled_putchar(ch)` | Renders one ASCII character; handles wrap and `\n` |
| `oled_puts(str)` | Renders a null-terminated string |
| `oled_fill(byte)` | Floods all 1024 pixels with the given byte |
| `oled_invert(bool)` | Toggles display invert mode |
| `oled_set_contrast(val)` | Sets brightness `0x00`–`0xFF` |
| `oled_scroll_h(...)` | Starts a horizontal scroll on a page range |
| `oled_scroll_vh(...)` | Starts a diagonal (vertical + horizontal) scroll |

---

## Configuration

These constants at the top of `oled_driver.c` can be adjusted without changing any logic:

| Constant | Default | Meaning |
|---|---|---|
| `I2C_BUS_NUM` | `1` | I2C bus index |
| `OLED_I2C_ADDR` | `0x3C` | OLED slave address |
| `OLED_COLS` | `128` | Display width in pixels |
| `OLED_PAGES` | `7` | Last page index (display has 8 pages: 0–7) |
| `FONT_W` | `5` | Glyph width in bytes |

---

## Tested On

- **Board:** Raspberry Pi 4 Model B  
- **OS:** Raspberry Pi OS (32-bit)  
- **Kernel:** Linux 5.4.51-v7l+
