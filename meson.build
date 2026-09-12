project(
  'partyline',
  'cpp',
  version: '0.1.1',
  license: 'Unlicense',
  meson_version: '>=0.56',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'prefix=/usr',
  ],
)

gtkmm = dependency('gtkmm-3.0')

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('APP_ID', 'org.gmgauthier.Partyline')
conf.set_quoted('DATADIR', get_option('prefix') / get_option('datadir') / 'partyline')
conf.set_quoted('SOURCE_ROOT', meson.project_source_root())

configure_file(
  output: 'config.hpp',
  configuration: conf,
)

inc = include_directories('.', 'src')

executable(
  'partyline',
  'src/main.cpp',
  'src/application.cpp',
  'src/paths.cpp',
  'src/main_window.cpp',
  'src/about_dialog.cpp',
  'src/connect_dialog.cpp',
  'src/join_dialog.cpp',
  'src/irc_session.cpp',
  'src/settings.cpp',
  'src/servers_dialog.cpp',
  include_directories: inc,
  dependencies: [gtkmm],
  install: true,
  install_dir: get_option('bindir'),
)

install_data(
  'data/partyline.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'data/partyline.ini',
  install_dir: get_option('datadir') / 'partyline',
)

install_data(
  'data/skin/lcos/lcos.css',
  install_dir: get_option('datadir') / 'partyline' / 'skin' / 'lcos',
)

install_data(
  'brand/icon-tile.svg',
  install_dir: get_option('datadir') / 'partyline' / 'brand',
)

install_data(
  'brand/icon-tile.svg',
  rename: 'partyline.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)
