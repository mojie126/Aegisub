project('ffms2', 'cpp', version : '5.1.1')

deps = []

opts = [
	'gpl=enabled', 'programs=disabled', 'tests=disabled',
	'libx264=enabled', 'libdav1d=enabled', 'ffnvcodec=enabled', 'cuda=enabled', 'cuvid=enabled'
]

deps += dependency('zlib')
deps += dependency('x264', default_options : ['thread=false'])
deps += dependency('libavformat', version : '>=60', default_options : opts)
deps += dependency('libavcodec', version : '>=60', default_options : opts)
deps += dependency('libswscale', default_options : opts)
deps += dependency('libavutil', default_options : opts)
deps += dependency('libavfilter', default_options : opts)
deps += dependency('libswresample', default_options : opts)

args = ['-D_FILE_OFFSET_BITNS=64', '-DFFMS_EXPORTS', '-D__STDC_CONSTANT_MACROS']
usage_args = []

if get_option('default_library') == 'static'
	usage_args += '-DFFMS_STATIC'
endif

ffms2_sources = [
	'src/core/audiosource.cpp',
	'src/core/ffms.cpp',
	'src/core/filehandle.cpp',
	'src/core/indexing.cpp',
	'src/core/track.cpp',
	'src/core/utils.cpp',
	'src/core/videosource.cpp',
	'src/core/videoutils.cpp',
	'src/core/zipfile.cpp',
]

deps_inc = include_directories('include')

ffms2 = library(
	'ffms2',
	ffms2_sources,
	include_directories : deps_inc,
	dependencies : deps,
	cpp_args : args + usage_args
)

ffms2_dep = declare_dependency(
	link_with : ffms2,
	include_directories : deps_inc,
	compile_args : usage_args
)
