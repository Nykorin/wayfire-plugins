# ext-foreign-toplevel-image-capture-source-v1 (window capture)

# wlroots' capture headers include these generated protocol headers
protocols = ['ext-image-capture-source', 'ext-image-copy-capture', 'ext-foreign-toplevel-list']
server_headers = []
foreach p : protocols
  server_headers += custom_target(p + '-protocol-h',
    input: protocols_dir / 'staging' / p / p + '-v1.xml',
    output: p + '-v1-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'])
endforeach

shared_module('toplevel-capture', 'src/toplevel-capture.cpp', server_headers,
  dependencies: [wayfire, wlroots, wayland_server, pixman, libdrm],
  cpp_args: plugin_args,
  install: true,
  install_dir: plugin_dir)

install_data('metadata/toplevel-capture.xml', install_dir: metadata_dir)

# Test client (not installed): captures every window through the protocol
client_files = []
foreach p : protocols
  xml = protocols_dir / 'staging' / p / p + '-v1.xml'
  client_files += custom_target(p + '-client-h', input: xml,
    output: p + '-v1-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
  client_files += custom_target(p + '-code', input: xml,
    output: p + '-v1-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
endforeach
executable('capture-toplevels', 'tests/capture-toplevels.c', client_files,
  dependencies: [wayland_client], c_args: ['-Wno-unused-parameter'], install: false)
