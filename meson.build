project(
    'd3d12-video-decode',
    ['c'],
    default_options: [
        'c_std=c17',
        'b_ndebug=if-release',
        'warning_level=3',
    ]
)

cc = meson.get_compiler('c')

compiler_args = []
linker_args = []

add_project_arguments(cc.get_supported_arguments(compiler_args), language: 'c')
add_project_link_arguments(cc.get_supported_link_arguments(linker_args), language: 'c')

src = [
    'main.c'
]

deps = [
    dependency('DirectX-Headers', include_type: 'system'),
    cc.find_library('d3d12'),
    cc.find_library('dxcore'),
    cc.find_library('dxgi'),
    cc.find_library('dxguid'),
]

executable(
    'd3d12-video-decode',
    src,
    dependencies: deps,
)
