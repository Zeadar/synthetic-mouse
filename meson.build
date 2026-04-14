project(
    'synthetic-mouse',
    'c',
    meson_version: '>=1.3.0',
    default_options: [
        'c_std=gnu11',
        'warning_level=1',
    ],
)

cc = meson.get_compiler('c')

libevdev_dep = dependency('libevdev', required: true)
threads_dep = dependency('threads')
math_dep = cc.find_library('m', required: true)

config_install_dir = join_paths(get_option('sysconfdir'), 'synthetic-mouse')
systemd_unit_dir = join_paths(get_option('libdir'), 'systemd', 'system')
openrc_service_dir = join_paths(get_option('sysconfdir'), 'init.d')
installed_binary = join_paths(get_option('prefix'), get_option('bindir'),
                              'synthetic-mouse')

executable(
    'synthetic-mouse',
    [
        'src/main.c',
        'src/parse_config.c',
    ],
    dependencies: [
        libevdev_dep,
        threads_dep,
        math_dep,
    ],
    install: true,
    install_tag: 'runtime',
)

if get_option('install_config_files')
    install_data(
        'synthetic.conf',
        install_dir: config_install_dir,
        rename: 'synthetic.conf',
        follow_symlinks: true,
        install_tag: 'config',
    )
    install_data(
        'synthetic_controller.conf',
        install_dir: config_install_dir,
        rename: 'synthetic_controller.conf.sample',
        follow_symlinks: true,
        install_tag: 'config',
    )
    install_data(
        'synthetic_keyboard.conf',
        install_dir: config_install_dir,
        rename: 'synthetic_keyboard.conf.sample',
        follow_symlinks: true,
        install_tag: 'config',
    )
endif

service_conf = configuration_data()
service_conf.set('binary_path', installed_binary)

if get_option('install_systemd_unit')
    configure_file(
        input: 'packaging/systemd/synthetic-mouse.service.in',
        output: 'synthetic-mouse.service',
        configuration: service_conf,
        install: true,
        install_dir: systemd_unit_dir,
        install_tag: 'systemd',
    )
endif

if get_option('install_openrc_service')
    openrc_script = configure_file(
        input: 'packaging/openrc/synthetic-mouse.initd.in',
        output: 'synthetic-mouse.openrc',
        configuration: service_conf,
    )
    install_data(
        openrc_script,
        install_dir: openrc_service_dir,
        rename: 'synthetic-mouse',
        install_mode: 'rwxr-xr-x',
        install_tag: 'openrc',
    )
endif
