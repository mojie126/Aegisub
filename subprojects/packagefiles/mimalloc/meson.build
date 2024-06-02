project('libmimalloc', 'c', version : '2.1.7')
mimalloc_static_dir = include_directories('include', '.')
mimalloc_static_sys = include_directories(is_system : true)
mimalloc_static_inc = [mimalloc_static_dir, mimalloc_static_sys]
mimalloc_static_src = files(
	'src/alloc.c',
	'src/alloc-aligned.c',
	'src/alloc-posix.c',
	'src/arena.c',
	'src/bitmap.c',
	'src/heap.c',
	'src/init.c',
	'src/libc.c',
	'src/options.c',
	'src/os.c',
	'src/page.c',
	'src/random.c',
	'src/segment.c',
	'src/segment-map.c',
	'src/stats.c',
	'src/prim/prim.c'
)
mimalloc_static = static_library(
	'mimalloc_static',
	mimalloc_static_src,
	build_by_default : true,
	link_args : [],
	link_with : [],
	include_directories : mimalloc_static_inc,
	install : true,
	override_options : ['cpp_std=c++17'],
	objects : [],
	install_dir : 'lib/mimalloc-2.1',
	cpp_args : [
		'/DWIN32',
		'/D_WINDOWS',
		'/GR',
		'/EHsc',
		'/Ob2',
		'/DNDEBUG',
		'-MD',
		'/Zc:__cplusplus',
		'-DMI_STATIC_LIB'
	],
	c_args : [
		'/DWIN32',
		'/D_WINDOWS',
		'/GR',
		'/EHsc',
		'/Ob2',
		'/DNDEBUG',
		'-MD',
		'/Zc:__cplusplus',
		'-DMI_STATIC_LIB'
	],
	pic : false
)
mimalloc_static_dep = declare_dependency(
	link_args : [],
	link_with : mimalloc_static,
	compile_args : [],
	include_directories : mimalloc_static_inc
)
