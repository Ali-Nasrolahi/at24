# AT24 Device Driver

- Tested with kernel 6.6
- Built for Raspberry Pi 5
- Use `at24-rpi5-overlay.dts` for enabling the driver probing on I2C-1 bus
- Overlay includes the `compatible` for linux kernel's AT24 official device driver for testing, as well
- `app.c` is a simple C program to showcase the driver usage
- For testing, you're able to use `dd(1)` with `skip` and `seek` to do I/O on specific address.

```bash
dd if=input  of=/dev/eeprom0 seek=3 bs=1 count=5 # For writing to specific address
dd if=/dev/eeprom0 of=output skip=3 bs=1 count=5 # For reading from specific address
```

Please also review related post on my website available at [Ali-Nasrolahi/Portfolio/AT24-Driver](https://ali-nasrolahi.github.io/portfolio/at24-driver/).
