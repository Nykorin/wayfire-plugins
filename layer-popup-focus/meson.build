# layer-popup-focus (keyboard focus for lxqt-panel's menus). A prototype: its own
# project, not part of the top-level build. Build it from the repo root with
#   meson setup layer-popup-focus/build layer-popup-focus

project('layer-popup-focus', 'cpp',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=1.1.0',
  default_options: ['cpp_std=c++17', 'warning_level=2', 'buildtype=debugoptimized'])

wayfire = dependency('wayfire', version: '>=0.11.0')
wlroots = dependency('wlroots-0.20')
wayland_server = dependency('wayland-server')

shared_module('layer-popup-focus', 'src/layer-popup-focus.cpp',
  dependencies: [wayfire, wlroots, wayland_server],
  cpp_args: ['-DWLR_USE_UNSTABLE', '-DWAYFIRE_PLUGIN', '-Wno-unused-parameter'],
  install: true,
  install_dir: get_option('libdir') / 'wayfire')

install_data('metadata/layer-popup-focus.xml',
  install_dir: get_option('datadir') / 'wayfire' / 'metadata')
