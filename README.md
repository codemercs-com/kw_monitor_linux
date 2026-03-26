# kw_monitor

A Linux command-line tool to monitor raw HID reports from [Code Mercenaries KeyWarrior](https://www.codemercs.com/en/keyboard) devices (USB Vendor ID `07C0`).

## Features

- Enumerates all connected KeyWarrior devices (VID `07C0`) and lets you pick one interactively
- Reads raw HID reports via `/dev/hidrawX` (using the [hidapi](https://github.com/libusb/hidapi) library)
- Displays each report as hex + ASCII
- **Highlights changed bytes in green** (ANSI color) compared to the previous report
- Exclusively grabs the corresponding `/dev/input/eventX` nodes via `EVIOCGRAB` so keypresses do not leak to the terminal while monitoring

## Requirements

- Linux (hidraw + evdev)
- `g++` with C++17 support
- `libudev-dev`

```
sudo apt install build-essential libudev-dev   # Debian / Ubuntu
```

## Build

```
make          # produces ./kw_monitor
make clean    # remove object files and binary
```

## Usage

```
./kw_monitor
```

 Without a udev rule the hidraw node is typically only accessible by root:
 ```
 sudo ./kw_monitor
 ```

On startup the tool lists all detected KeyWarrior devices (VID `07C0`):

```
╔══════════════════════════════════════════════════════════════════════╗
║              KeyWarrior Monitor – select device                             ║
╚══════════════════════════════════════════════════════════════════════╝

 Nr   VID     PID     UP    U     Device      Interface              Manufacturer              Product
 ────────────────────────────────────────────────────────────────────────────────────────────────────
 1    07C0    0181    0001  0006  hidraw1     KeyWarrior8            Code Mercenaries          KeyWarrior8

Number and Enter: _
```

After selection, incoming HID reports are printed continuously. Changed bytes are highlighted in green:

```
[00001]   8 B  00 00 00 00 00 00 00 00  |........|
[00002]   8 B  00 00 1E 00 00 00 00 00  |....1...|   ← byte 2 changed (key press)
[00003]   8 B  00 00 00 00 00 00 00 00  |........|   ← byte 2 changed back (key release)
```

Press **Ctrl+C** to stop.

## Unprivileged access (recommended)

### Option A — udev rule (device-specific)

```bash
sudo cp udev/99-keywarrior.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

This grants access to VID `07C0` / PID `0181` for members of the `plugdev` group (default on Ubuntu/Debian). Re-plug the device afterwards.

### Option B — add user to `input` group (covers all input devices)

```bash
sudo usermod -aG input $USER
# log out and back in for the change to take effect
```

## Architecture

```
kw_monitor
├── main.cpp          Device enumeration, evdev grab, monitor loop
├── main.h            DeviceEntry struct, function declarations
├── Makefile
├── deps/
│   └── hidapi/
│       ├── hid.c     Vendored hidapi (hidraw backend)
│       └── hidapi.h
└── udev/
    └── 99-keywarrior.rules
```

Two separate kernel interfaces are used for one device:

| Interface | Node | Purpose |
|-----------|------|---------|
| hidraw | `/dev/hidrawX` | Reading raw HID reports via `hid_open_path()` |
| evdev | `/dev/input/eventX` | Exclusive grab via `EVIOCGRAB` to suppress terminal keypresses |

The evdev nodes are located by walking sysfs: `hidraw node → HID node → USB interface → USB device → all eventX nodes beneath it`.

## Dependencies

The [hidapi](https://github.com/libusb/hidapi) library (`deps/hidapi/`) is vendored as a single C source file (hidraw backend). No dynamic library installation required.

## License

The project source (`main.cpp`, `main.h`) is released under the **MIT License**.
The vendored hidapi library retains its original license (GPL v3 / BSD / hidapi original — see `deps/hidapi/hidapi.h`).

## Links and further information

[Product site](https://www.codemercs.com/en/keyboard) for more information, datasheets, and software/tools for the KeyWarrior devices
[Company site](https://www.codemercs.com) for information on more devices.

## Contact

If you have any questions about the KeyWarrior please contact **support@codemercs.com** or use the [issues](https://github.com/codemercs-com/kw-monitor-linux/issues) section in this repository. There is also a company [forum](https://forum.codemercs.com/) with many solved questions.
