# Spice VD Agent for Windows

Community-maintained Windows guest agent for [SPICE](https://www.spice-space.org/).
This repository is a public mirror of the abandoned
[freedesktop.org spice/win32/vd_agent](https://gitlab.freedesktop.org/spice/win32/vd_agent)
project. Canonical downloads live on
[GitHub Releases](https://github.com/nefarius/vd_agent/releases).

The agent provides:

- Client mouse mode without grabbing the pointer
- Desktop resolution matching the client
- Clipboard sharing (text and images)
- File transfer into the guest
- A Windows service (`spice-agent`) that starts `vdagent.exe` in each session

## Status

Red Hat no longer maintains upstream SPICE. This fork keeps the Windows agent
building and shipping for current guests, especially Windows 11 VMs on Linux.

The tree already includes the multi-GPU mouse fix from
[`d7405ee`](https://gitlab.freedesktop.org/spice/win32/vd_agent/-/commit/d7405ee)
(`vdagent/desktop_layout.cpp`): when a real GPU is passed through alongside the
SPICE display device, the agent no longer loses mouse movement.

## License and provenance

The agent is **GPL-2.0-or-later**. See [COPYING](COPYING) and the copyright
headers in each source file. Original copyright remains with Red Hat, Inc. and
other upstream authors. This fork does not claim the Red Hat or SPICE
trademarks.

Pinned build-time submodules (do not bump casually):

| Submodule | Commit | Upstream |
|-----------|--------|----------|
| `spice-protocol` | `ce0c4211e6f16c66477934cc42e70fa0988ca7f0` | https://gitlab.freedesktop.org/spice/spice-protocol |
| `spice-common` | `05c0c26839e88e6d0cc5452f49c40e38543c8f97` | https://gitlab.freedesktop.org/spice/spice-common |

Submodule URLs use HTTPS. MSI upgrades keep the historical WiX `UpgradeCode`
(`7eb9b146-db04-42d7-a8ba-71fc8ced7eed`). Related products are removed after
`InstallInitialize` so files and the `spice-agent` service are installed
afterward. The x64 installer still only ships `vdagent.exe` and
`vdservice.exe` into `C:\Program Files\SPICE agent\bin`.

## Clone

```bash
git clone --recursive https://github.com/nefarius/vd_agent.git
cd vd_agent
```

If you already cloned without submodules:

```bash
git submodule update --init --recursive
```

The freedesktop GitLab remote is preserved as `upstream` after the mirror was
created. Fetch it with:

```bash
git fetch upstream
```

## Local build (MSYS2 UCRT64)

The Autotools + MinGW-w64 UCRT64 path is the supported way to produce the
installer. CMake + MSVC remains available for local development but does not
build an MSI.

### Prerequisites

- [MSYS2](https://www.msys2.org/)
- An **UCRT64** shell (`C:\msys64\ucrt64.exe`, or `MSYSTEM=UCRT64`)

From the UCRT64 shell, in the repository root:

```bash
bash msys2/install.sh
autoreconf -i
bash msys2/build.sh builducrt64
bash msys2/package.sh builducrt64
```

`install.sh` pulls `autotools`, `autoconf-archive`, the UCRT64 toolchain,
`msitools` (`wixl`), and ImageMagick (tests). PNG clipboard conversion uses
the Windows Imaging Component that ships with Windows Vista and later.

`build.sh` configures, compiles `vdagent.exe` / `vdservice.exe`, and runs
`test-png`, `test-log`, and `test-shell`. `package.sh` then invokes
`make msi` and writes:

```
builducrt64/spice-vdagent-x64-<version>.msi
```

Version strings come from `git describe` via
[`build-aux/git-version-gen`](build-aux/git-version-gen). Release tags must
look like `v0.11.0` (minor bumps) so Programs and Features shows the tag
exactly. Untagged builds add the commit count since the last tag (for example
`v0.11.0` plus 83 commits becomes `0.11.83`). Configure fails if that count
plus `--with-buildid` reaches 256, because that would collide with the next
micro version.

To sign a local build, sign the two executables **before** `package.sh`, then
sign the MSI.

### Optional MSVC build

```powershell
git submodule update --init --recursive
cmake -S . -B build64 -A x64
cmake --build build64 --config Release
cmake --build build64 --config Release --target check
```

## CI and releases

GitHub Actions (`.github/workflows/build.yml`) builds the x64 UCRT64 MSI on
`windows-2022`.

| Event | Signing | Publish |
|-------|---------|---------|
| Pull request / `master` push | Skipped | Workflow artifact `vdagent-win-x64` only |
| Tag `v*` | Required | Signed MSI + SHA-256, artifact mirror, GitHub Release |

Signing uses [SignRelay](https://github.com/nefarius/SignRelay) so the
certificate never lands on the runner. The flow matches
[DsHidMini](https://github.com/nefarius/DsHidMini):

1. Build and test unsigned binaries
2. On a `v*` tag, sign `vdagent.exe` and `vdservice.exe` in place
3. Package the MSI from those binaries
4. Sign the MSI
5. Verify Authenticode (`Get-AuthenticodeSignature` Status = `Valid`)
6. Write `<msi>.sha256`
7. Upload `vdagent-win-x64` and, on tags, notify
   [AppVeyorArtifactsReceiver](https://github.com/nefarius/AppVeyorArtifactsReceiver)
8. Attach the MSI and checksum to the GitHub Release

The SignRelay composite action is pinned to commit
`39ccbe0cef16a383237130380a5aef8db040d5d0`. The CLI needs **.NET 10** on the
runner (`actions/setup-dotnet` with `10.0.x`).

### Repository settings

Create these on `nefarius/vd_agent` (Settings → Secrets and variables):

| Name | Kind | Purpose |
|------|------|---------|
| `SIGN_RELAY_SERVER` | Variable | Relay base URL, for example `https://signrelay.api.nefarius.systems/` |
| `SIGN_RELAY_CI_TOKEN` | Secret | CI bearer token (`SignRelay__CiToken` on the server) |
| `WEBHOOK_URL` | Secret | AppVeyorArtifactsReceiver webhook |

Copy `SIGN_RELAY_CI_TOKEN` and `WEBHOOK_URL` from an already-working repo such
as DsHidMini. `SIGN_RELAY_SERVER` is already set as a repository variable.
Do not commit secret values.

The Windows SignRelay agent holds the code-signing certificate. Configure
subject/thumbprint and timestamp there, not in this repository.

### Publishing a release

1. Update [CHANGELOG.md](CHANGELOG.md)
2. Tag an annotated release and push it:

   ```bash
   git tag -a v0.11.0 -m "vdagent-win 0.11.0"
   git push origin v0.11.0
   ```

3. Confirm the **Build** workflow:
   - unsigned path is not used
   - both executables and the MSI verify as `Valid`
   - artifacts receiver accepted the webhook
   - the GitHub Release contains the MSI and `.sha256`
4. Install the MSI in a Windows 11 SPICE guest and run the checklist below

If a tagged build fails after signing started, fix the tree and move the tag
forward (or use a new minor version). Do not reuse a published MSI name with
different bytes.

To recover a failed release: delete the GitHub Release draft if any, push a
new tag, and keep the previous published tag immutable if users may have
downloaded it.

`appveyor.yml` is kept only for historical parity with the last upstream
UCRT64 MSI layout. GitHub Actions is the authoritative CI. Remove AppVeyor
once a signed Actions MSI has been smoke-tested.

## Windows 11 VM validation

Use a Windows 11 guest on Linux (QEMU/KVM + SPICE), with the QXL or
`qxl-wddm-dod` display device.

1. **Clean install** — run `spice-vdagent-x64-*.msi` as Administrator
2. **Service** — `spice-agent` is Running / Automatic; `vdagent.exe` is
   present in the user session
3. **SPICE connection** — reconnect virt-viewer / spicy; agent channel is up
4. **Clipboard** — text and a bitmap both ways
5. **File transfer** — drop a file from the client; it lands on the desktop
6. **Dynamic resolution** — resize the client window; the guest desktop
   follows when the WDDM QXL driver is in use
7. **Multi-GPU / passthrough mouse** — add a real GPU for passthrough, keep
   the SPICE display, confirm the pointer keeps moving (the `d7405ee` fix)
8. **Upgrade** — install over a previous Spice agent MSI; service comes back
9. **Uninstall** — remove the product; `spice-agent` is gone

## Optional CMake / Fedora notes

- Fedora cross-builds still work via [`.gitlab-ci.yml`](.gitlab-ci.yml) and
  [`mingw-spice-vdagent.spec.in`](mingw-spice-vdagent.spec.in); they are not
  used for GitHub Releases.
- x86 MSI builds are no longer produced by the maintained pipeline.
