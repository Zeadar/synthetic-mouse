# AGENTS.md

## Purpose
Synthetic Mouse is a small C program that reads events from a chosen input device via libevdev, maps configured evdev inputs to synthetic mouse actions, and can optionally pass the original events through to a cloned uinput device.

## Key Files
- `src/main.c`: Runtime logic. Opens and grabs the source device, creates the synthetic mouse and optional passthrough device, runs the motion thread, and translates configured events.
- `src/parse_config.c`: Token-based parser for `synthetic.conf` that fills `struct conf_data`, including event type/code pairs, optional ranges, and passthrough settings.
- `src/synthetic-mouse.h`: Shared structs and X-macros for holdable actions, clickable actions, and vars.
- `synthetic.conf`: Local config entrypoint; currently a symlink to `synthetic_keyboard.conf`.
- `synthetic_controller.conf`: Controller-oriented sample configuration.
- `synthetic_keyboard.conf`: Keyboard-oriented sample configuration.
- `meson.build`: Meson build definition, install rules, and optional service/config installation.
- `meson_options.txt`: Meson feature toggles for optional install payloads.
- `packaging/systemd/synthetic-mouse.service.in`: Templated systemd unit.
- `packaging/openrc/synthetic-mouse.initd.in`: Templated OpenRC init script.

## Dependencies
- Build-time: `gcc`, `meson`, `ninja`, `pkg-config`, `libevdev` headers (`libevdev-dev` on many distros).
- Run-time: access to `/dev/input/*` and uinput (typically root or `input` group, plus udev permissions).

## Build
- Configure debug build: `meson setup build`
- Build: `meson compile -C build`
- Configure release build in a fresh or wiped build dir: `meson setup build --wipe --buildtype=release`
- Install: `meson install -C build`
- Optional install payloads: `-Dinstall_config_files=true`, `-Dinstall_systemd_unit=true`, `-Dinstall_openrc_service=true`
- Clean build dir: `rm -rf build`

Binary output: `build/synthetic-mouse`

## Run
Requires access to input devices and uinput (typically root or input group).
- List devices: `sudo ./build/synthetic-mouse --list-devices`
- Run: `sudo ./build/synthetic-mouse`
- Log key events: `sudo ./build/synthetic-mouse --log-keys`
- Show usage/config search paths: `./build/synthetic-mouse --help`

## Configuration
Config files are searched in this order:
1. `./synthetic.conf`
2. `$XDG_CONFIG_HOME/synthetic-mouse/synthetic.conf` or `~/.config/synthetic-mouse/synthetic.conf`
3. `/etc/synthetic-mouse/synthetic.conf`

Local development typically edits `synthetic.conf`, which in this repo currently points at `synthetic_keyboard.conf`.

- `dev_id`: filename from `/dev/input/by-id/`; this is what `src/main.c` matches against.
- `enable_passthrough`: standalone property that enables creation of the cloned passthrough uinput device.
- `acceleration`, `max_speed`, `break_factor`: motion tuning variables.
- Key bindings use evdev code names from libevdev, including keys/buttons (`KEY_KP8`, `BTN_TR2`) and absolute axes (`ABS_RX`, `ABS_RY`).
- A third token may provide an integer range as `release..press`; the left side becomes `struct key.release`, the right side becomes `struct key.press`.
- Prefix a binding value with `!` to pass the matching source event through as well as handling it.
- Default variable values if omitted are `acceleration=0.5`, `break_factor=0.25`, `max_speed=12`, and `wheel=120`.

## Conventions and Notes
- Uses libevdev and libevdev-uinput.
- `current_device` is opened with `O_RDWR`, and is grabbed with `LIBEVDEV_GRAB`.
- Passthrough device creation is conditional on `enable_passthrough`, and uses `libevdev_uinput_create_from_device`.
- Passthrough preserves event framing by buffering `EV_MSC` and flushing queued frames on `EV_SYN/SYN_REPORT`.
- Motion thread runs at 100 Hz, emits `EV_REL` movement and hi-res wheel events, exits after about 60 seconds of idle input, and is restarted on demand.
- `struct key` stores `ev_code`, `ev_type`, `is_pass`, `release`, and `press`.
- Matching in `src/main.c` is done on both `ev.type` and `ev.code`, not just the code.
- Config parsing is token-based, whitespace-delimited, and supports `#` comments.
- Device discovery scans `/dev/input/by-id/` (see `EVENTPATH` in `src/main.c`).
- Holdable actions drive `motion_state[]`; clickable actions emit mouse button events defined in `X_FOR_EACH_CLICKABLE`.
- Clickable output button codes are centralized in `src/synthetic-mouse.h` via X-macros and reused in `src/main.c`.
- `--quiet` suppresses config summary output and some non-critical diagnostics.
- When passthrough is enabled, handled events are consumed unless the binding is prefixed with `!`; unhandled events are forwarded to the passthrough device.

## Common Changes
- Adding new holdable actions: update `X_FOR_EACH_HOLDABLE` in `src/synthetic-mouse.h`, then extend movement/scroll handling in `src/main.c` and add sample config entries as needed.
- Adding new clickable actions: update `X_FOR_EACH_CLICKABLE` in `src/synthetic-mouse.h`; this automatically feeds IDs and output button mapping, but `src/main.c` behavior may still need adjustment.
- Adding new bindable actions generally means updating the relevant X-macro, then checking parser output and runtime handling.
- Changing motion behavior: adjust `mouse_handler()` in `src/main.c`.
- Changing config syntax: update `src/parse_config.c` and refresh the examples in `synthetic.conf`, `synthetic_controller.conf`, and `synthetic_keyboard.conf`.
- Changing install behavior: update `meson.build`, `meson_options.txt`, and any affected files under `packaging/`.
- Changing system service behavior: update the relevant file under `packaging/systemd/` or `packaging/openrc/` and verify `meson install` output.

## Testing
No automated tests are present.
- Basic verification: `meson setup build && meson compile -C build`
- Manual verification: run `sudo ./build/synthetic-mouse --list-devices`, confirm `dev_id`, then run with the intended config and observe pointer/button behavior.
- `--log-keys` is useful when discovering event type/code names and axis ranges.
- Installation verification: `meson setup build --wipe -Dinstall_config_files=true -Dinstall_systemd_unit=true -Dinstall_openrc_service=true && meson install -C build --destdir /tmp/synthetic-mouse-stage`
- Inspect staged installs under `/tmp/synthetic-mouse-stage/usr/local/` for the binary, config files, systemd unit, and OpenRC script.
