project('CM4all Cash', 'cpp', version: '0.3',
  meson_version: '>= 1.0',
  default_options : [
    'cpp_std=c++20',
    'warning_level=3',

    'fmt:cpp_std=c++20',
  ],
)

compiler = meson.get_compiler('cpp')

debug = get_option('b_ndebug') != 'true'

conf = configuration_data()

common_flags = [
  '-D_GNU_SOURCE',
]

test_common_flags = [
  '-Wcast-align',
  '-Wcast-qual',
  '-Wdouble-promotion',
  '-Wfloat-equal',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-noreturn',
  '-Wredundant-decls',
  '-Wshadow',
  '-Wundef',
  '-Wunused',
  '-Wvla',
  '-Wwrite-strings',

  # clang specific warning options:
  '-Wunreachable-code-aggressive',
  '-Wused-but-marked-unused',
]

test_global_common_flags = [
  '-fvisibility=hidden',
]

add_global_link_arguments(
  compiler.get_supported_link_arguments(
    # make relocations read-only (hardening)
    '-Wl,-z,relro',

    # no lazy binding, please - not worth it for a daemon
    '-Wl,-z,now',
  ),
  language: 'cpp'
)

if not debug
  test_global_common_flags += [
    '-ffunction-sections',
    '-fdata-sections',
  ]

  add_global_link_arguments(
    compiler.get_supported_link_arguments(
      '-Wl,--gc-sections',
      '-Wl,--icf=all',
    ),
    language: 'cpp'
  )
endif

test_global_cxxflags = test_global_common_flags + [
]

test_cxxflags = test_common_flags + [
  '-fno-threadsafe-statics',
  '-fmerge-all-constants',

  '-Wcomma-subscript',
  '-Wextra-semi',
  '-Wmismatched-tags',
  '-Woverloaded-virtual',
  '-Wsign-promo',
  '-Wvolatile',
  '-Wvirtual-inheritance',

  '-Wno-missing-field-initializers',

  # a vtable without a dtor is just fine
  '-Wno-non-virtual-dtor',

  # clang specific warning options:
  '-Wcomma',
  '-Wheader-hygiene',
  '-Winconsistent-missing-destructor-override',
]

add_global_arguments(common_flags, language: 'cpp')
add_global_arguments(compiler.get_supported_arguments(test_global_cxxflags), language: 'cpp')
add_project_arguments(compiler.get_supported_arguments(test_cxxflags), language: 'cpp')

libsystemd = dependency('libsystemd', required: get_option('systemd'))

inc = include_directories('src', 'libcommon/src')

libcommon_require_uring = true

subdir('libcommon/src/util')
subdir('libcommon/src/lib/cap')
subdir('libcommon/src/lib/fmt')
subdir('libcommon/src/co')
subdir('libcommon/src/io')
subdir('libcommon/src/io/linux')
subdir('libcommon/src/io/uring')
subdir('libcommon/src/system')
subdir('libcommon/src/event')
subdir('libcommon/src/event/co')
subdir('libcommon/src/event/uring')

if libsystemd.found()
  subdir('libcommon/src/net')
  subdir('libcommon/src/event/systemd')
  libsystemd = event_systemd_dep
endif

conf.set('HAVE_LIBSYSTEMD', libsystemd.found())
conf.set('HAVE_LIBCAP', cap_dep.found())
configure_file(output: 'config.h', configuration: conf)

executable('cm4all-cash',
  'src/system/SetupProcess.cxx',
  'src/Main.cxx',
  'src/Config.cxx',
  'src/Cull.cxx',
  'src/DevCachefiles.cxx',
  'src/Walk.cxx',
  'src/Chdir.cxx',
  include_directories: inc,
  dependencies: [
    io_linux_dep,
    event_co_dep,
    event_uring_dep,
    util_dep,
    fmt_dep,
    cap_dep,
    libsystemd,
  ],
  install: true,
  install_dir: 'sbin',
)

install_data('cachefilesd.conf', install_dir: get_option('sysconfdir'))

if get_option('test')
  subdir('libcommon/src/time')
  subdir('test')
endif
