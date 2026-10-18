# ESP32 PS5 WebKit Autoloader – Display Edition

Fork of [owendswang/ps5-webkit-autoloader-esp32](https://github.com/owendswang/ps5-webkit-autoloader-esp32),
which is itself based on [itsPLK/ps5-webkit-autoloader](https://github.com/itsPLK/ps5-webkit-autoloader).

## Changes in this fork

- ESP32-C3 SuperMini / 01Space 0.42-inch OLED support (SSD1306, 72×40).
- ESP32-S3 Pocket-Dongle LCD support (ST7735, 80×160).
- Optional AP+STA gateway profiles for ESP32/PICO, S2, S3 and C3, with IPv4 NAT
  and selective DNS forwarding.
- First-boot Wi-Fi setup, persistent settings, configurable hotspot name and
  password, and a five-second BOOT reset for home Wi-Fi credentials.
- PlayStation manual redirection and DNS blocking of PS5 system update hosts.
- Docker and VS Code Dev Container builds for Linux, macOS and Windows.
- Flashing instructions and serial diagnostics in this README.

### Quick start: C3 OLED gateway

```sh
git submodule update --init --recursive
docker compose build
docker compose run --rm firmware make -C mklittlefs clean all
docker compose run --rm firmware make c3-oled-dns
```

Flash `build/selective-dns/c3/esp32-arduino.c3.merged.bin` at **`0x0`** for
first installation. A full merged flash clears saved configuration. Connect
to the board's Wi-Fi (initially **PS5 Master Key**, password **12345678**),
then open **http://192.168.4.1/wifi** to configure the home network.

After setup, connect the PS5 to the board's Wi-Fi with automatic network
settings, or use the board's home-network IP as both DNS servers on the PS5.
Open **`http://<board-IP>/settings`**, log in as **admin** with the board's
current Wi-Fi password, and change its hotspot name or password as needed.
The C3 OLED and S3 LCD display the hotspot credentials, scrolling long values.
Hold **BOOT for five seconds while powered on** to reset only the home Wi-Fi
credentials; the hotspot name and password remain saved.

See [gateway configuration and supported targets](#configurable-wi-fi-selective-dns-and-apsta-gateway)
for the complete DNS policy, partition layout and update instructions. The
standard profiles remain available for offline AP-only use.

-----------------------------------------------------------------------------------------------------------

# ESP32 PS5 WebKit Autoloader

A compact HTTP/HTTPS host for the PS5 WebKit Autoloader, packaged as 4 MB flash images for ESP32-PICO, ESP32-S2, ESP32-S3 and ESP32-C3 boards. All targets use LittleFS and do not require PSRAM.

The web content in `autoloader/` is based on a modified version of the [`ps5-webkit-autoloader`](https://github.com/itsPLK/ps5-webkit-autoloader) frontend. Its bundled [`slopkit`](https://github.com/itsPLK/slopkit), [`umtx2`](https://github.com/idlesauce/umtx2) and [`ps5-unified-autoloader`](https://github.com/owendswang/ps5-unified-autoloader/tree/feat/install-webkit-shortcut) components also contain project-specific modifications and therefore do not exactly match upstream.

## Firmware Version Supports

- ~~umtx2        1.00 -  5.50~~ NOT SUPPORTED (`System out of memory` Error)
- poops        7.00 - 12.00
- relapse        7.00 - 13.60   (except 9.05 and 11.40)

You could choose to use poops or relapse if your FW supports both methods while caching. It would use Poops by default if you don't choose.

## Flashing your ESP32 S3 Dongle

1. HOLD the Reset button, plug the dongle to pc, wait for 1 second then release button. If PC says USB not recognised, then button was not pressed.
2. Open https://adafruit.github.io/Adafruit_WebSerial_ESPTool/
3. Go to top right corner of the site and Set to 115200 baud and Click Connect button
4. Select the Port that came up when you did step 1. (Port usaully has label SUB "JTAG/Serial Debug..."
5. Click ERASE button, confirm it will erase everything and wait for it to finish (about 3-5 seconds)
6. Click the top most 'choose a file' button and select the .bin file from the releases page of this repo. https://github.com/poorDevMan/ps5-webkit-autoloader-esp32-DISPLAY/releases
7. Click Program button and wait for flashing to finish. Around 40-60 seconds.
8. Remove the dongle from the usb port to completely power off
9. Test the Dongle by plugging it back in to any dumb or pc usb port. Ignore the USB not recognised since we are not putting it in flash mode again.
10. Look for the WIfi Hotspot `PS5 MASTER KEY` and check the display if information is being shown.
11. If all goes well proceed to Usage section.

## Usage

1. Plug the ESP32 into the PS5 and turn on the console.
2. Connect the PS5 to the `PS5 Master Key` Wi-Fi network using the password `12345678` (or the name and password shown on its display).
3. Open **Settings → Guide & Tips ... → Guide & Tips → User's Guide**.
4. Wait for the installation and caching process to finish. Do not disconnect the ESP32 while it is still running.
5. On success, Payload Manager opens automatically and a **WebKit Autoloader** shortcut appears in the **Media** section of the home screen.

After a successful installation, the web content is cached on the PS5. On subsequent boots, launch **WebKit Autoloader** directly from the Media section.

A network connection is still **REQUIRED** to complete the exploit process, as the exploit method relies on the PS5 having an active network interface and IP address. Internet access itself is not required.

If an attempt fails, follow the on-screen instruction to reboot and try again. If the console freezes, crashes, or powers off, turn it back on and retry. Use this project at your own risk.

## Requirements

For macOS, Windows, or an isolated Linux build, use the Docker environment
below instead of installing dependencies on the host.

- Arduino CLI
- Arduino ESP32 core 2.0.11
- ~~Arduino ESP8266 core 3.1.2 (required for `make 8266`)~~

Install the Arduino CLI and required core:

```sh
./install-deps.sh
```

## Docker / Dev Container (Linux, macOS, Windows)

Install Docker with Compose v2 (for example, Docker Desktop with Linux
containers on Windows). Run these commands from the repository root.
Initialize the submodules if the repository was not cloned recursively:

```sh
git submodule update --init --recursive
docker compose build
```

The image installs GNU Make, Node.js, the asset minifiers, Arduino CLI 1.2.2,
the pinned ESP32/ESP8266 cores, and the display libraries using
`install-deps.sh`. It also installs the separate NAT-enabled ESP32 core with
`install-gateway-deps.sh`. The initial build needs Internet access and several GB
of disk space. The image supports Linux AMD64 and ARM64 and builds for the
host architecture, including Apple Silicon, without requiring x86 emulation.

Compile `mklittlefs` for Linux once, including after switching from a native
host build, then build the display firmware:

```sh
docker compose run --rm firmware make -C mklittlefs clean all
docker compose run --rm firmware make s3-lcd
```

The repository is mounted at `/workspace`, so the output is available on
the host at `build/lcd/s3/esp32-arduino.s3.merged.bin`. Other commands work
the same way:

```sh
docker compose run --rm firmware make
docker compose run --rm firmware make debug
docker compose run --rm firmware make clean
docker compose run --rm firmware bash
```

In VS Code, install **Dev Containers**, open this repository and choose
**Dev Containers: Reopen in Container**. In its terminal, run
`make -C mklittlefs clean all` once, then use the normal Linux commands
such as `make s3-lcd`. Dependencies are already installed in the image.

Keep shell scripts checked out with LF endings (enforced by `.gitattributes`).
On Linux, the container runs as root and generated files can be owned by root;
use `docker compose run --rm firmware chown -R UID:GID build data autoloader.mini`
with your numeric user and group IDs if needed. USB devices are not mapped;
flash the generated `.bin` from the host with your preferred flashing tool.

## Build

### Configurable Wi-Fi, selective DNS and AP+STA gateway

Gateway profiles keep the board's own Wi-Fi network active while connecting
it to a home router at 2.4 GHz. Clients connected to the board can use its
DNS blocker and reach the Internet through IPv4 NAT once the home connection
and gateway are ready. Devices on the home network can also use the board
as a selective DNS server without routing their traffic through it.

These profiles use **Arduino ESP32 3.3.2**, installed separately by
`install-gateway-deps.sh`; the standard profiles retain core 2.0.11. The
separate installation avoids changing the legacy toolchain. For native Linux:

```sh
./install-gateway-deps.sh
make c3-oled-dns
```

Rebuild the Docker image to include the gateway toolchain, then build:

```sh
docker compose build
docker compose run --rm firmware make c3-oled-dns
```

| Target | Board/profile | Output directory |
| --- | --- | --- |
| `c3-oled-dns` | ESP32-C3 with 0.42-inch OLED | `build/selective-dns/c3` |
| `c3-dns` | ESP32-C3 without OLED | `build/gateway/c3` |
| `pico-dns` | ESP32/PICO, 4 MB flash | `build/gateway/pico` |
| `pico-8m-dns` | ESP32/PICO, 8 MB flash | `build/gateway/pico-8m` |
| `s2-dns` | ESP32-S2 | `build/gateway/s2` |
| `s3-dns` | ESP32-S3 without LCD | `build/gateway/s3` |
| `s3-lcd-dns` | ESP32-S3 Pocket-Dongle LCD | `build/gateway-lcd/s3` |

`make all-dns` builds the standard ESP32 gateway profiles. ESP8266 retains
its existing AP-only firmware. Availability of a build target does not imply
that every physical board and display revision has been hardware-tested.
Gateway profiles keep USB serial diagnostics enabled on chips with native USB.

Read diagnostics from a serial terminal at **115200 baud**. For example,
install pyserial on the host and use the bundled timestamped monitor:

```sh
# macOS / Linux
python3 -m venv .venv
.venv/bin/python -m pip install pyserial
.venv/bin/python serial_timestamp.py /dev/cu.usbmodem1101 115200
```

```powershell
# Windows
py -m venv .venv
.venv\Scripts\python -m pip install pyserial
.venv\Scripts\python serial_timestamp.py COM4 115200
```

Replace the port with the one assigned to your board (`/dev/ttyACM0` or
`/dev/ttyUSB0` are common on Linux). Close the monitor before flashing.
`[WIFI] Connected` reports the home-network IP; `[ROUTER] NAPT READY` confirms
that IPv4 forwarding is enabled. To test Internet access through the board,
connect a phone or computer to its hotspot and open a website; disable mobile
data on phones during this check. For a blank C3 OLED, verify the OLED target
and the wiring described below; the standard `c3` target does not drive it.

For the C3 OLED, first installation uses
`build/selective-dns/c3/esp32-arduino.c3.merged.bin` at offset `0x0`.
On first boot, with no saved home Wi-Fi configuration:

1. Connect a phone or computer to **PS5 Master Key**, password **12345678**.
2. Open **http://192.168.4.1/wifi**; the captive portal may open automatically.
3. Enter the home Wi-Fi SSID and password. The network must support 2.4 GHz.
4. The board saves the credentials in NVS and restarts. Its own AP remains
   available while it connects to the router.
5. Connect the PS5 using either of the following modes.

| PS5 connection | Network settings |
| --- | --- |
| Board's Wi-Fi network | Keep IP, gateway and DNS automatic. DHCP supplies the board's AP address as DNS; IPv4 Internet traffic uses its NAT gateway when ready. |
| Home Wi-Fi network | Keep IP and gateway automatic. Set **both DNS fields** to the board's home-network IP shown as L: on the OLED, in `/settings`, or in the serial log. A router DHCP reservation keeps this IP stable. |

The board reuses saved credentials at every boot without reopening the Wi-Fi
form. If the router is absent or the password is wrong, it keeps retrying,
preserving both the credentials and its own AP. Internet access through the
board resumes once the upstream connection and NAT are ready. If the home
subnet overlaps the board's default `192.168.4.0/24` network, the board selects
a different private AP subnet. Reconnect clients to the board so they receive
the new DHCP settings; use its current AP IP to open the configuration pages.

Open **`http://<board-IP>/settings`** to change the board's own Wi-Fi name and
password without recompiling. This page works through either network address.
Authenticate as **admin**, using the **current password of the board's AP**.
An empty password field preserves the existing password; a new password must
contain 8–63 printable ASCII characters, and the name 1–32. Saving restarts
the board. The C3 OLED and S3 LCD keep the AP name and password visible, scrolling long
values, including while connected to the home router. The IP row alternates
between AP and home-network addresses (L: on OLED, LAN: on LCD) when online; `/settings` also lists
both addresses for boards without a display. The home router's Wi-Fi password
is never displayed or printed to serial.

To erase **only the home Wi-Fi credentials**, hold **BOOT for 5 seconds while
the board is already running**. Release BOOT when it restarts and reopen
`/wifi` through the AP. Its custom AP name and password are preserved, as are
LittleFS and unrelated NVS namespaces. BOOT+RESET enters the ROM flasher and
does not perform this configuration reset. BOOT is GPIO9 on C3 and GPIO0 on
the other supported ESP32 profiles.

| DNS names | Action |
| --- | --- |
| `manuals.playstation.net`, `ena.net.playstation.net` | Resolve to the board's AP IP for AP clients, or its home-network IP for home-network clients; AAAA returns no data |
| `ps5.update.playstation.net` and **all its subdomains** | Block with NXDOMAIN for all query types |
| `update.playstation.net`, `update.net.playstation.net` | Block with NXDOMAIN |
| All other names, including `www.msftconnecttest.com` | Forward unchanged to the DNS server supplied by the router |

The update zone includes regional metadata hosts such as
`fus01.ps5.update.playstation.net` / `fuk01.ps5.update.playstation.net`, and
download hosts such as `dus01.ps5.update.playstation.net` and
[`pc.ps5.update.playstation.net`, linked by Sony's update page](https://www.playstation.com/it-it/support/hardware/ps5/system-software/).
The regional host pattern is also recorded in the researcher's
[PS5 network analysis](https://archive.codeblue.jp/2024/files/cb24_Defeating_PlayStation_5_Network_Encryption_by_Aapo_Oksman.pdf).
Shared PlayStation game CDN domains are not blanket-blocked.

The policy blocks DNS names; it is not a general-purpose firewall. It does
not filter arbitrary connections, hardcoded destination IPs or encrypted DNS.
Blocking applies while the console uses this DNS and cannot prevent an
update already downloaded, cached elsewhere or supplied on USB/disc. Disable
automatic system update downloads and installation on the PS5 as well.
NAT forwards IPv4 traffic; these profiles do not provide an IPv6 gateway.
DNS forwarding supports UDP and TCP, up to eight pending UDP queries, one
TCP transaction and 4 KiB messages. Slow upstream queries expire after four
seconds without blocking local DNS responses or BOOT handling.

The 4 MB gateway layout reserves **1.5 MB** for the application and places
LittleFS at **`0x190000`**, with size **`0x270000`**. Legacy 4 MB profiles
have a 1 MB application partition and a different filesystem offset. When
moving from a legacy profile to a gateway profile, flash the gateway's
bootloader, partition table, application **and filesystem** together; use
the target's full merged image. An application-only flash across these layouts
can overwrite the old filesystem and will not install the new partition table.

For later updates with the **same partition layout**, flashing only the
application at `0x10000` retains saved NVS settings. For example, use
`build/selective-dns/c3/esp32-arduino.ino.bin` for C3 OLED. Flashing a full
merged image at `0x0` replaces the NVS region and removes saved configuration.
The 8 MB PICO profile uses its own `partitions-8m.csv` layout; always match
firmware and filesystem images to the selected target.

Gateway profiles use the nonblocking proxy in `selective-dns.h`; standard
AP-only profiles retain the patched Arduino DNSServer below. Run DNS protocol,
Wi-Fi persistence, settings validation and BOOT reset tests with:

```sh
python3 -m unittest discover -s tests -v
```

### ESP32-C3 SuperMini with 0.42-inch OLED

The standard `make c3` firmware does not drive a display. For the common
[01Space-compatible 0.42-inch OLED board](https://github.com/01Space/ESP32-C3-0.42LCD),
use `make c3-oled`. This profile assumes an SSD1306-compatible 72x40 display
at I2C address `0x3C`, with SDA on GPIO5 and SCL on GPIO6; check the board
revision if its display stays blank. It shows Wi-Fi name, IP, password and
startup status. The S3 LCD firmware uses a different chip and display.

```sh
docker compose build
docker compose run --rm firmware make c3-oled
```

For native Linux, rerun `./install-deps.sh` first to install U8g2.
The merged image is `build/oled/c3/esp32-arduino.c3.merged.bin` and must be
flashed at offset `0x0`. To keep USB serial logging enabled, build a separate
diagnostic image with:

```sh
docker compose run --rm firmware make USB_DEBUG=1 BUILD_DIR=/workspace/build/oled-debug C3_OLED=1 c3
```

That image is `build/oled-debug/c3/esp32-arduino.c3.merged.bin`.
The normal OLED image disconnects native USB after boot, like the other
release targets; a disappearing serial port alone does not indicate a crash.

### Standard targets

```sh
make
```

The default ESP32-S2/S3/C3/PICO firmware leaves native USB disconnected, so the board only draws power from its USB connection. Build the separate debug variant to enable USB CDC/Serial-JTAG output:

```sh
make debug
```

Debug firmware is written to `build/debug/`. For the ESP32-S2 alone, use `make debug-s2`. To timestamp every serial line from 00:00:00, run `python serial_timestamp.py COM4 115200` (requires pyserial).

DNS resolves `manuals.playstation.net`, `ena.net.playstation.net` and `www.msftconnecttest.com` to the ESP32; other names receive NXDOMAIN. The build creates a modified copy of Arduino ESP32 2.0.11's DNSServer under ignored `build/` using `prepare-dns-library.py`. That library retains its [upstream LGPL-2.1 license](https://github.com/espressif/arduino-esp32/blob/2.0.11/LICENSE.md).

The build copies `autoloader/` to a temporary `data/` directory, compresses the web assets, and creates:

- `build/pico/esp32-arduino.pico.merged.bin`: Intended to support generic ESP32-PICO series boards.
- `build/s2/esp32-arduino.s2.merged.bin`: Intended to support ESP32-S2 series boards.
- `build/s3/esp32-arduino.s3.merged.bin`: Intended to support ESP32-S3 series boards.
- `build/c3/esp32-arduino.c3.merged.bin`: Intended to support ESP32-C3 series boards.

Use `make pico`, `make s2`, `make s3`, `make c3`~~, `make 8266`~~, or `make clean` to build an individual target or clean generated files. ~~The ESP-12F target uses the Generic ESP8266 4 MB / 3 MB LittleFS layout and produces `build/8266/esp8266-arduino.esp12f.merged.bin`.~~

## Extract embedded autoloader binaries

After adapting upstream HTML for the ESP32 split caches, run:

```sh
python3 externalize_autoloader.py
```

The script detects the version under `autoloader/app/`, extracts the shared
`shared/elfldr-ps5.elf`, `shared/kexp-ps5.bin`, and `payloads/autoloader.elf`,
switches both pages to ordinary same-origin fetches, and adds the resources to
both manifests before `__complete__`. It verifies sizes, SHA-256 hashes, and
agreement between the pages before writing. Repeated runs verify the files
without changing them. An alternative web directory can be passed as an argument.

Original files are backed up outside the web directory, under
`autoloader-externalize-backup/<version>/`, so backups are not included in LittleFS.

## Manifest updates

ESP32 serves `.appcache`, `.manifest`, and `.cache` files over HTTP and HTTPS
with a random 16-digit hexadecimal `# VERSION` comment generated once per boot
and HTTP no-cache headers. Resource lists are preserved; the generated version
is not written to flash. Manifests remain uncompressed in LittleFS.

Rebuild and flash the merged image so both firmware and LittleFS are updated.
The version stays the same across HTTP and HTTPS requests until ESP32 restarts,
so a browser's final manifest consistency check receives identical content.
After a restart, the new version triggers an update on the next online cache
check. Repeated visits during the same boot do not force another update;
offline visits keep the existing cache.

## Credits

- [itsPLK/ps5-webkit-autoloader](https://github.com/itsPLK/ps5-webkit-autoloader)
- [itsPLK/ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader)
- [jordyidk/slopkit](https://github.com/jordyidk/slopkit)

Credit also belongs to all projects, researchers, and developers referenced by these upstream projects.

## License

Original ESP32 firmware and build code in this repository is MIT licensed. Modified upstream code, bundled payloads, and other third-party material retain their respective upstream terms; see [LICENSE](LICENSE).
