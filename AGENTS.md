# AGENTS.md

## Purpose
Synthetic Mouse is a small C program that reads events from a chosen input device via libevdev, maps configured evdev inputs to synthetic mouse actions, and can optionally pass the original events through to a cloned uinput device.

## Key Files
- `main.c`: Runtime logic. Opens and grabs the source device, creates the synthetic mouse and optional passthrough device, runs the motion thread, and translates configured events.
- `parse_config.c`: Token-based parser for `synthetic.conf` that fills `struct conf_data`, including event type/code pairs, optional ranges, and passthrough settings.
- `synthetic-mouse.h`: Shared structs and X-macros for holdable actions, clickable actions, and vars.
- `synthetic.conf`: Active configuration file.
- `synthetic_controller.conf`: Sample/controller-oriented configuration.
- `synthetic_keyboard.conf`: Sample/keyboard-oriented configuration.
- `meson.build`: Meson build definition for the `out` executable.

## Dependencies
- Build-time: `gcc`, `meson`, `ninja`, `pkg-config`, `libevdev` headers (`libevdev-dev` on many distros).
- Run-time: access to `/dev/input/*` and uinput (typically root or `input` group, plus udev permissions).

## Build
- Configure debug build: `meson setup build`
- Build: `meson compile -C build`
- Configure release build: `meson setup build --buildtype=release --reconfigure`
- Clean build dir: `rm -rf build`

Binary output: `build/out`

## Run
Requires access to input devices and uinput (typically root or input group).
- List devices: `sudo ./build/out --list-devices`
- Run: `sudo ./build/out`
- Log key events: `sudo ./build/out --log-keys`

## Configuration
Edit `synthetic.conf`:
- `dev_id`: filename from `/dev/input/by-id/`; this is what `main.c` matches against.
- `enable_passthrough`: standalone property that enables creation of the cloned passthrough uinput device.
- `acceleration`, `max_speed`, `break_factor`: motion tuning variables.
- Key bindings use evdev code names from libevdev, including keys/buttons (`KEY_KP8`, `BTN_TR2`) and absolute axes (`ABS_RX`, `ABS_RY`).
- A third token may provide an integer range as `release..press`; the left side becomes `struct key.release`, the right side becomes `struct key.press`.
- Prefix a binding value with `!` to pass the matching source event through as well as handling it.

## Conventions and Notes
- Uses libevdev and libevdev-uinput.
- `current_device` is opened with `O_RDWR`, and is grabbed with `LIBEVDEV_GRAB`.
- Passthrough device creation is conditional on `enable_passthrough`, and uses `libevdev_uinput_create_from_device`.
- Motion thread runs at 100 Hz, emits `EV_REL` and synthetic button events, exits after about 60 seconds of idle input, and is restarted on demand.
- `struct key` stores `ev_code`, `ev_type`, `is_pass`, `release`, and `press`.
- Matching in `main.c` is done on both `ev.type` and `ev.code`, not just the code.
- Config parsing is token-based, whitespace-delimited, and supports `#` comments.
- Device discovery scans `/dev/input/by-id/` (see `EVENTPATH` in `main.c`).
- Holdable actions drive `motion_state[]`; clickable actions emit mouse button events defined in `X_FOR_EACH_CLICKABLE`.
- Clickable output button codes are centralized in `synthetic-mouse.h` via X-macros and reused in `main.c`.
- When passthrough is enabled, handled events are consumed unless the binding is prefixed with `!`; unhandled events are forwarded to the passthrough device.

## Common Changes
- Adding new holdable actions: update `X_FOR_EACH_HOLDABLE` in `synthetic-mouse.h`, then extend movement/scroll handling in `main.c` and add sample config entries as needed.
- Adding new clickable actions: update `X_FOR_EACH_CLICKABLE` in `synthetic-mouse.h`; this automatically feeds IDs and output button mapping, but `main.c` behavior may still need adjustment.
- Adding new bindable actions generally means updating the relevant X-macro, then checking parser output and runtime handling.
- Changing motion behavior: adjust `mouse_handler()` in `main.c`.
- Changing config syntax: update `parse_config.c` and refresh the examples in `synthetic.conf`, `synthetic_controller.conf`, and `synthetic_keyboard.conf`.

## Testing
No automated tests are present.
- Basic verification: `meson setup build && meson compile -C build`
- Manual verification: run `sudo ./build/out --list-devices`, confirm `dev_id`, then run with the intended config and observe pointer/button behavior.
- `--log-keys` is useful when discovering event type/code names and axis ranges.
