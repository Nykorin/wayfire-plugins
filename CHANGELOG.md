# Changelog

All notable changes to wayfire-plugins. Dates are local time.

Contributors: user (design & direction), Claude (plugins and documentation)

Each bullet starts with the file it touches.

## [2026-11-08]

### Changed
- **ext-workspace-bridge/src/ext-workspace-bridge.cpp:** Rewritten on top of Wayfire's ext-workspace plugin (a352aa3b, PR #3092, MIT; not in a release yet), a per-output plugin, keeping what the bridge did differently so Noctalia's widget is unchanged: workspaces named 1, 2, 3 row by row, (row, column) coordinates, and activate as the only capability (upstream passes the state flags ACTIVE | HIDDEN, which offered removal). Tested in the Noctalia sandbox on a 3x2 grid: the bar, Noctalia's next-workspace and a live change to 2x2 behave as before *(Claude, directed by user)*
- **README.md:** The ext-workspace-bridge section describes the upstream-based plugin, how it differs from upstream's, and not to load both; the License section credits upstream's plugin *(Claude, directed by user)*

### Fixed
- **ext-workspace-bridge/src/ext-workspace-bridge.cpp:** The protocol manager is held for the plugin's whole life, so losing every output and getting one back doesn't create a second ext_workspace_v1 global, as upstream's plugin would (untested: the sandbox has one output that can't go away) *(Claude, directed by user)*

## [2026-11-06]

### Added
- **layer-popup-focus/src/layer-popup-focus.cpp:** A prototype plugin that gives keyboard focus back to grabbing popups of layer surfaces without keyboard focus (lxqt-panel's menus, unfocused since Wayfire 0.11's f7f5461b), limited to the processes in its `clients` option. It enters the popup's surface on the wlr_seat at map and hands focus back when the popup goes away. Compiles against Wayfire 0.11 / wlroots 0.20; never run *(Claude, directed by user)*
- **layer-popup-focus/meson.build, layer-popup-focus/metadata/layer-popup-focus.xml:** Its own meson project and option metadata, kept out of the shared build *(Claude, directed by user)*
- **README.md:** The layer-popup-focus section, marked as an untested prototype: how it works, how to build and test it, and what it doesn't cover *(Claude, directed by user)*
- **ext-workspace-bridge/:** The ext-workspace-bridge plugin (Wayfire's per-output workspace grids over ext_workspace_v1, for Noctalia's Workspace widget), moved here from system-update's dotfiles/wayfire/shared/plugins; built as a subdir of the shared build, with C++23 set for it alone *(Claude, directed by user)*
- **meson_options.txt, meson.build:** A boolean option per plugin (`-Dtoplevel-capture=false`, `-Dext-workspace-bridge=false`) to leave it out; toplevel-capture's own dependencies are only looked up when it's built *(Claude, directed by user)*
- **README.md:** The ext-workspace-bridge section and the build options *(Claude, directed by user)*
- **LICENSE:** The MIT License, Wayfire's own text with the copyright line for this repo *(Claude, directed by user)*
- **toplevel-capture/src/toplevel-capture.cpp:** A Wayfire plugin implementing ext-foreign-toplevel-image-capture-source-v1, which Wayfire 0.11 lacks, so clients of ext-image-copy-capture-v1 (Noctalia's window switcher previews) can capture single windows. Each window's surfaces and server-side decoration are rendered into an off-screen buffer and copied into the client's shm buffer; while a session runs the window counts as visible, so captures stay live on other workspaces. Needs the ext-toplevel plugin *(Claude, directed by user)*
- **toplevel-capture/tests/capture-toplevels.c:** A test client that captures every window through the protocol into PPM files *(Claude, directed by user)*
- **meson.build, toplevel-capture/meson.build:** Builds the plugins against the installed Wayfire and wlroots, installing to lib/wayfire and share/wayfire/metadata under the prefix *(Claude, directed by user)*
- **README.md:** Building, installing and what toplevel-capture does and doesn't do *(Claude, directed by user)*

### Changed
- **README.md:** Each plugin's section (ext-workspace-bridge, toplevel-capture, layer-popup-focus) is a collapsible details block with its heading as the summary, so the page opens on the plugin table and the build instructions. Each block keeps an anchor with the old heading's name, so the table's links still work *(Claude, directed by user)*
- **meson.build:** Declares the MIT license and LICENSE file, which needs meson 1.1 or newer *(Claude, directed by user)*
- **README.md:** A License section, crediting the snapshot render code adapted from Wayfire *(Claude, directed by user)*
- **toplevel-capture/src/toplevel-capture.cpp:** render_snapshot() now notes that it's adapted from Wayfire's view_interface_t::take_snapshot(), with Wayfire's copyright and MIT License *(Claude, directed by user)*

### Fixed
- **ext-workspace-bridge/src/ext-workspace-bridge.cpp:** A grid size change (vwidth/vheight edited while Wayfire runs) now reaches clients right away: Wayfire 0.11 emits workspace_grid_changed_signal only on the workspace set, not the output, so the plugin follows the output's current set and reconnects when it's swapped. Before, Noctalia kept the old workspace count until the next workspace switch *(Claude, directed by user)*
- **ext-workspace-bridge/src/ext-workspace-bridge.cpp:** The header no longer says the code was never compiled, and the VERIFY notes are gone: it builds without warnings against Wayfire 0.11 / wlroots 0.20 and has been in use *(Claude, directed by user)*

