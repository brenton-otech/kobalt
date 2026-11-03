# Paperwhite 3 SDK and runtime work

This work follows the PW3 SDK handoff for a device on firmware 5.13.5.
Its missing `/lib/ld-linux-armhf.so.3` explained
the earlier package's launcher refusal.

## Current device validation

The user confirmed that the installed PW3 build works on the physical device:
the copied EPUB library opens, the light-panel slider changes brightness, and
a vertical drag inside the left edge changes brightness. No code change was
needed for the gesture after testing a longer drag. Device logs also show the
reader staying alive through screensaver entry and wake after the startup
helper repair. The user approved this candidate for pushing to the PW3 branch.

This is focused physical validation, not a complete hardware qualification.
USB export, every recovery path and measured panel timing remain outside the
confirmed scope. The sections below retain the investigation and deployment
history; statements about tests still pending describe their respective stage.

## Build contract

The separate kobuild `kindle-pw3` target prepares Qt Base and Sensors 6.8.3,
SQLite, linked OpenSSL 3.5.9 and zlib 1.3.1. Source URLs and checksums belong
to that repository. Kobalt consumes `localhost/kobuild:kindle-pw3`; it never
compiles Qt. The retired application-side SDK recipe has been removed.

The pinned [koxtoolchain 2026.08 release](https://github.com/koreader/koxtoolchain/releases/tag/2026.08)
`kindlepw2.tar.zst` has SHA-256
`84bc8b4143796ba0c9e6306aefc596ca2e2685cf9174a285bb14f36425de8104`.
Compiler inspection established GCC 14.4.0, base AAPCS floating-point calling
conventions and `/lib/ld-linux.so.3`. CPU features alone did not establish
this ABI. The compiler defaults to NEON; the SDK explicitly selects
ARMv7-A/VFPv3/softfp for Qt and application code. Prebuilt compiler support
libraries passed userspace validation. A subsequent read-only device probe
confirmed both NEON and VFPv3; application execution on the physical device
remains untested.

Run the PW3 commands in [Building](building.md#kindle-prototype). PW3 and PW5
keep separate image tags, CMake output directories, packages, firmware caches
and application locks. Dependency cache identity includes the selected SDK
image ID. The install layout remains `kobalt/` and
`extensions/kobalt/`; existing `kobalt/data/` must be preserved when updating.

Amazon's `update_kindle_all_new_paperwhite_5.13.5.bin` is pinned at
253013186 bytes and SHA-256
`943796120a94f96743f9c34d59b9727a21c91a15e61ef91f9b23159c6d045775`.
Fixture preparation uses the existing pinned KindleTool and read-only
debugfs extraction. The PW5 fixture and its checks remain separate.

## Display and input audit

The previous 72-byte EPDC update definition placed temperature and flags
at the wrong offsets. The pre-Zelda Kindle ABI has histogram waveform fields
at offsets 28 and 32, temperature at 36, flags at 40 and alternate-buffer data
at 44. These are now explicit compile-time assertions. The independently
declared fields were checked against Amazon's matching firmware source header
and the [FBInk Kindle header](https://github.com/NiLuJe/FBInk/blob/886f25f13368859ad8a899b88d04c26e19cda32e/eink/mxcfb-kindle.h).
The corresponding update ioctl remains `0x4048462e`; matching structure size
alone did not establish a matching payload.

The [Amazon 5.13.5 PW3 source archive](https://s3.amazonaws.com/kindledownloads/Kindle_src_5.13.5_3716750030.tar.gz)
is 332758590 bytes, SHA-256
`ce79fa91abd63d531438a6d51c09746d4e11b9719588ed74ba5e036c90eaf3ec`.
Its nested `gplrelease/linux-3.0.35.tar.gz` supplies `include/linux/mxcfb.h`
and `drivers/video/mxc/mxc_epdc_fb.c`. The header confirms the structure
offsets and both ioctl numbers; the driver consumes the histogram fields
for AUTO updates. Its completion wait returns `-ETIMEDOUT` after five seconds,
or zero for an already completed marker. The existing worker accepts a
positive completion result and treats errors or a near-timeout wait as
uncertain. It does not block reader input during that wait.

The current adapter accepts the firmware's 8-bit grayscale framebuffer,
uses its stride and dimensions, and waits for updates on the existing worker.
It does not change the panel mode. Read-only device inspection confirmed the
1072 × 1448 viewport, 8-bit non-inverted grayscale and matching stride; see
the measured layout below. Physical update acknowledgments, panel behavior
and timing remain untested.

The source's Wario configuration enables the Cypress protocol B module.
`cyttsp4_mtb.c` reports slots and tracking IDs; `mx6_cyttsp4.c` declares
multitouch X/Y axes. The same firmware's `evdev.c` and `input.h` lack
`EVIOCGMTSLOTS`, so requiring a slot snapshot would prevent startup. The
adapter now learns coordinates from events on that kernel, preserves observed
slot coordinates between contacts, and invalidates them after dropped events.
It suppresses a whole contact whose coordinates are not yet known; an initial
or post-drop contact may therefore be ignored until both axes have been
observed. This avoids invented presses and restarting a gesture halfway through.
Modern kernels retain their slot snapshot path. This source audit does not
identify the event node or prove coordinate mapping on the physical device.
Input currently requires multitouch position axes and supports protocol A
packets or protocol B slots. It grabs only the selected touch node and
releases it during teardown. PW3 event-node identity, axis ranges, coordinate
mapping, release events and interrupted contacts remain unverified on hardware.

The supervisor still requires Upstart `lab126_gui`, `volumd`, LIPC power and
blanket services. Firmware ABI checks do not establish these service contracts.
Use the recovery procedure in Building before any supervised launch. Confirm
Home is interactive, the exact volume daemon has resumed, and the original
screensaver module has been restored after exit or failure.

Shutdown now sends the reader one TERM before its bounded checkpoint wait;
the previous handler and cleanup each sent TERM, which could reenter a
shutdown handler. The SDK's Qt cleanup patch lets the application return
normally and run its destructors; the application-wide `_exit` workaround
has been removed.

## Validation

`bash platforms/kindle/verify-package.sh --self-test` checks the glibc baseline
parser. `sh platforms/kindle/launcher/runtime-test.sh build/kindle-pw3/test-tmp`
checks both loader selections and rejects the opposite ABI and unknown targets.
The native Kindle preset includes this preflight test alongside the existing
supervisor, input, display and device checks. The PW3 package verifier checks
ARM ELF headers, interpreter, dependency closure and glibc requirements before
running the Type Tester smoke scenario against stock firmware userspace.

Native and QEMU userspace results do not establish hardware compatibility.
No device deployment is performed by these commands.

### Local result, 2026-11-03

The separately built PW3 SDK passed its offline compiler/linker, Qt module,
plugin, softfp, interpreter, dependency-closure and glibc checks. The configured
application rejected that SDK when deliberately selecting the PW5 target.

`bash platforms/kindle/build.sh pw3` produced
`build/kindle-pw3/Kobalt-kindle-pw3-prototype.zip`, SHA-256
`bf7c3da8ca08c06ca2ba88bbbbf9dd6c953ff4f067089d980b35514bf52440b3`.
The final incremental build reused the completed application dependencies and
reader objects while rebuilding the changed input adapter and relinking.

`bash platforms/kindle/verify-package.sh pw3` passed the package ABI and
dependency audit, power-helper checks and stock-5.13.5 userspace smoke with
`KOBALT_STRICT_MAIN_THREAD=1`. The Type Tester smoke opened the Library and
EPUB, changed page pixels, checkpointed, reopened and restored page pixels,
then exited through normal application teardown. The cross-compiled display
unit also passed against this same stock runtime and packaged libraries.

The standalone native touch-state checks passed, including unknown-coordinate
suppression, reused slots and recovery after dropped events. Loader preflight,
glibc-parser and mocked supervisor recovery checks passed. The complete native
Qt suite was not run because this Windows/WSL host has no prepared native Qt
SDK. No Kobo or Android changes required validation.

Current logs are `build/kindle-pw3/verification.log`, `arm-smoke.log`,
`display-check.log`, `focused-build.log` and `supervisor-test.log`; the SDK
build log is in the kobuild worktree's `build/sdk-build.log`. The earlier
compile failures were resolved in the SDK or platform that owns them.
Physical display, touch, sleep, USB and stock-service recovery remain untested.

### Read-only device result, 2026-11-03

SSH inspection confirmed firmware build `030-juno_1305_muscat_wario-371675`
and kernel `3.0.35-lab126`. The device's loader and libc SHA-256 digests match
the pinned stock-runtime fixture exactly. Its CPU reports ARMv7, NEON and
VFPv3, with the Freescale i.MX 6SoloLite Wario hardware name. The softfp
loader is present; the hard-float loader is absent.

Framebuffer inspection used only read ioctls through the already installed
LuaJIT interpreter. It did not map or write framebuffer memory, request an
update, change the mode or grab input.

| Framebuffer field | Observed value |
| --- | --- |
| Driver | `mxc_epdc_fb` |
| Visible size | 1072 × 1448 pixels |
| Virtual size | 1088 × 6144 pixels |
| Stride | 1088 bytes |
| Bits per pixel / grayscale | 8 / 1 |
| X / Y offset | 0 / 0 |
| Rotation | 3 |
| Storage length | 6782976 bytes |
| Type / visual | Packed pixels / static pseudocolor |

These values satisfy the current display adapter's layout checks. They do
not establish physical refresh correctness or touch alignment.

The touch node is `/dev/input/event1`, named `cyttsp4_mt`. Read-only axis
queries report X 0–1071, Y 0–1447, slots 0–10 and tracking IDs 0–65535.
`EVIOCGMTSLOTS` returns `EINVAL`, confirming the legacy fallback is needed
on this actual kernel. The reported axis ranges agree with the adapter's
portrait coordinate normalization; physical tap delivery remains untested.

Power-service reads returned brightness 18 with maximum 25, battery 89 percent
and charging enabled. The amber-level query returned no supported value.
The stock blanket service lists `screensaver`, `langpicker`, `blankwindow`
and `usb`. User storage permits execution, the passcode flag is absent and
there is no Kobalt recovery directory.

An existing KOReader process is running from `/mnt/us/koreader` and holds
both framebuffer and touch handles. `volumd` and `awesome` are already paused,
even though Upstart reports the services running. The Kobalt supervisor must
refuse an already paused volume daemon. No process was resumed or stopped,
and no Kobalt package was installed or launched. Exit KOReader normally
before a future supervised Kobalt launch; do not take over its paused services.

The local evidence is `build/kindle-pw3/device-read-only.log` and
`device-ioctl.log`. Existing SDK, package and smoke results were reused because
this inspection changed no application code or package contents.

### Old device package cleanup

At the user's request, the earlier incompatible installation was removed
from `/mnt/us/kobalt/`, along with its `/mnt/us/extensions/kobalt/` launcher
entry and `/mnt/us/kobalt-pw3.tar.gz`. The archive digest matched the original
handoff's `b747c1f90632ad28edc94eb15858ee5984da6cfce59ed01bf333abb924865fd7`.
The installed directory contained only package-owned program trees and no
saved-data directory. Cleanup targeted those program trees explicitly and
removed their parent directories only when empty.

Books, KOReader and its existing process state were preserved. At the end of
cleanup, the new PW3 ZIP had not been transferred, installed or launched.

### PW3 package upload

At the user's subsequent request, the verified ZIP was uploaded to
`/mnt/us/Kobalt-kindle-pw3-prototype.zip`. Its size is 53547305 bytes and its
device-side SHA-256 matches the validated local artifact:
`bf7c3da8ca08c06ca2ba88bbbbf9dd6c953ff4f067089d980b35514bf52440b3`.
The upload used a temporary filename and was renamed only after checksum
verification. At upload completion, the archive had not yet been extracted
or launched. Existing build and test results were reused because package
contents are unchanged.

### Device file installation

At the user's request, the ZIP was extracted over SSH and its files installed
in `/mnt/us/kobalt/` and `/mnt/us/extensions/kobalt/`. Extraction used a new
temporary directory on user storage. Each staged file was checked against
SHA-256 digests generated directly from the verified local ZIP before the
directories were moved into place. All installed files were then checked
again. The temporary directory and its manifest were removed afterward.

The PW3 runtime preflight and executable-storage checks passed. The KUAL
`Start Kobalt prototype` entry and reader executables are in place. Books,
KOReader and its paused stock-process state were preserved. Kobalt was not
launched, and physical reader interaction remains untested. Exit KOReader
normally before choosing the Kobalt entry in KUAL.

### Startup helper timeout and recovery

The first physical launches reached the Library, then returned to the stock
interface after roughly ten seconds. The loading bar came from the stock
framework restarting during supervisor recovery. The system uptime did not
reset, and the stock volume daemon and window manager kept their process IDs.

Both captured attempts ended with `Cannot inspect stock screen overlays: 15`
and `Kindle power service stopped (exit 1, normal exit)`. Stock LIPC logs
identify error 15 as a timeout on `com.lab126.blanket.load`: the calls took
10015 ms and 10000 ms. The app closed on helper failure, and the supervisor
restored the screensaver and framework. These logs do not show a Kobalt
segmentation fault or establish a kernel reboot. The exact internal LIPC
dispatch cause remains unproven; the failing operation is identified directly.

Overlay release and confirmation now belong entirely to the supervisor,
before reader startup. It uses short-lived stock command-line clients, waits
for asynchronous removal, and recovers without starting the reader if an
unload or status query fails. The power helper no longer loads, unloads or
polls blanket modules through its live service connection. It retains its
framework-ready property, event callbacks and bounded suspend deferral.

Focused regressions fail with the previous implementation and pass with the
change. They cover delayed overlay removal, failed unload, failed status
query, a module that never unloads, restoration, and helper readiness when
overlay queries would time out. The updated package passed its ELF/dependency
audit, power-helper checks and strict stock-runtime smoke. The prepared SDK
and literary/UI suites were not rebuilt or rerun. Package comparison confirmed
that only `kobalt/bin/kobalt-power-bridge` and `kobalt/launcher/supervisor.sh`
changed; the reader binary and every other packaged file are unchanged.

The replacement ZIP SHA-256 is
`f5d810deff8d7717d8c2f933f0e2235e187c1bb7d63f212e2219de596de60d5b`.
The failed package and captured logs are retained locally under
`build/kindle-pw3/idle-exit-evidence/`. The helper, supervisor and installer
archive were updated on the device with checksums verified before replacement.
Saved data and the active KOReader session were preserved. Kobalt remains
stopped; a new physical launch is still needed to confirm the repair.

### Device library selection

The user's library is under `/mnt/us/epubs`, while the prototype defaults to
`/mnt/us/documents`. At the user's request it was copied to
`/mnt/us/kobalt/books`, including the existing folder structure and KOReader
sidecars. Every EPUB copy passed SHA-256 verification. Because KOReader was
active, its original library was preserved. No KOReader progress was imported
into Kobalt. The device's KUAL action now passes
`--library /mnt/us/kobalt/books`; the original menu is retained alongside it
as `menu.json.before-library`. This is device configuration, with no package
or application rebuild. Kobalt was left stopped.
