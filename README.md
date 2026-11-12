# wayfire-plugins

Out-of-tree plugins for [Wayfire](https://wayfire.org), built against the installed Wayfire (0.11) and wlroots (0.20) headers.

| Plugin | What it does |
|---|---|
| [`toplevel-capture`](#toplevel-capture) | Window capture through `ext-foreign-toplevel-image-capture-source-v1`, for e.g. Noctalia's window switcher previews |
| [`ext-workspace-bridge`](#ext-workspace-bridge) | Wayfire's workspace grids through `ext_workspace_v1`, for Noctalia's Workspace widget |
| [`layer-popup-focus`](#layer-popup-focus-prototype-untested) | **Prototype, untested.** Keyboard focus for lxqt-panel's menus again. Built on its own, not by the shared build |

<details>
<summary><b><a id="toplevel-capture"></a>toplevel-capture</b></summary>

Wayfire 0.11 sets up `ext-image-copy-capture-v1` with one capture source per output, but has no way to capture a single window. This plugin adds the `ext-foreign-toplevel-image-capture-source-v1` global, so clients can ask for a capture source for any window from `ext-foreign-toplevel-list-v1`. Noctalia's window switcher uses it for its previews.

- **Needs** the `ext-toplevel` plugin, which provides the window list and stores each window's view in its handle.
- **What's captured:** the window's surfaces and Wayfire's server-side decoration, at the output's scale. Transformers (wobbly, cube, alpha and so on) are left out, and so are windows covering it.
- **Live updates:** while a capture session runs, the window counts as visible, so its app keeps drawing frames even on another workspace. A new frame is sent when the window's content changes.
- **Buffers:** shared memory only (ARGB8888 / XRGB8888). wlroots 0.20 keeps the renderer's render formats private, so no dmabuf formats are advertised.
- **Not unloadable:** wlroots has no way to remove the global, so the plugin stays loaded until Wayfire restarts.
- Captures may stop updating for a window that moves to another output mid-session (untested).

`tests/capture-toplevels.c` (built as `build/toplevel-capture/capture-toplevels`, not installed) captures every window through the protocol and writes `OUTDIR/WINDOW-FRAME-APPID.ppm`:

```sh
build/toplevel-capture/capture-toplevels /tmp/caps 2   # 2 frames per window; the second waits for new content
```

</details>

<details>
<summary><b><a id="ext-workspace-bridge"></a>ext-workspace-bridge</b></summary>

Wayfire 0.11 doesn't offer `ext_workspace_v1`, which Noctalia's Workspace widget reads. This plugin offers it through wlroots' implementation (`wlr/types/wlr_ext_workspace_v1.h`).

It's based on Wayfire's own `ext-workspace` plugin ([PR #3092](https://github.com/WayfireWM/wayfire/pull/3092), merged after 0.11, so not in a release yet), changed so Noctalia shows the grid the way Wayfire switches through it:

- **Model:** one workspace group per output, one workspace per cell of that output's workspace grid. Activating a workspace goes through Wayfire's normal switching, with vswitch's animation.
- **Names:** `1`, `2`, `3`… row by row. Upstream names them `<output>:workspace-X-Y`, and Noctalia labels every one "W".
- **Coordinates:** (row, column). Noctalia sorts workspaces by them, so upstream's (column, row) puts its dots and its next/previous in column order.
- **Capabilities:** activate only. Upstream passes the state flags ACTIVE | HIDDEN as capabilities, which offers workspace removal it doesn't do.
- **One global:** the protocol manager lives as long as the plugin. Upstream frees it with the last output, and the next output then creates a second `ext_workspace_v1` global.
- **Grid changes** (`vwidth`/`vheight` edited while Wayfire runs) and workspace sets swapped on an output rebuild that output's workspaces straight away.
- Don't load it together with upstream's `ext-workspace` (Wayfire 0.12 and later): each offers its own `ext_workspace_v1` global.
- Built with C++23, the standard Wayfire itself is built with; the other plugins use C++17.

</details>

<details>
<summary><b><a id="layer-popup-focus-prototype-untested"></a>layer-popup-focus (prototype, untested)</b></summary>

> **Prototype.** It compiles without warnings against Wayfire 0.11 / wlroots 0.20, but it has never been loaded into Wayfire or run, so it may not work at all. It isn't part of the shared build: no `meson_options.txt` entry, and the top-level `meson.build` doesn't include it.

Since Wayfire 0.11 (commit f7f5461b, for wayfire#3025), a popup of a layer surface whose `keyboard_interactivity` is `none` gets no keyboard focus, as the layer-shell protocol asks, even when the popup grabs input. lxqt-panel's menus are such popups, so Esc and the arrow keys do nothing in its right-click menu, and typing doesn't reach Fancy Menu's search (lxqt/lxqt-panel#2504). The plugin is meant to replace a patched Wayfire build that reverts f7f5461b, and to limit the old behaviour to the clients you list.

- **How:** Wayfire emits `view_mapped_signal` for a popup just before its own focus check. The plugin acts on a popup that grabs input, whose topmost parent is a desktop-environment view (a layer surface) that won't take keyboard focus, and whose process is listed in `clients`. It enters the popup's surface on the wlr_seat directly; while the grab lasts, wlroots ignores Wayfire's own keyboard focus changes.
- **Closing:** Wayfire's focus never moved to the popup, so the plugin hands keyboard focus back when the popup goes away: to the parent menu for a submenu, otherwise to the view Wayfire has focused.
- **Option:** `[layer-popup-focus] clients`, space-separated process names (as in `/proc/PID/comm`), default `lxqt-panel`. Empty applies it to every client, which brings back #3025's focus-stealing popovers too.
- **Known gaps:**
  - Wayfire's own idea of the focused view doesn't change while a menu has the keyboard, so anything that follows Wayfire's focus (focus-based window rules, the active window's decoration) still shows the window behind.
  - A menu opened by a shortcut (Super through `lxqt-qdbus openmenu`) still needs the panel to have had some input first: Qt won't open a grabbing popup otherwise, with or without this plugin.

Build it on its own, from the repo root:

```sh
meson setup layer-popup-focus/build layer-popup-focus --prefix /usr
ninja -C layer-popup-focus/build
sudo ninja -C layer-popup-focus/build install    # sudo ninja -C layer-popup-focus/build uninstall to remove
```

Then add `layer-popup-focus` to `plugins =` in `wayfire.ini`. As with the other plugins, rebuild after a Wayfire or wlroots update.

**Testing it** (not done yet): with a stock Wayfire 0.11, install to a test prefix (`meson setup layer-popup-focus/build-test layer-popup-focus --prefix /some/dir`) and run `desktop-sandbox.sh --desktop lxqt --prefix /some/dir`, after adding the plugin to the sandbox's `wayfire.ini`. Then check:

- Right-click the panel and press Esc: the menu closes.
- Open Fancy Menu by clicking it and type: the text reaches the search box.
- Click another app, press Super and type: the text reaches Fancy Menu's search.
- After each menu closes, typing goes to the focused window again.
- A popup from a client not listed in `clients` still doesn't take focus.

The nested test scripts in the wayfire-patched copy's `local-patches/tools/bin/` (`test.sh`, `test-fancymenu.sh`, `test-super.sh`) automate the first three checks against a Wayfire prefix.

</details>

## Building and installing

```sh
meson setup build --prefix /usr
ninja -C build
sudo ninja -C build install     # sudo ninja -C build uninstall to remove
```

Every plugin is built by default; leave one out with `-D<name>=false` at `meson setup` (e.g. `-Dtoplevel-capture=false` builds only ext-workspace-bridge, without toplevel-capture's extra dependencies). system-install's sources stage (`system-install.sh`) builds ext-workspace-bridge for Wayfire + Noctalia this way. The exception is [layer-popup-focus](#layer-popup-focus-prototype-untested), a prototype with its own build.

The plugins go to `/usr/lib/wayfire` and their metadata to `/usr/share/wayfire/metadata`. Pacman doesn't track these files, so after a Wayfire or wlroots update, rebuild and reinstall: a plugin built against the old version may fail to load, and Wayfire only notes that in its log. Then add the plugin's name to `plugins =` in `wayfire.ini`.

To try a build without installing it, install to a prefix (`meson setup build-test --prefix /some/dir`) and run `desktop-sandbox.sh --prefix /some/dir`, after adding the plugin to the sandbox's own `wayfire.ini`.

## License

MIT, the same license as Wayfire; see [LICENSE](LICENSE). `toplevel-capture`'s off-screen render is adapted from Wayfire's `view_interface_t::take_snapshot()` (Copyright (c) 2018 Iliya Bozhinov, MIT), as noted in the source. `ext-workspace-bridge` is based on Wayfire's `ext-workspace` plugin (commit `a352aa3b`, MIT), as noted in its source.
