# Synthetic Mouse

Synthetic Mouse maps events from one Linux input device to a virtual mouse. Common setups include a keyboard numpad used for pointer control, or a controller used for movement, buttons, and scrolling.

## Requirements

Build requirements:

- `gcc`
- `meson`
- `ninja`
- `pkg-config`
- `libevdev` development headers

Runtime requirements:

- Linux with `evdev`
- `uinput` support
- permission to read the selected `/dev/input` device
- permission to create virtual input devices

Typical runtime access is provided by `root`, or by suitable `udev` rules and group membership for `input` and `uinput`.

Example:

```sh
sudo usermod -aG input <username>
```

## Installing Build Dependencies

Package names vary by distribution.

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install build-essential meson ninja-build pkg-config libevdev-dev
```

### Fedora

```sh
sudo dnf install gcc meson ninja-build pkgconf-pkg-config libevdev-devel
```

### Arch Linux

```sh
sudo pacman -S base-devel meson ninja pkgconf libevdev
```

## Building

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

The binary is written to `build/synthetic-mouse`.

## Installing

Basic install:

```sh
meson setup build --wipe --buildtype=release
meson compile -C build
sudo meson install -C build
```

Optional install payloads:

- `-Dinstall_config_files=true` installs `synthetic.conf`, `synthetic_keyboard.conf.sample`, and `synthetic_controller.conf.sample`
- `-Dinstall_systemd_unit=true` installs the systemd service unit
- `-Dinstall_openrc_service=true` installs the OpenRC service script

Example with all optional payloads:

```sh
meson setup build --wipe --buildtype=release \
  -Dinstall_config_files=true \
  -Dinstall_systemd_unit=true \
  -Dinstall_openrc_service=true
meson compile -C build
sudo meson install -C build
```

With Meson defaults, the main install paths are usually:

- `/usr/local/bin/synthetic-mouse`
- `/etc/synthetic-mouse/synthetic.conf`
- `/etc/synthetic-mouse/synthetic_keyboard.conf.sample`
- `/etc/synthetic-mouse/synthetic_controller.conf.sample`

## Running

Common commands:

```sh
sudo synthetic-mouse --list-devices
sudo synthetic-mouse --log-input
sudo synthetic-mouse --log-output
sudo synthetic-mouse --log-pass
sudo synthetic-mouse
```

Command line options:

- `--list-devices` lists `/dev/input/by-id/` device names and exits
- `--log-input` prints source input events
- `--log-output` prints emitted mouse events
- `--log-pass` prints passthrough events
- `--quiet` suppresses non-critical diagnostics
- `--help` prints usage text

## Configuration

### Search Order

`synthetic.conf` is searched in this order:

1. `./synthetic.conf`
2. `$XDG_CONFIG_HOME/synthetic-mouse/synthetic.conf`
3. `~/.config/synthetic-mouse/synthetic.conf`
4. `/etc/synthetic-mouse/synthetic.conf`

### Format

The file is whitespace-delimited.

- `#` starts a comment
- one property is placed on each line
- action bindings use libevdev event code names such as `KEY_KP8`, `BTN_TR2`, and `ABS_RX`
- axis bindings may include a range written as `release..press`
- prefixing a binding input with `!` keeps the original matching event available in passthrough output

Example:

```conf
dev_id usb-Sony_Computer_Entertainment_Wireless_Controller-if03-event-joystick

acceleration        0.5
max_speed           20
mouse_break_factor  0.2
scroll_break_factor 0.35
wheel               60

left        ABS_RX 128..0
right       ABS_RX 128..255
up          ABS_RY 128..0
down        ABS_RY 128..255
left_click  BTN_TR2
right_click BTN_TL2
```

### Properties

`dev_id`

- required
- must match one filename from `/dev/input/by-id/`
- can be discovered with `synthetic-mouse --list-devices`

Passthrough is enabled by default.

- unbound events continue through the passthrough device
- bound events are consumed unless the binding input is prefixed with `!`
- `toggle_disable` temporarily disables remapping and forwards the source device unchanged until pressed again

### Variables

`acceleration`

- controls how quickly pointer speed ramps up while movement is held

`max_speed`

- sets the pointer speed cap

`mouse_break_factor`

- sets the movement speed multiplier while `mouse_break` is held

`scroll_break_factor`

- sets the scroll speed multiplier while `mouse_break` is held

`wheel`

- sets the wheel amount emitted for each scroll step

Defaults used when omitted:

- `acceleration 0.5`
- `mouse_break_factor 0.25`
- `max_speed 12`
- `scroll_break_factor 0.35`
- `wheel 120`

### Available Actions

Movement and hold actions:

- `up`
- `down`
- `left`
- `right`
- `mouse_break`
- `scroll_up`
- `scroll_down`

Mouse button actions:

- `left_click`
- `right_click`
- `scroll_click`
- `backward`
- `forward`

Function actions:

- `toggle_disable`

### Digital and Analog Bindings

Digital keys and buttons:

```conf
up            KEY_KP8
left_click    KEY_KP0
mouse_break   ! KEY_LEFTMETA
toggle_disable KEY_INSERT
```

Axis-based bindings:

```conf
left        ABS_RX 128..0
right       ABS_RX 128..255
scroll_up   ABS_Y 128..0
scroll_down ABS_Y 128..255
```

Range meaning:

- the left side of `release..press` is the idle or released value
- the right side is the active or pressed value
- `128..0` means activation grows as the axis moves from `128` toward `0`
- `128..255` means activation grows as the axis moves from `128` toward `255`

This format is mainly useful for analog sticks and triggers.

## Tuning Guide

For slower and more precise movement:

- reduce `max_speed`
- reduce `acceleration`

For faster travel across large displays:

- increase `max_speed`
- increase `acceleration`

For finer temporary control:

- bind `mouse_break`
- reduce `mouse_break_factor`
- reduce `scroll_break_factor`

For gentler scrolling:

- reduce `wheel`

For faster scrolling:

- increase `wheel`

## Example Configs

Keyboard example:

```conf
dev_id   usb-SteelSeries_SteelSeries_Apex_3-event-kbd

acceleration        0.3
max_speed           16
mouse_break_factor  0.35
scroll_break_factor 0.35
wheel               40

up             KEY_KP8
down           KEY_KP5
left           KEY_KP4
right          KEY_KP6
right_click    KEY_KPPLUS
left_click     KEY_KP0
scroll_down    KEY_KP3
scroll_up      KEY_KP1
scroll_click   KEY_KP2
backward       KEY_KP7
forward        KEY_KP9
mouse_break    ! KEY_LEFTMETA
toggle_disable KEY_INSERT
```

Controller example:

```conf
dev_id   usb-Sony_Computer_Entertainment_Wireless_Controller-if03-event-joystick

acceleration        0.5
max_speed           20
mouse_break_factor  0.2
scroll_break_factor 0.35
wheel               60

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

## Services

Optional service files are available for systemd and OpenRC.

Systemd install:

```sh
meson setup build --wipe --buildtype=release -Dinstall_systemd_unit=true
meson compile -C build
sudo meson install -C build
sudo systemctl enable --now synthetic-mouse
```

OpenRC install:

```sh
meson setup build --wipe --buildtype=release -Dinstall_openrc_service=true
meson compile -C build
sudo meson install -C build
sudo rc-update add synthetic-mouse default
sudo rc-service synthetic-mouse start
```
