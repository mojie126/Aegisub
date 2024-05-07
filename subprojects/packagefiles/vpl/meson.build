project('vpl', 'cpp', version : '2.11.0')

usage_args = []

if get_option('default_library') == 'static'
	usage_args += '-DBUILD_SHARED_LIBS=OFF'
endif

libvpl_sources = [
	'libvpl/src/mfx_dispatcher_vpl.cpp',
	'libvpl/src/mfx_dispatcher_vpl_config.cpp',
	'libvpl/src/mfx_dispatcher_vpl_loader.cpp',
	'libvpl/src/mfx_dispatcher_vpl_log.cpp',
	'libvpl/src/mfx_dispatcher_vpl_lowlatency.cpp',
	'libvpl/src/mfx_dispatcher_vpl_msdk.cpp',
]

libvpl_headers = [
	'api/vpl/mfx.h',
	'api/vpl/mfxadapter.h',
	'api/vpl/mfxbrc.h',
	'api/vpl/mfxcamera.h',
	'api/vpl/mfxcommon.h',
	'api/vpl/mfxdefs.h',
	'api/vpl/mfxdispatcher.h',
	'api/vpl/mfxdispatcherprefixedfunctions.h',
	'api/vpl/mfxencodestats.h',
	'api/vpl/mfximplcaps.h',
	'api/vpl/mfxjpeg.h',
	'api/vpl/mfxmemory.h',
	'api/vpl/mfxmvc.h',
	'api/vpl/mfxpcp.h',
	'api/vpl/mfxsession.h',
	'api/vpl/mfxstructures.h',
	'api/vpl/mfxsurfacepool.h',
	'api/vpl/mfxvideo++.h',
	'api/vpl/mfxvideo.h',
	'api/vpl/mfxvp8.h'
]

libvpl_preview_headers = [
	'api/vpl/preview/legacy/mfxvideo++.h'
]

libvpl_inc = include_directories(['libvpl', 'api'])

vpl = library(
	'vpl',
	libvpl_sources,
	include_directories : libvpl_inc,
	cpp_args : usage_args,
	install : true
)

vpl_dep = declare_dependency(
	link_with : vpl,
	include_directories : libvpl_inc,
	compile_args : usage_args
)

install_headers(
	libvpl_headers,
	subdir : 'vpl'
)

install_headers(
	libvpl_preview_headers,
	subdir : 'vpl/preview'
)

pkg = import('pkgconfig')
pkg.generate(
	vpl,
	version : meson.project_version(),
	name : 'Intel(R) Video Processing Library',
	description : 'Accelerated video decode, encode, and frame processing capabilities on Intel(R) GPUs',
	filebase : 'vpl'
)
