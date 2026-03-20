# Synthetic Mouse

Synthetic Mouse is a small C utility for Linux that turns events from a real evdev input device into synthetic mouse input.

It is intended for setups where a keyboard, controller, or other input device should act like a mouse. The program reads a selected device from `/dev/input/by-id/` through `libevdev`, translates configured source events into pointer movement, wheel input, and button clicks, and can optionally pass original events through to a cloned `uinput` device.

## Description

Synthetic Mouse can:

- map keyboard keys, controller buttons, or controller axes to mouse movement
- map inputs to left, right, middle, back, and forward mouse buttons
- map inputs to vertical wheel scrolling using high-resolution wheel events
- support analog-style axis ranges such as `128..255`
- clone the original input device and forward unhandled events to it
- also forward handled events when a binding is marked with `!`
- search for configuration in local, user, and system locations

At runtime, Synthetic Mouse:

1. opens a source evdev device from `/dev/input/by-id/`
2. grabs that device with `LIBEVDEV_GRAB`
3. creates a synthetic mouse through `libevdev-uinput`
4. optionally creates a passthrough `uinput` clone of the original device
5. parses configured bindings and translates matching input events
6. runs a 100 Hz motion thread while movement-related actions are active

Holdable actions such as `up`, `left`, or `scroll_down` drive a shared motion state. Clickable actions emit synthetic button press and release events immediately.

## Build Dependencies

You need:

- `gcc`
- `meson`
- `ninja`
- `pkg-config`
- `libevdev` development headers and library

### Installing Build Dependencies

Package names vary by distribution, but the required pieces are the same:

- a C compiler toolchain
- Meson
- Ninja
- pkg-config
- `libevdev` development headers

#### Debian / Ubuntu

```sh
sudo apt update
sudo apt install build-essential meson ninja-build pkg-config libevdev-dev
```

#### Fedora

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config libevdev-devel
```

#### Arch Linux

```sh
sudo pacman -S base-devel meson ninja pkgconf libevdev
```

#### Gentoo

```sh
sudo emerge --ask sys-devel/gcc dev-build/meson dev-build/ninja dev-util/pkgconf dev-libs/libevdev
```

If Meson fails to find `libevdev`, verify that the development package is installed, not just the runtime library.

## Build Instructions

Debug build:

```sh
meson setup build
meson compile -C build
```

Release build:

```sh
meson setup build --wipe --buildtype=release
meson compile -C build
```

Why `--wipe`?

- If you already have an older `build/` directory, wiping avoids stale Meson metadata when switching build options or newly added project options.

Binary output:

```text
build/synthetic-mouse
```

## Runtime Instructions

Runtime requirements:

- Linux `evdev`
- `uinput`
- permission to open the chosen source device and create virtual input devices

On many distributions that means:

- running as `root`, or
- running with the right `input`/`uinput` group membership and udev rules

### Command Line Usage

```text
synthetic-mouse [options]
```

Available options:

- `--list-devices` list visible `/dev/input/by-id/` devices and exit
- `--log-keys` print source events and passthrough events
- `--quiet` suppress non-critical diagnostics
- `--help` show usage text

Examples:

```sh
./build/synthetic-mouse --help
sudo ./build/synthetic-mouse --list-devices
sudo ./build/synthetic-mouse --log-keys
sudo ./build/synthetic-mouse
```

### Configuration

#### Config Search Order

The program searches for configuration in this order:

1. `./synthetic.conf`
2. `$XDG_CONFIG_HOME/synthetic-mouse/synthetic.conf`
3. `~/.config/synthetic-mouse/synthetic.conf`
4. `/etc/synthetic-mouse/synthetic.conf`

That makes local testing easy from the repo root while installed deployments can use XDG or system config locations.

#### Config Format

The parser is token-based and whitespace-delimited.

- `#` starts a comment
- standalone `enable_passthrough` enables passthrough mode
- key/value variables are floats
- input bindings use evdev names
- optional ranges are written as `release..press`
- prefix a binding with `!` to both handle it and pass the original event through

Example:

```conf
dev_id usb-Sony_Computer_Entertainment_Wireless_Controller-if03-event-joystick

acceleration 0.5
max_speed 20
break_factor 0.2
wheel 60

left        ABS_RX 128..0
right       ABS_RX 128..255
up          ABS_RY 128..0
down        ABS_RY 128..255
left_click  BTN_TR2
right_click BTN_TL2
```

#### Available Variables

- `acceleration`: how quickly pointer speed ramps up
- `max_speed`: movement speed cap
- `break_factor`: multiplier applied while `mouse_break` is active
- `wheel`: scroll magnitude used for `REL_WHEEL_HI_RES`

Default values when omitted:

- `acceleration = 0.5`
- `break_factor = 0.25`
- `max_speed = 12`
- `wheel = 120`

#### Available Actions

Holdable actions:

- `up`
- `down`
- `left`
- `right`
- `mouse_break`
- `scroll_down`
- `scroll_up`

Clickable actions:

- `scroll_click`
- `right_click`
- `left_click`
- `backward`
- `forward`

#### Binding Semantics

Digital keys and buttons:

```conf
left_click KEY_KP0
mouse_break ! KEY_LEFTSHIFT
```

Axis-driven bindings:

```conf
left  ABS_RX 128..0
right ABS_RX 128..255
```

The left side of the range is the release value. The right side is the press value.

For example:

- `128..0` means stronger activation as the axis moves from `128` toward `0`
- `128..255` means stronger activation as the axis moves from `128` toward `255`

### Example Configurations

#### Keyboard Example

`synthetic_keyboard.conf` maps numpad keys to mouse movement and buttons:

```conf
dev_id   usb-SteelSeries_SteelSeries_Apex_3-event-kbd

acceleration  0.5
max_speed     20
break_factor  0.2
wheel         40

enable_passthrough

up            KEY_KP8
down          KEY_KP5
left          KEY_KP4
right         KEY_KP6
right_click   KEY_KPPLUS
left_click    KEY_KP0
scroll_down   KEY_KP3
scroll_up     KEY_KP1
scroll_click  KEY_KP2
backward      KEY_KP7
forward       KEY_KP9
mouse_break   ! KEY_LEFTSHIFT
```

#### Controller Example

`synthetic_controller.conf` maps a controller stick and buttons:

```conf
dev_id   usb-Sony_Computer_Entertainment_Wireless_Controller-if03-event-joystick

acceleration  0.5
max_speed     20
break_factor  0.2
wheel         60

left          ABS_RX     128..0
right         ABS_RX     128..255
up            ABS_RY     128..0
down          ABS_RY     128..255
right_click   BTN_TL2
left_click    BTN_TR2
scroll_up     ABS_Y      128..0
scroll_down   ABS_Y      128..255
scroll_click  BTN_THUMBL
backward      BTN_TL
forward       BTN_TR
mouse_break   BTN_WEST
```

### Passthrough Mode

If `enable_passthrough` is present in the config, Synthetic Mouse creates a cloned virtual input device from the source device capabilities.

Behavior:

- unhandled source events are forwarded to the passthrough device
- handled events are normally consumed
- handled events prefixed with `!` are both handled and passed through

Implementation detail:

- passthrough event framing is preserved by buffering `EV_MSC` and flushing on `EV_SYN/SYN_REPORT`

This is useful when you want one device to continue behaving like itself while also driving the synthetic mouse.

## Everything Else

### Installation

The binary is installable by default through Meson:

```sh
meson setup build --wipe --buildtype=release
meson compile -C build
meson install -C build
```

#### Optional Install Payloads

Meson project options:

- `-Dinstall_config_files=true`
- `-Dinstall_systemd_unit=true`
- `-Dinstall_openrc_service=true`

Full example:

```sh
meson setup build --wipe --buildtype=release \
  -Dinstall_config_files=true \
  -Dinstall_systemd_unit=true \
  -Dinstall_openrc_service=true
meson compile -C build
meson install -C build
```

#### Installed Paths

With default prefix settings, the install layout is:

- binary: `/usr/local/bin/synthetic-mouse`
- config directory: `/usr/local/etc/synthetic-mouse/`
- systemd unit: `/usr/local/lib64/systemd/system/synthetic-mouse.service`
- OpenRC script: `/usr/local/etc/init.d/synthetic-mouse`

Installed config payloads:

- `/usr/local/etc/synthetic-mouse/synthetic.conf`
- `/usr/local/etc/synthetic-mouse/synthetic_controller.conf.sample`
- `/usr/local/etc/synthetic-mouse/synthetic_keyboard.conf.sample`

You can override these base paths with standard Meson install options such as `--prefix`, `--sysconfdir`, and `--libdir`.

### Running as a Service

#### systemd

Install with:

```sh
meson setup build --wipe --buildtype=release -Dinstall_systemd_unit=true
meson compile -C build
sudo meson install -C build
```

Then typically:

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now synthetic-mouse.service
```

The generated unit runs:

```text
ExecStart=/usr/local/bin/synthetic-mouse
```

Make sure the process can access the configured input device and `uinput`.

#### OpenRC

Install with:

```sh
meson setup build --wipe --buildtype=release -Dinstall_openrc_service=true
meson compile -C build
sudo meson install -C build
```

Then typically:

```sh
sudo rc-update add synthetic-mouse default
sudo rc-service synthetic-mouse start
```

The generated init script also runs `/usr/local/bin/synthetic-mouse`.

### Repository Layout

```text
.
├── src/
│   ├── main.c
│   ├── parse_config.c
│   └── synthetic-mouse.h
├── packaging/
│   ├── openrc/
│   │   └── synthetic-mouse.initd.in
│   └── systemd/
│       └── synthetic-mouse.service.in
├── synthetic.conf
├── synthetic_controller.conf
├── synthetic_keyboard.conf
├── meson.build
└── meson_options.txt
```

Notes:

- `synthetic.conf` is the local development config entrypoint used first at runtime
- in the current repository it may be a symlink to one of the sample configs
- `synthetic_controller.conf` and `synthetic_keyboard.conf` are example mappings

### Development Notes

- source files live under `src/`
- build logic and install rules live in `meson.build`
- optional install toggles are declared in `meson_options.txt`
- service templates live under `packaging/`
- holdable and clickable actions are defined centrally in `src/synthetic-mouse.h` through X-macros

When adding a new bindable action:

1. update the relevant X-macro set in `src/synthetic-mouse.h`
2. update behavior in `src/main.c` if needed
3. verify parser output in `src/parse_config.c`
4. refresh the sample configs

### Testing and Verification

There is currently no automated test suite.

Recommended verification flow:

```sh
meson setup build --wipe --buildtype=release
meson compile -C build
./build/synthetic-mouse --help
sudo ./build/synthetic-mouse --list-devices
sudo ./build/synthetic-mouse --log-keys
```

For install verification without touching the live system:

```sh
meson setup build --wipe --buildtype=release \
  -Dinstall_config_files=true \
  -Dinstall_systemd_unit=true \
  -Dinstall_openrc_service=true
meson install -C build --destdir /tmp/synthetic-mouse-stage
find /tmp/synthetic-mouse-stage -type f | sort
```

### Troubleshooting

#### Device Not Found

If the program says the configured device was not found:

- run `sudo ./build/synthetic-mouse --list-devices`
- copy the exact `dev_id` from `/dev/input/by-id/`
- update `synthetic.conf`

#### Permission Errors

If opening the source device or creating the virtual device fails:

- run as `root` first to confirm it is a permissions issue
- check access to `/dev/input/event*`
- check that `uinput` is available and permitted on your system

#### No Movement or Wrong Axis Direction

- verify bindings with `--log-keys`
- confirm the evdev names are correct
- adjust axis ranges such as `128..0` vs `128..255`
- tune `acceleration`, `max_speed`, `break_factor`, and `wheel`

#### Config Not Being Picked Up

Remember the lookup order:

1. local `./synthetic.conf`
2. XDG config location
3. `/etc/synthetic-mouse/synthetic.conf`

If local testing behaves differently from an installed service, you are probably using different config files.

### Limitations

- Linux-only
- no GUI configuration tool
- no automated tests yet
- requires evdev and uinput access
- the device match is based on the `/dev/input/by-id/` filename, not a more abstract device selector

### License

No license file is currently present in this repository. Add one before publishing if you want clear reuse terms.
