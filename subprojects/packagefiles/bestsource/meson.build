project('BestSource', 'cpp',
    default_options: ['buildtype=release', 'b_ndebug=if-release', 'cpp_std=c++14'],
    meson_version: '>=0.48.0'
)

libs = []

sources = [
    'src/audiosource.cpp',
    'src/videosource.cpp',
    'src/SrcAttribCache.cpp',
    'src/BSRational.cpp',
]

deps = [
    dependency('jansson', version: '>= 2.7', required: true),
    dependency('libavcodec'),
    dependency('libavformat'),
    dependency('libavutil'),
]

bs_lib = static_library('bestsource', sources,
    dependencies: deps,
    gnu_symbol_visibility: 'hidden'
)

bestsource_dep = declare_dependency(link_with: bs_lib, include_directories: include_directories('src'))

