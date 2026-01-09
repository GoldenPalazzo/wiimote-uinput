# wiimote-uinput

usermode driver for Nintendo Wiimote controllers using the uinput kernel
module.

## Building

`libudev-dev` is required to build this project.

It's used to scan already connected devices and to monitor for newly connected
ones.

On Debian-based systems, you can install it with:

```sh
sudo apt-get install libudev-dev
```

On Arch-based systems, it should be included in the already present `systemd`
package.

To build the project, simply run:

```sh
make
```

The binary should be created as `build/wiimote-uinput`.

## Usage

```sh
./wiimote-uinput
```

The program will scan for already connected Wiimotes and actively monitor
new connections. It will create a virtual input device for each connected
Wiimote.

It is strongly suggested writing a udev rule to access `/dev/hidraw*` devices
and `/dev/uinput` without root privileges.

## Features

- Supports up to 4 connected Wiimotes.
- Wiimote:
    - Buttons
    - D-Pad
- Nunchuck:
    - Buttons
    - Analog stick
- Classic Controller:
    - Buttons
    - Analog sticks
    - Triggers

## License

This project is licensed under the GPLv2 license. See the [`COPYING`](COPYING) file for
details.
