# CI-Lights (ESP32-S3)

**Languages:** English | [Deutsch](README.de.md)

**Project information:** The code for this project was created by AI.

> Use only in a separate IoT network.

## 3D print model

Find the [CI-Lights 3D print model on MakerWorld](https://makerworld.com/de/models/3352315-ci-lights).

## Browser installation and firmware updates

After the first successful GitHub Pages deployment, the installer is available
at [wr-design-dev.github.io/CI-Lights](https://wr-design-dev.github.io/CI-Lights/).
Use Chrome or Edge on a computer and a USB data cable to install the complete
firmware without installing ESP-IDF. Leave the erase option unchecked when
reinstalling to preserve settings and branding. Migrating an older device to
the OTA partition layout requires one complete USB installation.
The custom installer loads esptool-js 0.6.1 from UNPKG after connecting.
English is the default language. The **EN** and **DE** flag buttons in the header
switch the entire installer, including status messages and legal pages.
Its separate **Geraet loeschen** button erases the whole flash after confirmation,
without installing firmware. Legal notice and privacy links are in the footer.

For subsequent updates, open device administration, choose **Check for updates**,
then **Install update**. The device downloads the public GitHub Release over HTTPS,
checks its size, SHA-256 and RSA signature, and restarts. An incomplete download
leaves the previous firmware active. The new firmware confirms successful local
startup after ten seconds; a reset before confirmation rolls back to the previous
firmware. Settings and uploaded branding are preserved during OTA updates.

See [firmware hosting and signing setup](docs/firmware-updates.md) for GitHub
configuration and signing key backup instructions.

## Set up Visual Studio Code and the ESP-IDF extension

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the official
   [Espressif ESP-IDF extension](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension).
   In VS Code, you can also open Extensions with `Ctrl+Shift+X` and search for
   `ESP-IDF`.
2. Open the Command Palette with `F1` and run
   `ESP-IDF: Open ESP-IDF Installation Manager`. Install ESP-IDF **6.1.0** and
   its tools. Then run `ESP-IDF: Select Current ESP-IDF Version` and select
   that installation. If the extension detects an existing installation, you
   can select it directly; otherwise, use the linked manual configuration
   instructions. `ESP-IDF: Doctor Command` can check the configuration.
   [Espressif installation guide](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html)
3. Clone this repository with Git, or use an existing checkout. In VS Code,
   use `File > Open Folder` to open the **CI-Lights** directory containing
   the top-level `CMakeLists.txt`:

   ```sh
   git clone https://github.com/WR-Design-Dev/CI-Lights.git
   ```

   This is an existing project, so `ESP-IDF: New Project` is unnecessary. For
   C/C++ IntelliSense, `ESP-IDF: Add VS Code Configuration Folder` can create
   a local `.vscode` configuration.
4. The device target must be `esp32s3`. It is already set in `sdkconfig`; if
   VS Code shows another target, run `ESP-IDF: Set Espressif Device Target`
   and select `esp32s3`. Keep the existing configuration: **4 MB flash**, the
   custom partition table (`partitions.csv`), and **2 MB Quad PSRAM**.
5. The first build requires `secrets/ota_signing_key.pem`. The project owner
   restores the original signing key. For your own development build, run
   `python tools/prepare_ota_key.py --development` once in the ESP-IDF terminal.
   Devices installed with this development key via USB will not accept official
   OTA releases. Then run `ESP-IDF: Build your Project`. On the first build, the ESP-IDF Component
   Manager needs internet access to download the cJSON and mDNS dependencies.
   A successful build creates `build/CI-Lights.bin`.
6. Connect the ESP32-S3-Zero with a data-capable USB cable in download mode.
   The board has no USB-to-UART chip: to enter download mode, the
   [Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Zero) says to
   hold **BOOT** while connecting USB, or hold **BOOT** and press **RESET**.
   Then run `ESP-IDF: Select Port to Use` and choose its serial port (for
   example, `COMx` on Windows). Run `ESP-IDF: Flash your Project` and choose
   the **UART** flash method. Press **RESET** after flashing to start the firmware.
   [Espressif flashing guide](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/flashdevice.html)
7. If the port changes after reset, select it again with
   `ESP-IDF: Select Port to Use`. Run `ESP-IDF: Monitor Device` to view warnings,
   errors, the device web address, and Wi-Fi setup instructions.
   Then follow the Wi-Fi first-time setup described below.
   [Espressif monitor guide](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/monitoroutput.html)

On first boot without saved configuration, the ESP creates the WPA3-protected
Wi-Fi network `ci-lights-xxxx-setup`. `xxxx` is the last two bytes of the
Wi-Fi MAC address, for example `ci-lights-7a3f-setup`. The traffic light
shows its Wi-Fi password as eight LED signals. Each signal lights one
position (top `T`, middle `M`, bottom `B`) in one color (Red `R`, Orange
`O`, Yellow `Y`, Green `G`, Blue `B`, Violet `V`). Position and color
produce a two-letter pair, such as `TR` for top red or `BV` for bottom
violet. Enter all eight pairs without spaces as the 16-character Wi-Fi
password. Each signal lights for 0.9 seconds. The first seven are separated by
1.5 seconds of darkness; after the eighth, a 3.5-second dark pause marks
the repeat. A new code is generated
after every restart. Setup requires WPA3; WPA2 is not offered.
After connecting, the captive portal should
open the setup page automatically. If it does not open on iOS or Android, visit
`http://192.168.4.1` manually. Enter the Wi-Fi name, optional Wi-Fi username,
and Wi-Fi password there; leave the password blank for an open network. If a
username is provided, the ESP attempts WPA2-Enterprise authentication using
PEAP or TTLS with MSCHAPv2. Leave the username blank for open and PSK networks.
Server certificate verification is disabled for this Enterprise test. An
attacker could impersonate the company Wi-Fi network; before production use,
the company CA must be installed and verification must be enabled. The page
also displays the future local URL and the Wi-Fi MAC address. Write both down.
After restarting, the ESP connects to a saved Wi-Fi network.

During setup, create an administrator username (3 to 32 characters) and
password (8 to 64 characters). The password must be repeated for confirmation.
If no saved Wi-Fi network can be reached, the setup network and captive portal
start again; the existing administrator credentials remain valid. After
upgrading from a firmware version without an administrator password, setup
opens again. Devices with the former fixed username `admin` retain it until
their settings are reset.

## Select a job in the browser

After connecting to Wi-Fi, the traffic light is available at a local mDNS
address:

`http://ci-lights-ID.local`

`ID` is the last two bytes of the Wi-Fi MAC address as four hexadecimal
characters. For example, if the MAC address ends in `7a:3f`, the address is
`http://ci-lights-7a3f.local`.

The exact address is also printed to the serial console at startup. Opening
the administration page prompts for the username and password set during
setup. The administration API requires the same login. The password is stored
as a salted hash. The local website uses HTTP, so login details and other
credentials are transmitted across the network without encryption.

First enter the Jenkins URL, Jenkins username, and Jenkins API token on the
administration page. A dropdown then shows Jenkins jobs from all folders and
subfolders. It displays the folder path before each job name to distinguish
jobs with the same name. The job list is fetched as soon as the Jenkins
credentials are saved. The small refresh button beside the dropdown reloads
it later. Once loaded, it is stored in the browser and remains available when
the page is reopened without
making another request. Switching tabs or logging in does not refresh the
list. Changing the Jenkins credentials reloads the list automatically.

The selected job is stored persistently and queried immediately. Jenkins
status `blue` lights green, `yellow` lights yellow, and `red` lights red.
During a running build (`*_anime`), the corresponding Jenkins status color
pulses or blinks on the LEDs and in the web traffic light. For `grey`, `aborted`, and
`notbuilt`, only the middle LED glows dim gray; the top and bottom LEDs stay
off. `disabled` turns the traffic light completely off (black). Until a job
is selected, the traffic light is yellow.

The job status is queried immediately at startup. If the first query fails
transiently, two more attempts follow five seconds apart. It is then queried
every five minutes by default. On the `Configuration` tab, the interval can be
set to a whole number
of minutes, with a minimum of one minute. Changes are stored persistently and
take effect no later than the next one-minute polling check.

The administration page has `Job`, `Configuration`, and `Controls` tabs.
Under `Configuration`, up to eight Wi-Fi networks can be saved. A network
scan helps with selection. Saving an existing network name updates its
credentials without adding a duplicate and keeps its position. New names are
added at the end. If all eight slots are occupied, a new network replaces the
last profile. The arrows in the management page persistently change the order.
Passwords are not displayed on the website. After saving, the ESP restarts
and tries the networks from top to bottom. A changed order takes effect after
the next restart. When several
access points share a name, it prefers the strongest signal. After quick
connection failures, it pauses briefly and retries within the time window
before moving to the next saved network or returning to setup. The last
remaining Wi-Fi network cannot be removed through the administration page.
Single saved networks from older firmware versions are automatically
imported as the first profile.

The `Microcontroller` section in the footer is collapsed initially. When
expanded, it shows the detected chip model and revision, core count, current
and maximum CPU clock, flash, firmware and ESP-IDF versions, build time,
uptime, Wi-Fi MAC address, IPv4 address, global and link-local IPv6 addresses,
channel, signal strength, and current memory values. Under `Configuration`,
the CPU mode can be set to fixed 160 MHz, fixed 240 MHz, automatic 40–160 MHz,
or automatic 40–240 MHz. The change takes effect without a restart and is
stored in NVS. In automatic mode, ESP-IDF reduces the clock when idle and
raises it up to the selected limit for Wi-Fi work, Jenkins polling, and
management requests. 40 MHz is a lower bound, not a guaranteed continuous idle
clock. The displayed clock is sampled during the device-information request
and may therefore show the upper limit. According to the
[Espressif datasheet](https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.pdf),
the ESP32-S3 supports up to 240 MHz. The mode is also available through
`GET /api/cpu-mode` and `POST /api/cpu-mode` with `{"mode":"auto240"}`; the other
values are `fixed160`, `auto160`, and `fixed240`. The values are updated when the section
is expanded and every minute afterward through `GET /api/device-info`. A
global IPv6 address appears only if the Wi-Fi network provides one. Free and
total internal heap, its historical minimum, the largest free block, and
free and total PSRAM are shown in bytes.

The flags at the top right switch between German and English. German is the
default language. The selection is stored in NVS and restored on the setup
and administration pages after a restart.

The traffic light graphic is embedded in the firmware. The banner and favicon
are not included: upload the banner by clicking the placeholder at the top
of the page and the favicon under `Configuration`. The visible title
`CI-Lights` can be edited directly. Leaving the text field saves it in NVS;
Escape discards the change. SVG or PNG is allowed for the banner, and ICO or
PNG for the favicon, each up to just under 32 KiB. Both are stored in a
dedicated 128 KiB flash partition and survive restarts and normal firmware
flashing. After the first flash, the banner and favicon appear only after
they are uploaded.

## Control modes and REST API

The control mode is not stored persistently. It starts in
`Auto (Jenkins)` after every restart.

- `Auto (Jenkins)`: Jenkins controls the traffic light and is queried immediately when this mode is selected.
- `Manual`: Red, yellow, and green can be switched individually on the administration page.
- `Disco (WS2812)`: The WS2812 traffic light plays the selected animation; Jenkins is ignored.
- `REST API`: Jenkins is ignored, and an API client sets exactly one traffic light status.

The REST API is available at the same local address as the administration
page. Every request requires HTTP Basic Auth with the username and password
created during setup. With `curl -u USERNAME`, curl prompts for the password
interactively, so it does not need to appear in the command. Replace `ID`
in the hostname with your device's four characters. Without authentication,
the device responds with HTTP `401 Unauthorized`.

First enable API mode:

```sh
curl -u USERNAME -X POST http://ci-lights-ID.local/api/mode \
  -H "Content-Type: application/json" \
  -d '{"mode":"api"}'
```

This request then sets the traffic light to green. Valid `status` values are
`off`, `red`, `yellow`, and `green`.

```sh
curl -u USERNAME -X POST http://ci-lights-ID.local/api/status \
  -H "Content-Type: application/json" \
  -d '{"status":"green"}'
```

`POST /api/mode` accepts `auto`, `manual`, `disco`, and `api`.
`GET /api/lights` returns the current LED state and active control mode.
This read request also requires a password:

```sh
curl -u USERNAME http://ci-lights-ID.local/api/lights
```

A status request outside API mode is rejected with HTTP `409 Conflict`. The
mode endpoint remains available in every control mode, so an API client can
reactivate API mode after a switch to `auto` or `manual`.

## Manual traffic light control

On the administration page, red, yellow, and green can each be switched on
or off when `Manual` mode is active. Each LED has a toggle button that shows
its current state as `On` or `Off`. Manual control changes only the selected
LED; the other two keep their states. Switching back to `Auto (Jenkins)`
lets Jenkins update the traffic light again.

The traffic light graphic beside the switches shows the current state with
a visible glow. It updates immediately after a job is selected and otherwise
once per second.

## Disco mode and LED brightness

On the `Controls` tab, choose `Pulse` or `Blink (on/off)` for running Jenkins
builds. This choice is stored persistently and survives restarts. `Pulse` is
the default. The effect can also be set through the REST API:

```sh
curl -u USERNAME -X POST http://ci-lights-ID.local/api/build-effect \
  -H "Content-Type: application/json" \
  -d '{"effect":"blink"}'
```

The brightness control is on the `Controls` tab and applies to the external
WS2812 traffic light, the built-in RGB LED, and all control modes. It can be
set from 1 to 100 percent and is stored persistently. Without a saved value,
it starts at 50 percent.

Disco mode offers ten WS2812 animations inspired by WLED:
`Rainbow`, `Colorloop`, `Chase`, `Rainbow Chase`, `Blink`, `Breathe`,
`Twinkle`, `Scan`, `Theater Chase`, and `Fireworks`.

The effect can also be selected through the REST API after Disco mode has
been enabled:

```sh
curl -u USERNAME -X POST http://ci-lights-ID.local/api/disco-effect \
  -H "Content-Type: application/json" \
  -d '{"effect":"rainbow-chase"}'
```

Brightness can be set regardless of the control mode:

```sh
curl -u USERNAME -X POST http://ci-lights-ID.local/api/brightness \
  -H "Content-Type: application/json" \
  -d '{"brightness":35}'
```

The ESP requests the job list and every status update directly from Jenkins.
The website does not display stored credentials, but it sends the Jenkins
token to the ESP when settings are saved.

The local website and REST API require HTTP Basic Auth. Because the connection
uses unencrypted HTTP, login details and the Jenkins token can be read in
transit on the Wi-Fi network. Anyone with valid administrator credentials can
change the Jenkins connection, selected job, and traffic light status. On
some company Wi-Fi networks, client isolation or multicast filtering may
block mDNS; in that case, the page is reachable only through the IP address
assigned by the router.

## Diagnostics through the USB monitor

After flashing, `idf.py -p COMx monitor` shows the serial messages (replace
`COMx` with the ESP's port). At startup, it prints the reset reason and
web server activation. About once per minute, a `jenkins: Diagnose` line
reports Wi-Fi signal strength (RSSI), free internal heap, its historical
minimum, the largest free block, and free PSRAM.

The `network` tag reports Wi-Fi disconnections with reason and RSSI as well
as reconnection. `lights_http` reports accepted and closed connections,
HTTP parser errors, and the durations of `GET /`, `GET /api/jobs`, and
`POST /api/mode`. The three POST requests for manual LED control are also
logged. `GET /api/jobs` is triggered when Jenkins credentials are saved and
by the job list refresh button.

If `lights_http` reports an accepted connection but no following `GET /`
or API request, the HTTP error code helps identify the problem. Long
`GET /api/jobs` times point to the Jenkins connection. Frequent
`network: WLAN getrennt` lines or fluctuating RSSI point to the Wi-Fi
connection. The ESP32-S3-Zero has 2 MB of PSRAM enabled. Larger `malloc`
allocations, Jenkins response buffers, and JSON data can use it; some
internal memory remains reserved for tasks and hardware. `PSRAM frei 0 B`
after flashing this firmware indicates a PSRAM initialization problem.
The heap values indicate whether memory is under pressure.

## Reset settings

If you forgot the local URL or need new credentials, hold the **BOOT**
button for five seconds while the ESP is **running**. It deletes all saved
settings, including Wi-Fi, Jenkins, and administrator credentials, then
restarts with `ci-lights-xxxx-setup`. The firmware and uploaded branding
remain. Erasing the entire flash also removes the banner and favicon;
they must then be uploaded again.

BOOT is GPIO 0 on the ESP32-S3-Zero. Do not hold it while powering on or
together with RESET: that starts the flash download mode instead of the
firmware.

## Wiring

### ESP32-S3-Zero

The microcontroller information panel detects the chip model at runtime.
The board manufacturer's name is not stored in the chip. The connections
below document the ESP32-S3-Zero GPIO assignments used by the firmware.

The firmware is designed for the ESP32-S3-Zero. The traffic light consists
of a chain of three WS2812-compatible LEDs. The data input of the first
LED is connected to GPIO 1. The LEDs use a 5 V supply and share ground with
the board. In data-flow order, the first LED is green (top), the second is
yellow, and the third is red (bottom). At power-on, all three LEDs pulse
white until the firmware outputs its first traffic light state or blink
pattern. The board's built-in WS2812B on GPIO 21 is driven in parallel as a
status indicator and shows the same states and blink patterns.

See the [Waveshare ESP32-S3-Zero documentation](https://docs.waveshare.com/ESP32-S3-Zero)
for board hardware details.

### WS2812 traffic light

Hold the board with the USB connector at the top and the pins facing you.
Count the pins on the right side from the top:

| WS2812 chain | ESP32-S3-Zero | Position on upper right |
| --- | --- | --- |
| Positive (5 V) | 5 V | 1st pin |
| Negative (GND) | GND | 2nd pin |
| Data (DIN of first LED) | GPIO 1 | 4th pin |

Cut the LED strips so that each strip segment contains exactly three LEDs.
Cut only at the marked points so the segments fit in the traffic light.

Each jumper wire soldered to an LED strip must be 12 cm long, including
its Dupont connector. Strip 2 mm of insulation from each wire end and tin
the exposed wire. Tin the LED strip contacts before soldering them together.

```text
     WS2812 chain                        ESP32-S3-Zero
  +------------------+                 +-----------------+
  | 5 V              |-----------------| 5 V             |
  | GND              |-----------------| GND             |
  | DIN (LED 1: green) |---------------| GPIO 1          |
  | DOUT -> LED 2: yellow -> LED 3: red                  |
  +------------------+                 +-----------------+
```

GPIO 1 outputs 3.3 V data signals, while the WS2812 chain uses a 5 V
supply. For reliable operation, a 3.3 V-to-5 V level shifter (such as a
74AHCT125) between GPIO 1 and DIN is recommended; a shared GND is
essential. A resistor of about 330 ohms in the data line and an
electrolytic capacitor (about 1000 µF) between 5 V and GND at the start
of the chain are also recommended.

## 3D-printed enclosure

The matching traffic light enclosure is available here:
[CI-Lights on MakerWorld](https://makerworld.com/de/models/3352315).

## Firmware version

Every successful `idf.py build` increments the firmware version in the
`1.0.N` format, even if no source files changed. `version.txt` stores the
last built number and survives `idf.py fullclean`. The version appears in
the firmware metadata and in the administration page footer.

GitHub Actions builds every push and pull request. Pushes to `main` publish
signed releases and update the installation website. Other branches and pull
requests use disposable signing keys and do not publish updates. CI versions
are `1.0.(1000 + workflow run number)`, selected through
`CI_LIGHTS_RELEASE_VERSION` without changing `version.txt`.

### Build and publish on Windows

Run `publish.bat` from the project folder or double-click it. Its opening
comments list the prerequisites and setup commands for Git, Node.js,
GitHub CLI and ESP-IDF 6.1.

The script tests the browser interfaces, builds and signs the firmware locally
with the existing original key, and checks the signature and installation
packages. It then shows the project files before committing; enter `JA` to
confirm the commit and push to `main`. GitHub rebuilds, signs with its environment
secret, and publishes the release and flash website. The script waits for
completion and displays the links.

```bat
publish.bat -Message "My change"
publish.bat -CheckOnly
publish.bat -SkipLocalBuild
```

`-CheckOnly` checks prerequisites without building or publishing.
`-SkipLocalBuild` skips the local SDK build; GitHub still builds and signs.
This mode needs neither ESP-IDF nor the private OTA key on your computer.
With no new commits, the script manually starts a new GitHub build.
`-Yes` skips commit confirmation for deliberate automation.

Start with an empty Git staging area, on `main`, with the current `origin/main`
included in your branch. The script stages all changed project files;
`.vscode/settings.json`, Git-ignored keys, backups and build files stay local.
Never put passwords or tokens in the script. The existing OTA key is reused.

The firmware uses `-Os` to optimize flash size. SDK info logs are removed at
compile time; essential Wi-Fi and setup messages remain. Flash usage is measured
by `build/CI-Lights.bin`; the much larger ELF and map files also contain debugging
information.

## Dependencies and security

`main/idf_component.yml` adds Espressif's cJSON and mDNS components. On the
first `idf.py build`, the ESP-IDF Component Manager must download them
from the internet.

Wi-Fi and Jenkins credentials are not in the source code, but are stored
unencrypted in the ESP's flash.

The 4 MiB partition layout has two 1.75 MiB application slots and a 128 KiB
branding partition. Signed OTA updates alternate between the slots. Signatures
are verified in software against the public key of the running firmware.
Hardware Secure Boot and Flash Encryption remain disabled; this setup does not
change eFuses. The private RSA key is absent from firmware and source control.
Its local PEM file is unencrypted and must be protected and backed up separately
with encryption. GitHub uses an environment secret restricted to `main`. Anyone
who can change trusted build code on `main` can potentially extract this key.

The setup Wi-Fi network uses WPA3-SAE and a new LED-displayed code after every
restart. The captive portal still uses HTTP; someone with access to the setup
network can intercept or change the entered Wi-Fi credentials through an
active attack. Jenkins credentials are entered later on the administration
page over the local Wi-Fi network.

If Jenkins uses an internal CA, its root certificate must also be added
to the ESP-IDF certificate bundle. TLS verification must not be disabled.

## License

This project's own source code, documentation, and traffic light graphic
are released under the [Unlicense](LICENSE). The banner and favicon are
uploaded by the user and are not part of the project; their usage rights
belong to their respective owners. Included components such as ESP-IDF,
cJSON, and mDNS retain their own licenses. When distributing compiled
firmware, include the applicable license texts and copyright notices
for those components.
