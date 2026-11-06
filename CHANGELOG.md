# Changelog

All notable changes to wayfire-plugins. Dates are local time.

Contributors: user (design & direction), Claude (plugins and documentation)

Each bullet starts with the file it touches.

## [2026-11-06]

### Added
- **LICENSE:** The MIT License, Wayfire's own text with the copyright line for this repo *(Claude, directed by user)*
- **toplevel-capture/src/toplevel-capture.cpp:** A Wayfire plugin implementing ext-foreign-toplevel-image-capture-source-v1, which Wayfire 0.11 lacks, so clients of ext-image-copy-capture-v1 (Noctalia's window switcher previews) can capture single windows. Each window's surfaces and server-side decoration are rendered into an off-screen buffer and copied into the client's shm buffer; while a session runs the window counts as visible, so captures stay live on other workspaces. Needs the ext-toplevel plugin *(Claude, directed by user)*
- **toplevel-capture/tests/capture-toplevels.c:** A test client that captures every window through the protocol into PPM files *(Claude, directed by user)*
- **meson.build, toplevel-capture/meson.build:** Builds the plugins against the installed Wayfire and wlroots, installing to lib/wayfire and share/wayfire/metadata under the prefix *(Claude, directed by user)*
- **README.md:** Building, installing and what toplevel-capture does and doesn't do *(Claude, directed by user)*

### Changed
- **meson.build:** Declares the MIT license and LICENSE file, which needs meson 1.1 or newer *(Claude, directed by user)*
- **README.md:** A License section, crediting the snapshot render code adapted from Wayfire *(Claude, directed by user)*
- **toplevel-capture/src/toplevel-capture.cpp:** render_snapshot() now notes that it's adapted from Wayfire's view_interface_t::take_snapshot(), with Wayfire's copyright and MIT License *(Claude, directed by user)*

