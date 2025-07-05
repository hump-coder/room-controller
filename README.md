# room-controller
Room Controller - Designed for our home.

## Hardware

This project is dedicated to the ESP32‑S3 device that uses an
**AXS15231B** QSPI display and matching touch controller.  The
display configuration mirrors the working ESPHome example:

```yaml
display:
  - platform: qspi_dbi
    model: axs15231
    data_rate: 40MHz
    cs_pin: 45
    rotation: 90
touchscreen:
  - platform: axs15231
    transform:
      swap_xy: true
      mirror_y: true
```

The old TFT_eSPI based configuration was removed.  The project now
uses [LVGL](https://github.com/lvgl/lvgl) together with Espressif's
`esp_lcd_axs15231b` driver.
