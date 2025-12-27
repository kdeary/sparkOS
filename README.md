spark! Console Firmware (ESP-IDF)
=================================

Firmware for running spark! cartridges on ESP32 hardware. The runtime boots a `.sprk` cart from the microSD card, executes it via sparkEngine, and renders the 320x240 framebuffer directly to an ST7796 LCD (no LVGL, no touch UI). Inputs and audio are not wired yet; this build focuses on display bring-up.

Quick start
-----------
- Put your `.sprk` cartridge on the microSD card (mounted at `/carts`) and set the boot target in `/carts/.spark`, e.g.:
  ```
  CART_FILE=example.sprk
  ```
  (`ROM_FILE=...` is also accepted for backward compatibility.)
- Build/flash/monitor:
  ```
  idf.py -p PORT flash monitor
  ```
  (Requires ESP-IDF.)

Display
-------
- LCD: ILI9341 over SPI; frames render at 320x240.
- Defaults live in `main/Kconfig.projbuild` (`CONFIG_LCD_*`). Adjust via `menuconfig` if needed:
  - SCLK=12, MOSI=11, MISO=13, DC=46, CS=10, RST=NC, BL=45 (active high)
  - Size: 320x240, offsets 0,0, pixel clock 40 MHz
  - Uses HSPI by default (SPI2_HOST) to leave VSPI free for SD.

microSD (SPI mode)
------------------
- The microSD card is mounted at `/carts` using SDSPI + FATFS.
- Defaults live in `main/Kconfig.projbuild` (`CONFIG_SD_SPI_*`):
  - SCLK=18, MOSI=23, MISO=19, CS=5 (VSPI pins from the provided ESP32 pinout)
  - Uses VSPI by default (SPI3_HOST) to avoid sharing the LCD bus.

Filesystem/partition
--------------------
- A microSD card is mounted at `/carts`; no flash filesystem is used for carts.
- Boot config: `/carts/.spark`
  ```
  # Boot cart path relative to /carts
  CART_FILE=example.sprk
  ```

Project layout
--------------
- `main/spark_pico.c` – app entry, SD card mount, boot config parsing, sparkEngine runtime, ST7796 driver, framebuffer render loop.
- `sparkEngine/` – spark! runtime and tooling.
- `partitions.csv` – partition table for the ESP32 app.

Notes
-----
- Input and audio are not implemented yet.
