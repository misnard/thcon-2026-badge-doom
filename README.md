# THCon 2026 Badge DOOM

This repository contains a self-contained Arduino/ESP32-C6 port of DOOM for the
THCon 2026 badge.

The badge port lives in `embedded_doom_badge/`. It is derived from
[embeddedDOOM](https://github.com/cnlohr/embeddedDOOM), with the host/Linux
parts removed and badge-specific video, input, memory, and baked asset support
merged directly into the sketch tree.

## Requirements

- `arduino-cli`
- ESP32 Arduino core with the `esp32:esp32:esp32c6` board package
- A THCon 2026 badge connected over USB serial

## Build And Flash

List connected boards:

```bash
make ports
```

Build the sketch:

```bash
make build
```

Build and flash to the first detected serial port:

```bash
make upload
```

Or choose the port explicitly:

```bash
make upload PORT=/dev/ttyUSB0
```

## Play

After flashing, relay keyboard input to the badge serial console:

```bash
make play PORT=/dev/ttyUSB0
```

Controls:

- `W`, `A`, `S`, `D`: move and turn
- `F`: fire
- `E`: use/open
- `Enter` / `Esc`: menus
- `G`: cycle OLED gamma
- `m`: cycle OLED render mode
- `[` / `]`: shift render Y offset
- `Ctrl-]`: quit the relay

## Project Layout

- `embedded_doom_badge/`: Arduino sketch and integrated DOOM source.
- `embedded_doom_badge/support/`: baked map, texture, and WAD data used by the
  badge build.
- `scripts/play.py`: host-side serial keyboard relay.
- `Makefile`: build, upload, monitor, and play shortcuts.

## Credits And License Notes

This project is based on
[cnlohr/embeddedDOOM](https://github.com/cnlohr/embeddedDOOM), a memory-focused
embedded port of the DOOM source code.

The OLED grayscale, gamma, and dithering work was informed by
[DOOM on a watch](https://jborza.com/post/2020-11-20-doom-on-a-watch/) by
Juraj Borza.

embeddedDOOM itself is derived from the id Software DOOM source release and uses
DOOM shareware data. The id Software limited-use license text is included in
[`LICENSES/ID_SOFTWARE_DOOM_LICENSE.txt`](LICENSES/ID_SOFTWARE_DOOM_LICENSE.txt).
