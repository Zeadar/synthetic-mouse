# Synthetic Mouse

Synthetic Mouse maps events from a Linux input device to a virtual mouse. Common setups include a keyboard numpad used as a mouse, or a game controller used for pointer movement, buttons, and scrolling.

The program reads a configuration file, opens one device from `/dev/input/by-id/`, creates a virtual mouse through `uinput`, and applies the selected bindings.

## Requirements

Build requirements:

- `gcc`
- `meson`
- `ninja`
- `pkg-config`
- `libevdev` development headers

Runtime requirements:

- Linux with `evdev`
- `uinput` available
- permission to read the selected `/dev/input` device and create virtual input devices

Typical runtime access is provided by `root`, or by suitable `udev` rules and group membership for input and `uinput`.

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

### Gentoo

```sh
sudo emerge --ask sys-devel/gcc dev-build/meson dev-build/ninja dev-util/pkgconf dev-libs/libevdev
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

## Installing

Basic install:

```sh
meson setup build --wipe --buildtype=release
meson compile -C build
sudo meson install -C build
```

Optional install payloads:

- `-Dinstall_config_files=true` installs the default config and both sample configs under `/etc/synthetic-mouse/`
- `-Dinstall_systemd_unit=true` installs the systemd unit
- `-Dinstall_openrc_service=true` installs the OpenRC service

Example with all optional install files:

```sh
meson setup build --wipe --buildtype=release \
  -Dinstall_config_files=true \
  -Dinstall_systemd_unit=true \
  -Dinstall_openrc_service=true
meson compile -C build
sudo meson install -C build
```

Installed paths depend on Meson `prefix`, `bindir`, and `sysconfdir`. With the default prefix, the main paths are usually:

- `/usr/local/bin/synthetic-mouse`
- `/etc/synthetic-mouse/synthetic.conf`
- `/etc/synthetic-mouse/synthetic_keyboard.conf.sample`
- `/etc/synthetic-mouse/synthetic_controller.conf.sample`

## Running

Available commands:

```sh
sudo synthetic-mouse --list-devices
sudo synthetic-mouse --log-keys
sudo synthetic-mouse
```

Command line options:

- `--list-devices` lists available `/dev/input/by-id/` device names and exits
- `--log-keys` prints incoming source events and passthrough events
- `--quiet` suppresses non-critical diagnostics
- `--help` prints usage text

## Configuration

### Config Search Order

Synthetic Mouse searches for `synthetic.conf` in this order:

1. `./synthetic.conf`
2. `$XDG_CONFIG_HOME/synthetic-mouse/synthetic.conf`
3. `~/.config/synthetic-mouse/synthetic.conf`
4. `/etc/synthetic-mouse/synthetic.conf`

### Config Format

The file is whitespace-delimited.

- `#` starts a comment
- one property is placed on each line
- action bindings use libevdev event code names such as `KEY_KP8`, `BTN_TR2`, `ABS_RX`
- axis bindings may include a range written as `release..press`
- a binding may be prefixed with `!` to handle the event and also pass the original event through
- `enable_passthrough` is a standalone property with no value

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

### Required and Optional Properties

`dev_id`

- required
- must match one filename from `/dev/input/by-id/`
- discoverable through `synthetic-mouse --list-devices`

`enable_passthrough`

- optional, but critical for keyboard-based setups
- when enabled, Synthetic Mouse creates a cloned virtual version of the original device and forwards events into that clone
- when disabled, handled events are consumed and the source device remains grabbed by Synthetic Mouse

Keyboard guidance:

- `enable_passthrough` is effectively mandatory for a keyboard
- without passthrough, the grabbed keyboard stops behaving like a keyboard while Synthetic Mouse is running
- with passthrough enabled, normal typing and non-mouse bindings remain available through the cloned device

Controller guidance:

- `enable_passthrough` is usually undesirable for a controller
- most controller mappings use the device only as a mouse source, so forwarding the full original controller stream often creates duplicate or unwanted gamepad input
- passthrough should be enabled for a controller only when the original controller events are also needed by another application or desktop stack

### Variables

`acceleration`

- controls how quickly pointer speed ramps up while a movement action remains active
- lower values feel steadier and slower to ramp
- higher values feel more aggressive and responsive

`max_speed`

- sets the movement speed cap
- lower values give finer control
- higher values increase top speed across the screen

`break_factor`

- controls the slowdown applied while `mouse_break` is held
- smaller values produce stronger slowing
- larger values keep more of the normal speed

`wheel`

- sets the magnitude used for scroll events
- smaller values feel gentler
- larger values feel faster and coarser

Defaults used when a value is omitted:

- `acceleration 0.5`
- `max_speed 12`
- `break_factor 0.25`
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

### Digital and Analog Bindings

Digital keys and buttons:

```conf
up          KEY_KP8
left_click  KEY_KP0
mouse_break ! KEY_LEFTSHIFT
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

This is mainly useful for analog sticks and triggers.

## Choosing a Config

### Keyboard Setup

Recommended starting point:

- `synthetic_keyboard.conf`

Typical behavior:

- numpad or nearby keys control movement, clicks, and scroll
- `enable_passthrough` should remain enabled
- one modifier key may be marked with `!` so the original key event still passes through

Key discovery workflow:

1. Run `synthetic-mouse --list-devices` and copy the correct keyboard `dev_id`.
2. Run `synthetic-mouse --log-keys`.
3. Press the intended keys and note the printed `KEY_*` names.
4. Place those names in the config file.
5. Keep `enable_passthrough` enabled.

### Controller Setup

Recommended starting point:

- `synthetic_controller.conf`

Typical behavior:

- one analog stick controls pointer movement
- buttons control left and right click
- another stick or axis controls scrolling
- `enable_passthrough` normally stays disabled

Axis discovery workflow:

1. Run `synthetic-mouse --list-devices` and copy the correct controller `dev_id`.
2. Run `synthetic-mouse --log-keys`.
3. Move one stick or trigger at a time.
4. Note the `ABS_*` code names and the observed resting and extreme values.
5. Write bindings with the matching `release..press` direction.

Practical examples:

- centered stick resting near `128`, left edge near `0`: `ABS_RX 128..0`
- centered stick resting near `128`, right edge near `255`: `ABS_RX 128..255`
- trigger resting at `0`, fully pressed at `255`: `ABS_Z 0..255`

## Tuning Guide

For slower and more precise movement:

- reduce `max_speed`
- reduce `acceleration`

For faster travel across large displays:

- increase `max_speed`
- increase `acceleration`

For finer temporary control:

- bind `mouse_break`
- lower `break_factor`

For gentler scrolling:

- reduce `wheel`

For faster scrolling:

- increase `wheel`

## Example Configs

Keyboard example:

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

Controller example:

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
