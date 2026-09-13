unreleased
==========

v0.13.0
=======
- Fix a major upgrade wiping the installation: removing the previous MSI after
  InstallInitialize ran once costing had already decided the shared components
  needed no work, so the old package's uninstall deleted vdagent.exe,
  vdservice.exe and the spice-agent service, leaving only an Add/Remove
  Programs entry until the MSI was run a second time
- Record the binary file version in the MSI File table; wixl does not read the
  PE version resource, so Windows Installer had no version data to compare
- Migrate feature states on upgrade and allow a rebuild of the same version to
  replace an existing install

v0.12.0
=======
- Harden display, clipboard, and file-xfer error paths: recover more
  cleanly from display-setting failures, reject invalid clipboard images,
  and close file-transfer leaks
- Avoid a second PNG decode on clipboard conversion and skip extra display
  refresh work when the monitor layout has not changed

v0.11.0
=======
- Replace libpng/zlib with the Windows Imaging Component for PNG clipboard
  conversion. This removes the MinGW static-libpng link failure on Fedora
  (`__intrinsic_setjmpex`) and drops a third-party codec dependency.
- Fix mouse movement when multiple GPUs are present
- Add side and extra mouse button support
- Build and publish x64 MSI installers from GitHub Actions
- Bump the minor version on each release so Programs and Features shows the
  tag (0.11.0) instead of a packed micro or a commit count
- Give each MSI a new ProductCode so a newer build upgrades the installed
  package instead of reporting that the product is already installed
- Include commits since the last tag in the MSI product version so Windows
  upgrades an existing same-tag install
- Remove a related Spice agent install after InstallInitialize, then copy
  files and register spice-agent, so an upgrade does not leave an empty
  Program Files directory or a missing service

v0.10.0
=======
- Introduce turn_monitor_off method of WDDM interface
- Fix number of displays after reconnection ([rhbz#1477191])
- Avoid possible integer overflows from reading spice-protocol messages
- Fix possible buffer overflows while reading from registry
- Fix loss of mouse movement events
- file-xfer: only store completed files
- Some fixes in vcproj file to build with Visual Studio
- Do not use reserved chars for filenames ([rhbz#1520393])
- Fix saving BMP file format
- Various cleanups and code improvements

[rhbz#1477191]: https://bugzilla.redhat.com/show_bug.cgi?id=1477191
[rhbz#1520393]: https://bugzilla.redhat.com/show_bug.cgi?id=1520393

v0.9.0
======
- remove cximage dependency
- remove 'RHEV' reference from service name
- don't disconnect agent when client disconnects
- support file transfer when file already exists
- don't use LTO by default with mingw to work around a compiler bug
- leak fixes

v0.8.0
======
- add multi monitor/dynamic resolution support on Win8+
  when the qxl-wddm-dod driver is used
- disable drag and drop when the screen is locked
- dynamic resolution/monitor positioning improvements
- improve handling of Unicode filenames during drag and drop

v0.7.3
======
- file-xfer: make user desktop the target directory
- clipboard: Add VD_AGENT_CAP_MAX_CLIPBOARD support
- build spice-vdagent MSI installer
- Don't refresh displays config when updating (rhbz#1111144)
- file-xfer: Fix dragging of files with CJK characters in name
- fix building with Visual Studio
- various cleanups

v0.7.2
======
- File transfer support
- Convert line endings when needed
