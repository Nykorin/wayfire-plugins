# wayfire-plugins

Out-of-tree plugins for [Wayfire](https://wayfire.org), built against the installed Wayfire (0.11) and wlroots (0.20) headers.

| Plugin | What it does |
|---|---|
| [`toplevel-capture`](#toplevel-capture) | Window capture through `ext-foreign-toplevel-image-capture-source-v1`, for e.g. Noctalia's window switcher previews |
| [`ext-workspace-bridge`](#ext-workspace-bridge) | Wayfire's workspace grids through `ext_workspace_v1`, for Noctalia's Workspace widget |

## Building and installing

```sh
meson setup build --prefix /usr
ninja -C build
sudo ninja -C build install     # sudo ninja -C build uninstall to remove
```

Every plugin is built by default; leave one out with `-D<name>=false` at `meson setup` (e.g. `-Dtoplevel-capture=false` builds only ext-workspace-bridge, without toplevel-capture's extra dependencies). system-update's `build-sources.sh` builds ext-workspace-bridge for Wayfire + Noctalia this way.

The plugins go to `/usr/lib/wayfire` and their metadata to `/usr/share/wayfire/metadata`. Pacman doesn't track these files, so after a Wayfire or wlroots update, rebuild and reinstall: a plugin built against the old version may fail to load, and Wayfire only notes that in its log. Then add the plugin's name to `plugins =` in `wayfire.ini`.

To try a build without installing it, install to a prefix (`meson setup build-test --prefix /some/dir`) and run `desktop-sandbox.sh --prefix /some/dir`, after adding the plugin to the sandbox's own `wayfire.ini`.

## ext-workspace-bridge

Wayfire 0.11 doesn't offer `ext_workspace_v1`, which Noctalia's Workspace widget reads. This plugin offers it through wlroots' implementation (`wlr/types/wlr_ext_workspace_v1.h`).

- **Model:** one workspace group per output, one workspace per cell of that output's workspace grid.
- **Staying in step:** every Wayfire signal it listens to just reconciles the protocol state against Wayfire's actual state, so a signal arriving at an unexpected time doesn't leave it out of date.
- Built with C++23, the standard Wayfire itself is built with; the other plugins use C++17.

## toplevel-capture

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

## License

MIT, the same license as Wayfire; see [LICENSE](LICENSE). `toplevel-capture`'s off-screen render is adapted from Wayfire's `view_interface_t::take_snapshot()` (Copyright (c) 2018 Iliya Bozhinov, MIT), as noted in the source.
