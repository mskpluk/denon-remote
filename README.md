# Denon Remote

A Qt 6 desktop remote control for Denon and Marantz network receivers.
Developed against an AVR-X1400H.

## What it does differently

The receiver announces every change on its own control port. This application
keeps one connection open and treats it as the event source, so turning the
front panel knob, pressing the infrared remote or changing the volume from the
HEOS app all move the slider in the window. Nothing is polled, and no value in
the interface is ever a guess about what the receiver did with a command.

The input list is read from the receiver too. `SSFUN` reports the label
configured for each input and `SSSOD` reports which inputs are still enabled,
so the drop-down shows exactly the inputs that receiver has, under the names
its owner gave them.

## Features

- Main zone and zone 2: power, volume in decibels, mute, input selection
- Surround modes and the four Quick Select presets
- Bass, treble and subwoofer trim, dynamic range compression, Restorer,
  ECO mode, sleep timer
- Cursor control for the on-screen setup menu
- Receiver discovery on the local network, with manual entry as a fallback
- Tray icon for power, standby and mute; the mouse wheel over it changes the
  volume, half a decibel per notch as on the front panel knob; optional
  close-to-tray
- Automatic reconnection with a capped backoff

## Building

Needs Qt 6.4 or newer and KStatusNotifierItem from KDE Frameworks 6. The tray
icon uses the latter because Qt's own StatusNotifierItem implementation ignores
the mouse wheel.

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-svg-plugins \
    libkf6statusnotifieritem-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/denon-remote
```

## Debian package

```sh
sudo apt install debhelper devscripts
dpkg-buildpackage -us -uc -b
sudo apt install ../denon-remote_*_amd64.deb
```

Release packages are built with `scripts/build-deb-trixie.sh` instead, inside a
rootless Podman container running Debian 13.0. `dpkg-shlibdeps` records the
library versions the build linked against as the minimum in `Depends:`, so a
build on a newer system would ask for libraries no released Debian 13 has. The
script then checks that the package installs on current Debian 13 and on
Ubuntu 26.04 LTS.

## Protocol notes

Commands are plain text terminated with a carriage return on TCP port 23.
The parts this application relies on:

| Command        | Meaning                                            |
|----------------|----------------------------------------------------|
| `PWON` / `PWSTANDBY` | Main power                                   |
| `MV45` / `MV455`     | Volume; three digits mean a half step. 80 is 0 dB |
| `MVMAX 98`     | Ceiling configured on the receiver                 |
| `MUON` / `MUOFF`     | Mute                                         |
| `SISAT/CBL`    | Input select                                       |
| `MSDOLBY SURROUND`   | Surround mode                                |
| `MSQUICK1`     | Recall Quick Select preset                         |
| `PSBAS 50`     | Bass, centred on 50                                |
| `PSTONE CTRL ON`     | Tone control, without which bass and treble are ignored |
| `Z2ON`, `Z240`, `Z2MUON`, `Z2SOURCE` | Zone 2 power, volume, mute, input |
| `SSFUN ?`      | Input labels configured on the receiver             |
| `SSSOD ?`      | Which inputs are enabled                            |

Queries append `?`; parameters under `PS` need a space first, as in `PSBAS ?`.
The receiver drops commands sent back to back, so they are paced roughly 80 ms
apart.

A command the receiver will not accept produces no reply at all — no error, no
echo. Bass and treble behave this way whenever tone control is off, and so does
subwoofer trim whenever `PSSWL` reports `OFF`, which is a setting in the
receiver's own setup menu and cannot be changed remotely. Those sliders are
therefore disabled until the receiver says it will listen, rather than left
looking usable while doing nothing. The same rule is worth remembering when
adding controls: verify against the hardware that a command is echoed back,
because silence is what failure looks like.

The application never sends anything the user did not ask for, apart from the
queries that read the state right after connecting. A periodic query could
count as activity and keep the receiver from dropping into auto-standby, so a
dead link is detected with TCP keepalive instead (about 90 seconds), which the
receiver's network stack answers without the control protocol ever seeing it.

The receiver accepts a limited number of control connections, and this
application holds one for as long as it runs — including while it sits closed
to the tray. Quit it rather than hiding it if another controller needs the port.

## Layout

| File | Role |
|------|------|
| `src/AvrConnection.*` | Connection, reconnection, command pacing, line framing |
| `src/AvrState.*`      | Parses the event stream into mirrored state |
| `src/Discovery.*`     | SSDP search, confirmed by fetching `Deviceinfo.xml` |
| `src/ZonePanel.*`     | Controls for one zone; used for both |
| `src/SoundPanel.*`    | Surround modes, tone, standing options |
| `src/ConnectDialog.*` | Receiver picker |
| `src/MainWindow.*`    | Tabs, menus, tray icon, settings |

`legacy/denon_control.py` is the single-window GTK 3 script this application
replaced. It is kept for reference only and is not built or packaged.

## License

GPL-2.0-or-later; see [LICENSE](LICENSE).
