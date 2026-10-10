# Denon Remote

A Qt 6 desktop remote control for Denon and Marantz network receivers.
Developed against an AVR-X1400H.

## How it works

The receiver announces every change on its own control port. This application
keeps one connection open and treats it as the event source, so turning the
front panel knob, pressing the infrared remote or changing the volume from the
HEOS app all move the slider in the window. Nothing is polled, and no value in
the interface is ever a guess about what the receiver did with a command.

The input list is read from the receiver too, under the names its owner gave
the inputs and without the ones hidden in its setup. Older receivers that
don't report it over the control connection are asked for their web page
instead.

## Features

- Main zone, zone 2 and zone 3: power, volume in decibels, mute, input
- Surround modes, Quick Select presets under their names on the receiver
- Bass, treble and subwoofer trim, dynamic range, Restorer, ECO, sleep timer
- Cursor control for the on-screen setup menu
- Receiver discovery on the local network, or a typed address
- Tray icon for power and mute; the mouse wheel over it changes the volume
- Automatic reconnection

Controls for things a model doesn't have, such as zone 3 or a fifth Quick
Select slot, are not shown.

## Building

Needs Qt 6.4 or newer and KStatusNotifierItem from KDE Frameworks 6.

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-svg-plugins \
    libkf6statusnotifieritem-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build
./build/denon-remote
```

## Debian package

```sh
sudo apt install debhelper devscripts
dpkg-buildpackage -us -uc -b
sudo apt install ../denon-remote_*_amd64.deb
```

Release packages are built with `scripts/build-deb-trixie.sh` in a Debian 13.0
container, so they install on every Debian 13 release and on Ubuntu 26.04.

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
| `Z2ON`, `Z240`, `Z2MUON`, `Z2SOURCE` | Zone 2 power, volume, mute, input; `Z3…` likewise for zone 3 |
| `SSFUN ?`      | Input labels configured on the receiver             |
| `SSSOD ?`      | Which inputs are enabled                            |
| `SSQSNZMA ?`   | Quick Select names (and how many slots)             |

Receivers that do not answer `SSFUN ?` are asked over HTTP on port 80 instead:
`/goform/formMainZone_MainZoneXml.xml?ZoneName=ZONE1` first, then
`/goform/formMainZone_MainZoneXmlStatus.xml`. Both carry three parallel lists,
`InputFuncList`, `RenameSource` and `SourceDelete`. An AVR-X1400H, for one,
refuses both pages with 403.

Queries append `?`; parameters under `PS` need a space first, as in `PSBAS ?`.
The receiver drops commands sent back to back, so they are paced roughly 80 ms
apart.

A command the receiver won't accept gets no reply at all. Bass and treble are
ignored while tone control is off, subwoofer trim while `PSSWL` reports `OFF`,
so those controls stay disabled until then. When adding a control, check on the
hardware that the command is echoed back.

Apart from the state queries after connecting, nothing is sent unasked: a
periodic query would keep the receiver out of auto-standby. A dead link is
detected with TCP keepalive.

The application holds its control connection while it runs, also when closed
to the tray. Quit it if another controller needs the port.

## Layout

| File | Role |
|------|------|
| `src/AvrConnection.*` | Connection, reconnection, command pacing, line framing |
| `src/AvrState.*`      | Parses the event stream into mirrored state |
| `src/Discovery.*`     | SSDP search, confirmed by `Deviceinfo.xml` or the UPnP description |
| `src/WebCatalogue.*`  | Input list from the web page, for receivers without `SSFUN` |
| `src/ZonePanel.*`     | Controls for one zone; used for all three |
| `src/SoundPanel.*`    | Surround modes, tone, standing options |
| `src/ConnectDialog.*` | Receiver picker |
| `src/MainWindow.*`    | Tabs, menus, tray icon, settings |

`tests/` holds the unit tests; the pages they parse are captures from real
receivers (`tests/data/NOTICE`).

`legacy/denon_control.py` is the single-window GTK 3 script this application
replaced. It is kept for reference only and is not built or packaged.

## License

GPL-2.0-or-later; see [LICENSE](LICENSE).
