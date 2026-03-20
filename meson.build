project(
    'synthetic-mouse',
    'c',
    default_options: [
        'buildtype=debug',
        'c_std=gnu11',
        'warning_level=3',
    ],
)

cc = meson.get_compiler('c')

libevdev_dep = dependency('libevdev', required: true)
threads_dep = dependency('threads')
math_dep = cc.find_library('m', required: true)

executable(
    'out',
    [
        'main.c',
        'parse_config.c',
    ],
    dependencies: [
        libevdev_dep,
        threads_dep,
        math_dep,
    ],
    install: false,
)
