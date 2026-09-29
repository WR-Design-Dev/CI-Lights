# CI-Lights (ESP32-S3)

**Languages:** English | [Deutsch](README.de.md)

> **Experimental project — not production ready.** WPA2-Enterprise server certificate verification is disabled for testing, the setup Wi-Fi network is open, and the local administration interface uses unencrypted HTTP. Use only in a trusted test environment.

On first boot without saved configuration, the ESP creates the open Wi-Fi network
`ci-lights-xxxx-setup`. `xxxx` is the last two bytes of the Wi-Fi MAC address,
for example `ci-lights-7a3f-setup`. After connecting, the captive portal should
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
jobs with the same name. The job list is fetched from Jenkins only when the
small refresh button beside the dropdown is pressed. Once loaded, it is
stored in the browser and remains available when the page is reopened without
making another request. Switching tabs or logging in does not refresh the
list. After changing the Jenkins credentials, press the button to reload it.

The selected job is stored persistently and queried immediately. Jenkins
status `blue` lights green, `yellow` lights yellow, and `red` lights red.
During a running build (`*_anime`), the corresponding Jenkins status color
pulses on the LEDs and in the web traffic light. For `grey`, `aborted`, and
`notbuilt`, only the middle LED glows dim gray; the top and bottom LEDs stay
off. `disabled` turns the traffic light completely off (black). Until a job
is selected, the traffic light is yellow.

The job status is queried immediately at startup and every five minutes by
default. On the `Configuration` tab, the interval can be set to a whole number
of minutes, with a minimum of one minute. Changes are stored persistently and
take effect no later than the next one-minute polling check.

The administration page has `Job`, `Configuration`, and `Controls` tabs.
Under `Configuration`, up to eight Wi-Fi networks can be saved. A network
scan helps with selection. Saving an existing network name updates its
credentials without adding a duplicate and marks it as the most recently
saved profile. New names are added. If all eight slots are occupied, a new
network replaces the profile that has gone the longest without being saved
again. Passwords are not displayed on the website. After saving, the ESP
restarts and tries the new network first. On later boots, it first tries the
last successfully used network, then the other saved networks. When several
access points share a name, it prefers the strongest signal. After quick
connection failures, it pauses briefly and retries within the time window
before moving to the next saved network or returning to setup. The last
saved Wi-Fi network cannot be removed through the administration page.
Single saved networks from older firmware versions are automatically
imported as the first profile.

The `Microcontroller` section in the footer is collapsed initially. When
expanded, it shows the detected chip model and revision, core count, current
and maximum CPU clock, flash, firmware and ESP-IDF versions, build time,
uptime, Wi-Fi MAC address, IPv4 address, global and link-local IPv6 addresses,
channel, signal strength, and current memory values. This firmware runs the
CPU at 160 MHz; according to the
[Espressif datasheet](https://documentation.espressif.com/esp32-s3-mini-1_mini-1u_datasheet_en.pdf),
the ESP32-S3 supports up to 240 MHz. The values are updated when the section
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
logged. `GET /api/jobs` is triggered only by the job list refresh button.

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

## Dependencies and security

`main/idf_component.yml` adds Espressif's cJSON and mDNS components. On the
first `idf.py build`, the ESP-IDF Component Manager must download them
from the internet.

Wi-Fi and Jenkins credentials are not in the source code, but are stored
unencrypted in the ESP's flash.

There are intentionally no OTA updates. The only firmware partition is
1.75 MiB of the 2 MiB flash and is flashed over USB. The final 128 KiB
is reserved for uploaded branding.

The setup Wi-Fi network is intentionally open. Wi-Fi credentials are
therefore transmitted without encryption during setup and can be read or
changed by anyone within radio range. Jenkins credentials are entered later
on the administration page over the local Wi-Fi network.

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
