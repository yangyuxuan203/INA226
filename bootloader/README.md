# STM32F407 OTA Bootloader

This is a single-slot OTA design for the STM32F407ZET6 (512 KiB internal
Flash). It does not require external Flash, but it cannot restore the previous
application after sector 5-7 erasure has started.

## Flash layout

| Region | Address | Size | STM32F4 sectors |
| --- | --- | ---: | --- |
| Bootloader | `0x08000000` | 48 KiB | 0-2 |
| Metadata power-loss backup | `0x0800C000` | 16 KiB | 3 |
| OTA metadata log | `0x08010000` | 64 KiB | 4 |
| Application | `0x08020000` | 384 KiB | 5-7 |

The application vector table is linked at `0x08020000`. A fixed 32-byte
firmware information block is linked at `0x08020200`. Sector 3 is not part of
the Bootloader executable: it temporarily preserves both the current state and
the previous signed `CONFIRMED` record while Sector 4 is compacted, so a reset
during compaction still leaves a trusted recovery path.

## Local configuration

Create the ignored credential headers from their tracked templates:

```powershell
Copy-Item Core/Inc/wifi_config.h.example Core/Inc/wifi_config.h
Copy-Item Core/Inc/onenet_config.h.example Core/Inc/onenet_config.h
```

Generate the development signing key once, back up its private PEM securely,
and rebuild the Bootloader with the generated public header:

```powershell
python tools/ota_keygen.py
```

Never commit `tools/config/ota_ed25519_private.pem`. Key rotation requires a
physical Bootloader update before devices can accept images signed by the new
key.

## Build and first programming

Build the application and Bootloader in Release mode. Run these commands from
an STM32Cube toolchain terminal, or otherwise ensure that
`arm-none-eabi-gcc --version` succeeds first.

```powershell
cmake --preset Release
cmake --build --preset Release
cmake -S bootloader -B build/bootloader/Release -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/bootloader/Release --parallel
```

For a signed initial SWD installation, generate a factory metadata record from
the final application binary and its stable recovery URL:

```powershell
python tools/ota_manifest.py build/Release/INA226.bin `
  https://firmware.example.com/INA226-1.0.0.bin 0x00010000 `
  --factory-metadata-output build/Release/INA226_factory_metadata.bin --pretty
```

Then erase the chip and program all three images:

```text
build/bootloader/Release/STM32F407_Bootloader.bin -> 0x08000000
build/Release/INA226_factory_metadata.bin          -> 0x08010000
build/Release/INA226.bin                          -> 0x08020000
```

The Bootloader then authenticates the factory application with the same
Ed25519 signature, CRC32, BLAKE2b-256 hash, vector, board ID, and version checks
used after OTA. The default Bootloader is strict: an empty or invalid metadata
sector enters `RECOVERY` and does not start the application. Program all three
images together after a full-chip erase.

## Version and signed manifest

Version encoding is `0x00MMmmpp`. Update `FW_VERSION_MAJOR`,
`FW_VERSION_MINOR`, and `FW_VERSION_PATCH` in
`Core/Inc/firmware_version.h`, then rebuild the application. The example below
assumes the source version has been changed from 1.0.0 to 1.0.1. Generate the
signed OneNET command only after the final `.bin` is built:

```powershell
python tools/ota_manifest.py build/Release/INA226.bin `
  https://firmware.example.com/INA226.bin 0x00010001 --pretty
```

The tool refuses a version or board ID that differs from the information
embedded in the binary. The signature covers this byte sequence:

```text
"STM32F407-OTA-V3" || board_id_le32 || version_le32 || size_le32
|| crc32_le32 || url_length_le32 || blake2b_256 || url_ascii
```

The Bootloader verifies the Ed25519 signature before erasing the application,
then verifies CRC32, BLAKE2b-256, vector addresses, board ID, and embedded
version after downloading. Successfully installed signed images are checked
again on each boot.

## Current OneNET property model

The current firmware uses a signed custom OneJSON OTA manifest. It accepts an
immediate `thing/property/set` command and also actively reads the same eight
fields from OneNET desired properties. All eight writable request properties
must describe one firmware image and must be updated together.

| Identifier | Type | Access | Constraint |
| --- | --- | --- | --- |
| `ota_upgrade` | Boolean | read/write | Must be `true` |
| `ota_url` | String | read/write | Direct HTTP(S), 1-255 chars |
| `ota_size` | Integer | read/write | 8-393216 |
| `ota_crc32` | String | read/write | Example `0x1234ABCD` |
| `ota_version` | Integer | read/write | Decimal value of `0x00MMmmpp`, newer only |
| `ota_board_id` | String | read/write | Must be `0xF4070001` |
| `ota_hash` | String | read/write | 64 lowercase hex chars |
| `ota_signature` | String | read/write | 128 lowercase hex chars |
| `fw_version` | Integer | read-only | Installed version |
| `ota_target_version` | Integer | read-only | Requested version |
| `ota_state` | Integer | read-only | 1-5, see below |
| `ota_progress` | Integer | read-only | 0-100 |
| `ota_result` | Integer | read-only | 0 none, 1 running, 2 success, 3 failed |
| `ota_error` | Integer | read-only | 0-14, from `OTA_Error_t` |

`ota_state` values are: 1 `CONFIRMED`, 2 `REQUESTED`, 3 `INSTALLING`,
4 `TRIAL`, and 5 `RECOVERY`. The command must also contain a non-empty root
`id` of at most 47 characters. The application replies on
`thing/property/set_reply` with code 200 after metadata is committed.

Do not mix `home_feng`, `home_led`, `home_load`, or `qi` with OTA fields in one
`property/set`; mixed commands are rejected as a unit. This prevents an
accepted OTA reset from silently discarding an unrelated switch command.

The binary itself is not carried in the property message. The ESP8266 downloads
it from `ota_url`. The server must return a direct 200 response and an exact
`Content-Length`, must support GET (HEAD is preferred), and must not redirect or
use chunked transfer encoding. The signed URL must be ASCII, use a DNS name or
IPv4 host, and contain no credentials, fragment, whitespace, quotes, or
backslashes.

For an immediate online update, send the generated JSON to:

```text
$sys/{product-id}/{device-name}/thing/property/set
```

For an update that must survive the device being offline, store all eight OTA
fields as OneNET desired-property values. After every MQTT connection, once the
running application is confirmed, the device publishes a query to:

```text
$sys/{product-id}/{device-name}/thing/property/desired/get
```

and subscribes to:

```text
$sys/{product-id}/{device-name}/thing/property/desired/get/reply
```

It waits up to five seconds for the matching reply and retries the same request
ID three times. If no matching reply arrives, it clears that ID and reconnects
MQTT; the normal five-minute poll remains as a later backstop. A desired
manifest whose version is already installed is treated as satisfied. If the platform updates
desired fields separately, write `ota_upgrade=false` first, update the other
seven fields, and write `ota_upgrade=true` last; an atomic update of all eight
fields is preferred. The device fingerprints the complete observed tuple, so a
partially updated tuple cannot permanently blacklist the later corrected one.

## Publishing an update

1. Increment `FW_VERSION_MAJOR`, `FW_VERSION_MINOR`, or `FW_VERSION_PATCH` and
   keep `FW_VERSION_STRING` consistent.
2. Build the final Release application binary.
3. Put that exact `.bin` at a stable direct HTTP(S) URL. Do not use a web page,
   redirect, expiring link, or chunked response.
4. Generate the signed command with `tools/ota_manifest.py`. Never hand-edit
   any signed field afterward.
5. Send the complete `params` object as one online `property/set`, or store the
   same values together as OneNET desired properties for offline delivery.
6. Observe `ota_state`, `ota_progress`, `ota_result`, and `ota_error`. The
   device resets only after the manifest has been committed to internal Flash.

The current implementation does **not** yet consume a native OneNET OTA job
created by uploading a BIN in the OneNET OTA console. Native SOTA support would
also need `ota/inform` handling, a 20-second `ota/inform_reply`, an authorized
`/fuse-ota/.../check?type=2&version=...` request after every reconnect, package
status reporting, and a signed-manifest format in addition to OneNET's MD5.
The relevant OneNET protocol pages are:

- <https://iot.10086.cn/doc/aiot/fuse/detail/922>
- <https://iot.10086.cn/doc/aiot/fuse/detail/1447>
- <https://iot.10086.cn/doc/aiot/fuse/detail/904>

## Boot flow and delivery reliability

On every reset the Bootloader checks local metadata only. It does not connect to
OneNET during a normal boot. This keeps normal startup independent of Wi-Fi and
prevents cloud outages from blocking a confirmed application. A `REQUESTED`
record causes installation; a normal `CONFIRMED` record starts the application.
The application confirms a `TRIAL` image only after LVGL has run for 10 seconds.
The OneNET task waits for that explicit confirmation before requesting desired
OTA data, so OTA acceptance does not depend only on the previous 12-second
startup delay.

A committed `REQUESTED` record cannot be missed after reset. A OneNET
`property/set` message sent while the device is offline or before subscription
can still be missed because it is an immediate QoS 0 command. Use desired
properties for persistent delivery. On every MQTT connection the application
reports `fw_version` and OTA state, then queries desired OTA data after reaching
`CONFIRMED`; it also refreshes the query every five minutes.

## Recovery limits

Preflight failure while the old application is intact records the complete
manifest fingerprint and starts the old application. The same transient
Wi-Fi/HTTP failure is cooled down globally for five minutes and an identical
tuple is limited to three application cycles. A corrected signature/field tuple
is not permanently blacklisted; after a transient network failure it still
observes the global cooldown. After
application erasure begins, download failures
are tried three times per cycle; `RECOVERY` waits 30 seconds and retries the
same signed URL so a repaired network or server can recover the device. A new
image is started as `TRIAL` and must run LVGL for 10 seconds before confirmation,
within at most three boots.

This single-slot implementation still cannot restore the previous image.
Invalid metadata or an exhausted recovery budget requires SWD recovery.
Automatic recovery is bounded: the initial installation
has at most three attempts, followed by at most three persisted recovery cycles
of three attempts each. After 12 total erase/download attempts the Bootloader
stays in `RECOVERY` and stops erasing Flash until serviced. Use external SPI
Flash or SD storage for a future version that must retain and restore the old
application automatically.

Before treating this as a production boot chain, validate two remaining
hardware limits: `TRIAL` has no independent watchdog if the application
hard-locks, and the 115200-baud ESP8266 download writes internal Flash while
receiving on UART. Validate UART overrun behavior and interrupted
erase/program operations on the target board. The 48 KiB Bootloader region also
has limited growth margin, so keep its link-size check enabled. When replacing
the application through SWD during development, erase/reprogram Sectors 3 and 4
as well so old signed metadata does not reject the new image.
