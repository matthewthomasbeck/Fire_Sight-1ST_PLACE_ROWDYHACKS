# Vendored MLX90640 driver (Melexis API + Pimoroni Linux I2C port)
#
# Sources:
#   https://github.com/melexis/mlx90640-library  (Apache-2.0)
#   https://github.com/pimoroni/mlx90640-library (Apache-2.0)
#
# On a Raspberry Pi 4B, physical pins 3 (SDA) and 5 (SCL) are I2C1
# → device node /dev/i2c-1, default sensor address 0x33.
#
# Before first run:
#   sudo raspi-config   # Interface Options → I2C → Enable
#   sudo usermod -aG i2c $USER && # then log out/in
#   i2cdetect -y 1      # should show "33"
