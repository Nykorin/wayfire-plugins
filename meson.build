project('wayfire-plugins', 'cpp', 'c',
  version: '0.1.0',
  license: 'MIT',
  license_files: 'LICENSE',
  meson_version: '>=1.1.0',
  default_options: ['cpp_std=c++17', 'warning_level=2', 'buildtype=debugoptimized'])

wayfire = dependency('wayfire', version: '>=0.11.0')
wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')

plugin_dir = get_option('libdir') / 'wayfire'
metadata_dir = get_option('datadir') / 'wayfire' / 'metadata'
plugin_args = ['-DWLR_USE_UNSTABLE', '-Wno-unused-parameter']

# Each plugin can be left out (-D<name>=false), along with what only it needs.
if get_option('toplevel-capture')
  wayland_client = dependency('wayland-client')
  pixman = dependency('pixman-1')
  libdrm = dependency('libdrm')
  wayland_protocols = dependency('wayland-protocols')
  wayland_scanner = find_program(
    dependency('wayland-scanner', native: true).get_variable(pkgconfig: 'wayland_scanner'))
  protocols_dir = wayland_protocols.get_variable(pkgconfig: 'pkgdatadir')
  subdir('toplevel-capture')
endif
if get_option('ext-workspace-bridge')
  subdir('ext-workspace-bridge')
endif
