# Unofficial Kindle builds

This port starts at Nico Verbruggen's `v1.0-beta.7` tag, commit
`e86533c80e8aa41bfc7d79c9e445cbd4a6405039`. The original Kindle implementation
was ported from `a45f983` without importing its older shared application tree.
The upstream release and this port have separate identities: an example is
`Kobalt 1.0-beta.7 · unofficial Kindle pw5 build 1`, distributed as
`Kobalt-1.0-beta.7-kindle-pw5-r1.zip`.

## Updating upstream

Keep the fork's `main` as a fast-forward mirror of `upstream/main`. Develop
Kindle changes on `feature/` branches in matching worktrees. Base installable
builds on upstream release tags rather than arbitrary development commits.
Port the platform patch set onto the next tag, resolve shared API changes,
and update the upstream commit in `platforms/kindle/build-identity.cmake`.
Do not rewrite a published Kindle branch or merge old shared source over a
new upstream release. Keep platform changes and necessary shared hooks easy
to review separately when commits are authorized.

PW3, PW5 and Colorsoft share one implementation with separate build targets.
Prefer fixing a general reader issue upstream over maintaining a second
reader implementation. Shared changes currently provide the local folder/book
picker, Library Exit action, quantized light steps and battery status updates.
The upstream Library About actions and minute-tick work remain in place.

## Building

Read [Building](building.md) first. Compiler, sysroot, Qt, TLS and SDK production
belong to kobuild. Prepare `localhost/kobuild:kindle` for PW5/Colorsoft and
`localhost/kobuild:kindle-pw3` for PW3 separately in kobuild. Never mount another
SDK over `/sdk`. All Kindle SDK containers use `linux/amd64`.

From the worktree root on Linux or WSL with Docker integration:

```sh
bash platforms/kindle/build.sh pw5
bash platforms/kindle/verify-package.sh pw5
```

Substitute `pw3` or `colorsoft` in both commands. Outputs remain in
`build/kindle/`, `build/kindle-pw3/` and `build/kindle-colorsoft/` respectively.
The build uses upstream's content-keyed dependency cache, mounted separately
from the worktree, and stages runtime assets from this worktree only. The
launcher neither pulls an SDK image nor installs tools automatically.

Set `KOBALT_KINDLE_REVISION=2` (and increment for later distributions) when
building another revision of the same upstream version. ZIP and Colorsoft KPM
filenames include the version, target and revision. `package-artifact` identifies
the current artifact; verification reads it rather than selecting an older
archive. `kobalt/build-info.json` inside the package records the upstream pin,
source commit and modified flag, target, revision and SDK image ID.

KPM keeps the existing `kobalt` package ID. Its numeric version encodes the
upstream patch, release stage, prerelease number and Kindle revision, so beta
updates and local revisions increase instead of all becoming `1.0.0`.
Application name, organization and profile locations stay unchanged. Back up
the existing profile while Kobalt is closed before device upgrades; follow
[Storage compatibility](storage.md). A new application build does not reset data.

The older [PW3 SDK notes](pw3-sdk.md) and [Colorsoft notes](colorsoft.md) retain
historical firmware and hardware evidence. Their prototype filenames and
checksums describe earlier builds, not this beta.7 candidate. Use the versioned
artifacts from the current output for new installations.

## Validation

With a prepared native Qt SDK, configure/build `kindle-native` from
`platforms/kindle/` and run `ctest --preset kindle-native`. The shared picker
assertions are in the desktop reader UI suite; the full gallery declares the
folder and book picker scenes, including empty/error states. Run the affected
reader UI and model checks and inspect the picker alongside Library folders
before considering the port validated. Use the desktop presets documented in
[Building](building.md); do not run visible apps automatically.

For SDK adapter regressions, reuse the application's configured build directory,
matching SDK container and `/work` and `/dependency-cache` mounts. From
`platforms/kindle/` inside that container, enable the optional targets and build
them explicitly (substitute the matching preset for another target):

```sh
cmake --preset kindle-colorsoft -DKOBALT_KINDLE_ADAPTER_TESTS=ON
cmake --build --preset kindle-colorsoft --target kindle-mtk-display kindle-refresh-queue kindle-touch-state
```

These targets are excluded from normal SDK application builds and are never
installed in a package. The native preset retains its existing CTest targets.
On the host, run `KOBALT_KINDLE_VERIFY_ADAPTERS=1 bash platforms/kindle/verify-package.sh colorsoft`
to execute them with the matching packaged Qt libraries and stock firmware
userspace in the native verification container. Their synthetic frames do not
open a real framebuffer. The regular package audit and reading smoke still run.

Other independent checks are:

```sh
cmake -P platforms/kindle/build-identity-test.cmake
bash platforms/kindle/verify-package.sh --self-test
sh platforms/kindle/launcher/supervisor-test.sh /tmp/kobalt-shell-tests
sh platforms/kindle/launcher/runtime-test.sh "$PWD/build/kindle-shell-tests"
sh platforms/kindle/kpm/hooks-test.sh "$PWD/build/kindle-shell-tests"
bash platforms/kindle/power-bridge-test.sh "$PWD/build/kindle-shell-tests"
```

Run the shell checks inside a Linux test container. Supervisor checks need
scratch on its native filesystem so executable-bit changes take effect; Windows
bind mounts do not provide that behavior. The disposable container removes
that scratch directory. The checks use isolated fake
services and clean their own scratch state; never run the real device
supervisor on a development host. Package verification uses the native
verification container and checksum-pinned firmware fixtures prepared with
`platforms/kindle/sdk/prepare-runtime.sh` for the same target. Preserve shared
firmware and SDK caches.

The PW3 beta.7 revision 1 package was built with the prepared WSL Docker SDK
`sha256:d8faa66833563201baa3b49d5385ffbc46df8deb9dcced0944808255670ebb9c`.
Docker Desktop and WSL use separate engines on this host; the prepared Kindle
images are in WSL. The port adapts upstream's animation-capability API and
loopback-hook namespace. Optional diagnostic boot timestamps are compiled only
when the SDK defines `CLOCK_BOOTTIME`, matching the reading timer's portability
guard. Asset validation uses the upstream version independently of the displayed
unofficial build label.

The PW3 ELF/dependency audit and strict ARM userspace smoke passed against the
checksum-pinned 5.13.5 stock firmware runtime. The smoke selected an EPUB through
the file picker, opened Type Tester, checked changed page pixels, saved a
checkpoint and verified restored page pixels after reopening. Folder checks
covered storage boundaries, stale reads, paging, navigation, recursive scanning,
selection and cancellation. Focused picker captures are retained under
`build/kindle-pw3/folder-picker-captures/`; the entry and opened controls were
inspected in light, dark and enlarged-text captures alongside Library folders.
Every ZIP file was decompressed and its SHA-256 compared with the validated
package stage. The build-identity/version-ordering, shell syntax, audit parser,
supervisor recovery, runtime preflight, KPM hooks and power bridge results were
also retained or reused where unchanged.

The current package is `build/kindle-pw3/Kobalt-1.0-beta.7-kindle-pw3-r1.zip`,
with SHA-256
`be46ed4e6e0af6418039c99549a4dba6ff4d7c360d649cb891fa2174c543ffe5`.
Its adjacent `.sha256` file records the same digest. Build, verification and
archive-audit logs remain in that output directory. Source changes are
uncommitted; package metadata records the modified flag.

The native desktop suites and full gallery remain unrun without a prepared native
Qt SDK. The Kobo syntax check stopped because its pinned SDK image was unavailable.
The PW5 beta.7 package has not been built. ARM userspace validation
does not exercise the physical framebuffer, touch input, suspend or stock UI
recovery. This package is installed on the physical PW3, and the user confirmed
that the PW3 version works. Separate Colorsoft engineering results follow.

## Colorsoft gen 1 beta.7 engineering result

The Colorsoft beta.7 revision 1 KPM and ZIP packages were built with prepared
hard-float SDK image
`sha256:3ee1ac0327cedf9d91fbb43236a0a49c7d2832817ae51bfbe47765bd80487799`.
They include the focused exit-recovery fix from `8c5ffb0`: bounded stock commands,
an explicit Kindle Home activation request after GUI startup, retries and
retained recovery evidence on failure. Unrelated features from that branch
were not imported. The mocked supervisor suite passed, including the PW3 path,
delayed Home activation, failed requests and stalled stock clients.

The existing MTK/EPDC display, refresh-queue and touch-state regressions were
built with that SDK and passed under ARM QEMU against the checksum-pinned
Colorsoft 5.20.1 runtime. Display checks cover RGB32 initialization, both color
channel orders, inversion, bounds, stride and GCC16/GL16 waveforms. Refresh
checks cover ownership, coalescing, GUI responsiveness, cancellation, retry and
shutdown. A test helper was renamed to avoid the old sysroot's `union wait`;
its assertions are unchanged.

`KOBALT_KINDLE_VERIFY_ADAPTERS=1 bash platforms/kindle/verify-package.sh colorsoft`
passed the hard-float ELF/dependency audit, KPM hook lifecycle checks, adapter
regressions, power-helper checks and strict reading smoke. The reader selected
an EPUB, opened Type Tester, changed page pixels, checkpointed, reopened and
restored the saved page. Both archives were checked byte-for-byte against the
package stage, including the exit-recovery script. The KPM manifest retains ID
`kobalt`, platform `kindlehf`, version `[1, 0, 207001]` and the embedded Library
icon; reading data remains outside the package.

Artifacts under `build/kindle-colorsoft/`:

| File | SHA-256 |
| --- | --- |
| `Kobalt-1.0-beta.7-kindle-colorsoft-r1.kpkg` | `741dd2c81011868965296ffafbe3222bb85505666669fed5128ef613c427d241` |
| `Kobalt-1.0-beta.7-kindle-colorsoft-r1.zip` | `d375d6f2791839d53c3b086ccfe48320d90ba10504266e9b2b17bf7c16e93c23` |

Adjacent `.sha256` files, current validation logs and focused picker captures
are retained. The shared PW3 reading results and unchanged hook behavior were
reused where applicable; no full native gallery or desktop suite was rerun.
The Colorsoft was powered off, so nothing was transferred or installed. Physical
color output, touch alignment, sleep/wake and return to Kindle Home still need
device validation. Software and stock-userspace checks do not establish those
hardware behaviors.

### Fresh installation on another Colorsoft

The same verified revision 1 KPM artifact was installed on a new SeaBreeze
Colorsoft running firmware release `048-juno_2002_seabreeze_bellatrix4-493580`,
with the hard-float glibc 2.35 loader and KPM 0.2.4. There was no prior Kobalt
installation to replace. The user authorized removing the temporary jailbreak
filler that had exhausted USB storage before uploading the package.

KPM accepted version `1.0.207001` and completed the fresh installation. The
uploaded archive, installed application files and Library scriptlet matched
their expected checksums. Runtime and executable-storage preflight passed.
The app was then opened independently on the device; its running reader,
power helper and supervisor were preserved during installer cleanup. Detailed
interactive reading, color, sleep and exit behavior still require user
confirmation. No saved profile was inspected through an external database
connection, and no books were removed.

## Recorded PW3 installation

Beta.7 revision 1 was installed over SSH with Kobalt stopped. The complete prior
application, books and profile were backed up to
`/mnt/us/kobalt-before-beta7-pw3-dfed4afb.tar.gz` and the archive was verified.
The uploaded ZIP digest matched the local verified artifact. Installed package
files matched the deployment checksum manifest. Before/after hashes confirmed
that the books, profile and customized KUAL menu were unchanged; its
`--library /mnt/us/kobalt/books` argument was retained. Audit records are in
`/mnt/us/kobalt/logs/beta7-pw3-dfed4afb-*`.

KOReader's original processes were left running. Kobalt was left stopped, and
the installer's staging directory and temporary launch lock were removed.
Exit KOReader normally before opening Kobalt from KUAL. No device launch,
touch, page-turn, suspend or stock-UI recovery test was performed during installation.
The verified package results above were reused; the same binary was installed.

## Installing the PW3 ZIP

Close Kobalt and KOReader normally and back up the existing `kobalt` and
`extensions/kobalt` folders. Extract the ZIP onto the Kindle USB storage root,
merging its `kobalt` and `extensions` folders with the existing folders. Replace
matching application files; keep existing configuration, books and saved data.
If the KUAL menu has a customized `--library` argument, retain that customization
in the new menu. Eject normally and use the Kobalt entry in KUAL. See the
[PW3 notes](pw3-sdk.md) for the existing launch and firmware constraints.
