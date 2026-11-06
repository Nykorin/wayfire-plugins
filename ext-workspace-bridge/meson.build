# ext_workspace_v1 (Wayfire's workspace grids, for Noctalia's Workspace widget)

shared_module('ext-workspace-bridge', 'src/ext-workspace-bridge.cpp',
  dependencies: [wayfire, wlroots, wayland_server],
  cpp_args: plugin_args + ['-DWAYFIRE_PLUGIN'],
  # Match the cpp_std Wayfire itself is built with (see its meson.build).
  override_options: ['cpp_std=c++23'],
  install: true,
  install_dir: plugin_dir)

install_data('metadata/ext-workspace-bridge.xml', install_dir: metadata_dir)
