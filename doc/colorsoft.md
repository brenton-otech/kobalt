# Kindle Colorsoft target

This target packages the standalone Kindle reader for the Kindle Colorsoft
(12th generation, Kaleido 3 color panel). It builds the PW5 application with
the same hard-float SDK. The package has its own output, its own runtime
preflight tag and a stock Colorsoft userspace fixture. The display adapter
detects the color framebuffer at runtime.
The PW3 softfp toolchain does not apply. Besides the KUAL ZIP, the target
produces a KPM package for the Véra jailbreak, which has no KUAL.

The current beta.7 revision 1 packages and engineering results are recorded in
[Kindle maintenance](kindle-maintenance.md#colorsoft-gen-1-beta7-engineering-result).
That build includes the folder and EPUB pickers, the embedded Library icon and
the Colorsoft Home recovery fix. It passed the hard-float package, adapter and
stock-runtime reader checks. The powered-off device has not received this build;
physical color, input, sleep and exit checks remain pending. Dated prototype
results below describe earlier packages.

## Build contract

| Property | Colorsoft | Evidence |
| --- | --- | --- |
| Floating-point ABI | ARM EABI5 hard-float | Stock `libc.so.6` ELF flags `0x5000400, Version5 EABI, hard-float ABI` |
| Program interpreter | `/lib/ld-linux-armhf.so.3` | Regular file in the stock rootfs; stock `libc.so.6` requests it |
| `/lib/ld-linux.so.3` | Symlink to `/lib/ld-linux-armhf.so.3` | Stock rootfs inode is a fast link to that path |
| Stock glibc | 2.35 on firmware 5.20.1 | `GNU C Library (GNU libc) stable release version 2.35.` |
| Stock libstdc++ | `libstdc++.so.6.0.29` (GCC 11) | `/usr/lib` in the stock rootfs |
| SoC | MediaTek MT8110 (`mt8110_bellatrix4`) | Firmware 5.20.1 update payload member name |
| Platform | Bellatrix4, codename SeaBreeze | FBInk device table, firmware release string |
| Display controller | `hwtcon_v2`, the same MediaTek ABI as PW5 | FBInk `mtk-kindle.h`, KOReader `framebuffer_mxcfb.lua` |
| Panel | 300 PPI, color filter array (CFA) at 32 bits per pixel | FBInk and KOReader device tables |

Firmware 5.16.3 and later use KOReader's `kindlehf` packages. The Colorsoft
shipped after that change, so it uses the PW5 toolchain, not PW3's
`kindlepw2` softfp toolchain.

The application is built with the pinned
[koxtoolchain 2026.08](https://github.com/koreader/koxtoolchain/releases/tag/2026.08)
`kindlehf.tar.zst`. Its SHA-256 is
`8cc7dfbd71abd78f9e947d6b2e20670288a4402edc7b07176bca791f7eaf87d0`.
Its crosstool-NG log shows GCC 14.4.0 configured with
`--with-cpu=cortex-a9 --with-fpu=neon --with-float=hard --with-mode=thumb`,
and its sysroot holds glibc 2.20 (`ld-2.20.so`, `libc-2.20.so`).

Firmware 5.20.1 moved from glibc 2.20 to glibc 2.35. Packages still keep
the glibc 2.20 symbol ceiling, and verification still enforces it. glibc
is backward compatible, so one package runs on the current 5.20.1
userspace and on older Colorsoft firmware that still ships glibc 2.20.
The package bundles GCC 14's `libstdc++`, `libgcc_s` and `libatomic`, which
are newer than the stock GCC 11 runtime. The PW5 Qt cleanup patch remains
harmless on glibc 2.35.

### SDK image

The Colorsoft and PW5 targets both consume `localhost/kobuild:kindle`. As
of the initial SDK work, neither `brenton-otech/kobuild` (`main`, `feature/pw3-sdk`)
nor `nicoverbruggen/kobuild` provides that target. The PW3 change removed
the application-side recipe and pointed at kobuild, but the kobuild target
was never added. The original validated local image came from a kobuild `kindle`
target modeled directly on `kindle-pw3`:

- `kindle/prepare.sh` pins `kindlehf-2026.08.tar.zst` with the SHA-256
  above, together with the zlib 1.3.1, OpenSSL 3.5.9 and Qt 6.8.3 Base
  and Sensors archives already pinned for PW3.
- `kindle/toolchain.cmake` selects `arm-kindlehf-linux-gnueabihf` and keeps
  the compiler's own CPU defaults.
- `kindle/build.sh` builds OpenSSL and zlib with
  `-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -mthumb` and leaves Qt's
  NEON support enabled.
- `kindle/check.sh` requires the hard-float ABI and
  `/lib/ld-linux-armhf.so.3`.
- Only `qt-cleanup.patch` is applied. PW3's other patches compensate for its
  glibc 2.12 headers.
- `build.sh` accepts `kindle` and runs it as `linux/amd64`.

That change is commit `c5bc783` on the `fm/kindle-hf-sdk` branch of
`brenton-otech/kobuild`. It is not yet merged into `main`. Until it is,
prepare the image from that branch with `./build.sh kindle`. Without the
image, `platforms/kindle/build.sh colorsoft` and the default PW5 build both
fail with the existing "Prepare the Kindle SDK image in kobuild first"
message.

## Commands

From the Kobalt repository root, with Docker running and
`localhost/kobuild:kindle` prepared:

```sh
bash platforms/kindle/build.sh colorsoft
bash platforms/kindle/sdk/prepare-runtime.sh colorsoft
bash platforms/kindle/verify-package.sh colorsoft
```

The output stays under `build/kindle-colorsoft/` and the firmware fixture under
`build/kindle-colorsoft-sdk/`. The build writes two packages with the same
app files:

- `build/kindle-colorsoft/Kobalt-1.0-beta.7-kindle-colorsoft-r1.kpkg` for KPM
- `build/kindle-colorsoft/Kobalt-1.0-beta.7-kindle-colorsoft-r1.zip` for KUAL
  or a manual install

PW5 and PW3 keep their own commands, outputs, ZIP packages, fixtures and
locks. They do not produce a KPM package.

### KPM package

KPM installs a gzip tar with `manifest.json` and optional `install.sh`,
`launch.sh` and `uninstall.sh` hooks at its root. KPM runs the hooks with
`sh` from the unpacked package folder, `/mnt/us/kmc/kpm/packages/kobalt/`.
That folder is deleted and extracted again on every upgrade. The sources
are KPM's
[README](https://github.com/KindleModding/KPM/blob/main/README.md),
`kpm-helper.py`, `src/install.c` and `src/uninstall.c`, and the
[example package](https://github.com/KindleModding/example_kpm_package).
Kobalt's data therefore cannot live in the package folder. The package
follows the pattern of the official KOReader package and installs the app
in `/mnt/us/kobalt`, which is where the ZIP puts it.

`package.cmake` writes the archive with `cmake -E tar` rather than
`kpm-helper.py`, so the build gains no Python step. The result matches the
helper's output: a gzip tar with `manifest.json` first. The manifest has
`manifest_version` 2, ID `kobalt`, and `supported_platforms` set to
`kindlehf`. Beta.7 revision 1 uses `[1, 0, 207001]`, encoding the upstream
prerelease and local revision instead of dropping the beta suffix. KPM stores
these components as [unsigned integers](https://github.com/KindleModding/KPM/blob/4800e09/src/include/kpm/semver.h).
The description includes the unofficial build label. The hooks are in
`platforms/kindle/kpm/`:

- `install.sh` moves each app file from the package folder into
  `/mnt/us/kobalt` by renaming it, so no second copy is written. It never
  writes `data/` (settings and reading progress) or `logs/`. It records the
  installed files in `/mnt/us/kobalt/kpm-installed.txt` and removes files
  that only an earlier package installed. It copies the `Kobalt.sh`
  scriptlet to `documents/`.
- `uninstall.sh upgrade`, which KPM runs before an upgrade, keeps the app.
  A full uninstall removes only the recorded files, the directories they
  leave empty and an unchanged scriptlet. `data/`, `logs/` and any other
  file stay.
- `launch.sh` runs `sh /mnt/us/kobalt/launcher/launch.sh "$@"`, which is the
  same command as the KUAL entry.
- Both hooks refuse to run while `/var/tmp/kobalt-kindle-prototype` exists,
  because Kobalt is running or its supervisor failed.

The scriptlet embeds a Kobalt PNG as its Kindle Library icon. Its appearance
requires on-device validation. The scriptlet runs `/var/local/kmc/bin/kpm launch kobalt`, as
KindleModding recommends. It omits `# DontUseFBInk`, so launcher refusals,
such as an enabled passcode, appear on screen.

The fixture is Amazon's `update_kindle_colorsoft_5.20.1.bin`, at 495503556
bytes with SHA-256
`6d2fc159937b244d49cc9dd0bcc5f5551b7ff103a02a9fc74b1b6628f2b61aa9`.
Its `/etc/version.txt` reports
`050-juno_2001_seabreeze_bellatrix4-489385`. Preparation checks the stock
loader (`a089ca56fba8e33fb8d87b791ac9f81c9065d0ae9d9d0ffb899037f7ed284701`)
and `libc.so.6`
(`5a34d04c0392bf6b69b361ffab68b8c2a064b444a4c6c58891a3607bd4593431`).
Each target now records its release string, loader, libc and stock glibc
version in `platforms/kindle/target.sh`, rather than in branches inside the
preparation script.

## Display

FBInk's `mtk-kindle.h`, taken from the Bellatrix4 kernel, adds the color
waveform modes `GCC16 = 14`, `GLRC16 = 15`, `GCCK16 = 16` and `GLRCK16 = 17`.
It also adds the framebuffer grayscale values `GRAYSCALE_COLOR = 0` and
`GRAYSCALE_COLOR_NIGHTMODE = 3`. The update structure and ioctl numbers are
unchanged from PW5, so the existing 96-byte update layout applies.

KOReader's `framebuffer_mxcfb.lua` records three driver behaviors that the
adapter follows:

- On color panels, AUTO chooses only between DU and GC16. Antialiased content
  therefore becomes a flashing GC16 partial update. Partial updates request
  GL16 instead.
- The CFA waveform modes are never chosen automatically and must be full
  updates. Full refreshes request GCC16 with the full update mode.
- Swipe updates leave the waveform on AUTO with the swipe flag, so the kernel
  can select its own swipe waveforms.

The adapter accepts a 32-bit `hwtcon_v2` framebuffer only when green is the
middle byte and red and blue use the remaining two 8-bit channels. Grayscale
must be 0 or 3. It copies Qt's `Format_RGB32` scanlines directly when red
is the high channel (B, G, R, X in memory). It swaps red and blue when the
driver reports red at offset 0. Color night mode (grayscale 3) inverts RGB
like 8-bit night mode. The 32-bit path is limited to `hwtcon_v2`; PW3's
`mxc_epdc_fb` path still accepts only 8-bit grayscale. Full-refresh dimensions
come from the framebuffer. The panel's reported 300 PPI matches the screen's
existing physical-size calculation. The reader enables its color-display
services when the framebuffer is 32-bit.

Power, battery and light controls use the existing LIPC `com.lab126.powerd`
properties. The battery status fallback already includes KOReader's Colorsoft
`bd71827_bat` node. Touch input still selects the first multitouch event node.

## Runtime preflight

The launcher's `runtime-target` file contains `colorsoft`. The preflight
requires `/lib/ld-linux-armhf.so.3`, the same as PW5.

Colorsoft 5.20.1 also links `/lib/ld-linux.so.3` to the hard-float loader.
Without a further check, the PW3 softfp package would pass its loader
preflight there. The PW3 check now also refuses a system that has the
hard-float loader. A real PW3 has no hard-float loader, so its behavior is
unchanged. The preflight test covers both cases.

## Install and launch

The launcher refuses to run while the Kindle passcode is enabled. Before
starting Kobalt, exit KOReader or any other reader that has stopped the stock
UI. Kobalt refuses a volume daemon that another reader has already paused.

### With KPM (Véra jailbreak)

The Véra jailbreak installs the KindleModding hotfix and KPM, but no KUAL.
On a Mac, the Colorsoft's USB storage is reached over MTP, for example with
[OpenMTP](https://openmtp.ganeshrvel.com/). The top level of its storage is
`/mnt/us` on the device.

1. Connect the Colorsoft to the Mac and open OpenMTP. Copy
   `Kobalt-1.0-beta.7-kindle-colorsoft-r1.kpkg` to the top level of the Kindle
   storage, beside `documents/`. Copy DRM-free EPUB files to `documents/`.
   The default library scans that folder recursively.
2. Eject the Kindle in OpenMTP and unplug it. On the Home screen, tap the
   search bar and enter:

   ```text
   ;kpm install file:///mnt/us/Kobalt-1.0-beta.7-kindle-colorsoft-r1.kpkg
   ```

   KPM shows its log on screen. The install hook ends with
   `Kobalt installed. Open "Kobalt" in the Library to start it.`
3. Open **Kobalt** in the Library. It is the `documents/Kobalt.sh`
   scriptlet, and it runs `kpm launch kobalt`.
4. To leave, close any open book with the Library arrow. Then choose **Exit
   Kobalt** from the Library's three-dot menu. The stock interface restarts.
5. The installed package file is no longer needed. Delete
   `Kobalt-1.0-beta.7-kindle-colorsoft-r1.kpkg` with OpenMTP if you want the
   space back.

To update, copy the new `.kpkg` the same way, then enter
`;kpm uninstall kobalt` followed by the install command above. The KPM
version tested refuses a local package over an installed `kobalt` package,
even a newer one. Uninstalling first is safe, because uninstall keeps
`kobalt/data/` (settings and reading progress) and `kobalt/logs/`.

To add another folder, open Kobalt's Library menu and select **Add folder**. Browse from Kindle storage, open the desired folder, and
choose **Use this folder**. Kobalt scans EPUBs recursively without moving them.
The Library menu also offers **Open a single book**.

`;kpm uninstall kobalt` removes the app files the package installed and the
unchanged scriptlet. It keeps `kobalt/data/`, `kobalt/logs/` and any other
file in `kobalt/`. Delete that folder with OpenMTP to remove everything.

If the search bar does not accept the command, run the same steps over SSH:
`/var/local/kmc/bin/kpm -y install file:///mnt/us/Kobalt-1.0-beta.7-kindle-colorsoft-r1.kpkg`.
A one-line scriptlet that runs that command from `documents/` also works.

### With KUAL

The device must be jailbroken with KUAL (or KUALA) installed.

1. Connect the Kindle over USB. Extract
   `Kobalt-1.0-beta.7-kindle-colorsoft-r1.zip` at the Kindle USB root. This
   creates or updates `kobalt/` and `extensions/kobalt/`. When updating,
   keep the existing `kobalt/data/`, which holds settings and reading
   progress.
2. Copy DRM-free EPUB files to `documents/`. The default library scans that
   folder recursively. Add other folders from the Library folders screen.
3. Eject the Kindle. Open KUAL, then choose **Kobalt prototype → Start
   Kobalt prototype**.
4. To leave, close any open book with the Library arrow. Then choose **Exit
   Kobalt** from the Library's three-dot menu. The stock interface restarts.

Over SSH, the same steps are: copy the ZIP to `/mnt/us/`, then run
`unzip -o /mnt/us/Kobalt-1.0-beta.7-kindle-colorsoft-r1.zip -d /mnt/us/`.
Use one install method at a time. KPM uninstall removes only the files its
own install recorded.

### Diagnostics

Run `LD_LIBRARY_PATH=/mnt/us/kobalt/lib /mnt/us/kobalt/bin/kobalt-device-info`
for a read-only diagnostic that does not stop the stock UI or open the
framebuffer. Launcher logs are written to `/mnt/us/kobalt/logs/launcher.log`.
The display line `kindle: native hwtcon_v2 WxH (32-bit …)` shows which
channel order and resolution the adapter detected.

If startup fails, the supervisor restores the stock UI. If the supervisor
itself is killed, follow the recovery steps in
[Kindle maintenance](kindle-maintenance.md). A reboot also recovers, because
Kobalt installs no boot-time launcher.

## Validation and gaps

`mtk_display_test` covers the color layout, both channel orders, color
inversion, GCC16 full updates, GL16 partial updates, swipe waveform
selection and rejection of grayscale snapshots for a 32-bit framebuffer.
`touch_state_test`, `refresh_queue_test`, the runtime preflight and the
mocked supervisor checks are unchanged in scope. `kpm/hooks-test.sh`
(`kindle.kpm-hooks` in the native CTest set) runs the KPM hooks the way KPM
does. It covers a fresh install over a ZIP install, an upgrade that retires a
library, refusal while Kobalt runs, an uninstall that keeps data, logs,
unrelated files and a changed scriptlet, and a clean reinstall and uninstall.
For the Colorsoft, `verify-package.sh` also checks the `.kpkg`: manifest first,
ID and platform, hooks identical to the sources, app files identical to the
ZIP's. It then runs the hook test.

### Local result, 2026-11-06

This run used an Apple Silicon Mac with Docker Desktop. The SDK image was
built from the kobuild `kindle` target described above. The amd64
SDK ran under emulation, and the verification container ran natively on
ARM64. Both of the image's offline SDK checks passed: compile/link, hard-float
ABI, interpreter, dependency closure and GLIBC ≤ 2.20.

`bash platforms/kindle/build.sh colorsoft` at commit `f531843` produced
both packages:

| Package | Bytes | SHA-256 |
| --- | --- | --- |
| `Kobalt-kindle-colorsoft-prototype.kpkg` | 54022483 | `6ee660bbdaaff5acee7045e1ff661b787a13a45544a9889f09c5f5b5b136966e` |
| `Kobalt-kindle-colorsoft-prototype.zip` | 54111612 | `ef0426f3e279dd2587d2e9ab421431ed8ab3a7dea2600807de3c59aa78011b0f` |

`bash platforms/kindle/sdk/prepare-runtime.sh colorsoft` prepared and then
re-verified the 5.20.1 fixture.

`bash platforms/kindle/verify-package.sh colorsoft` passed the KPM package
checks and the ARM ELF audit:
hard-float ABI, `/lib/ld-linux-armhf.so.3`, dependency closure and GLIBC ≤ 2.20.
The power-helper checks passed. The Type Tester smoke also passed with
`KOBALT_STRICT_MAIN_THREAD=1` against the stock glibc 2.35 userspace. It
opened the Library and EPUB, changed page pixels, checkpointed, then reopened
and restored those pixels.

`mtk_display_test` and `touch_state_test` were cross-compiled with the SDK.
Both passed under ARM QEMU against the same stock runtime and the packaged
libraries. They also passed natively in an ARM64 Ubuntu 24.04 container
with distribution Qt 6.4.2 and GCC 13, along with `refresh_queue_test`,
the runtime preflight and the mocked supervisor checks. On that Qt,
`refresh_queue_test` needs `-Werror` removed because of a sign-compare
warning in Qt 6.4's own `qlist.h`. The full `kindle-native` CTest preset
was not run because this Mac has no prepared native Qt 6.11 SDK or CMake.

The KPM hook test passed under macOS `sh`, BusyBox 1.36 and the stock
Colorsoft 5.20.1 BusyBox. The stock binary, `/bin/busybox.nosuid`, was
extracted from the fixture firmware and run under ARM emulation; every applet
the hooks use is present in that firmware's `/bin`.

The real KPM CLI was built from `KindleModding/KPM` commit `4800e09` for
`kindlehf`, with the stock package and database paths, and run in an ARM64
Debian container:

- `kpm -y install file:///mnt/us/Kobalt-kindle-colorsoft-prototype.kpkg`
  moved the app into `/mnt/us/kobalt`, kept the existing `data/` and
  `logs/`, and installed `documents/Kobalt.sh`. No app copy remained in the
  package folder.
- `kpm launch kobalt` reached Kobalt's launcher. Its preflight then refused
  the container because it has no hard-float loader, as expected.
- `kpm -y uninstall kobalt` removed the app files and the scriptlet, and kept
  `data/` and `logs/`.
- Installing a local package over the installed one failed inside KPM with
  "Could not resolve dependency graph". This happened with the same and a
  newer version, and with and without the online index. Uninstalling and
  then installing worked and kept `data/`. The update steps above therefore
  uninstall first.

Building that CLI outside KMC's toolchain needed the system `sqlite3.h`,
because the CLI includes it without the subproject's include path. Not
established: that Véra's search bar passes the `file://` argument through
unchanged, which KPM version Véra installs, and scriptlet launch on the
device.

PW5 still builds and passes `verify-package.sh` against its 5.19.2 fixture.
At commit `d21d3da`, PW3 also built from a freshly prepared
`localhost/kobuild:kindle-pw3` image, whose own SDK checks passed. It produced
only its ZIP, with no KPM package, and passed `verify-package.sh pw3`: the
softfp ELF audit, the power-helper checks and the stock 5.13.5 reading smoke.
The PW5 and PW3 fixtures re-prepared and verified with their unchanged pinned
release strings and loader and libc digests after the fixture metadata moved
into `target.sh`.

Not established here: physical color rendering, the stock framebuffer's
actual channel offsets and stride, panel rotation and touch alignment,
refresh timing, sleep and wake, USB export, and stock-UI recovery on the
Colorsoft. The user performs on-device testing. Native, QEMU userspace and
firmware-fixture checks do not establish hardware compatibility.
