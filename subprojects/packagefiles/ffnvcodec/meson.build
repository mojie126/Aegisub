project('ffnvcodec', 'cpp', version : 'n12.2.72.0')

ffnvcodec_inc = include_directories('include')

headers = files([
					'include/ffnvcodec/dynlink_cuda.h',
					'include/ffnvcodec/dynlink_cuviddec.h',
					'include/ffnvcodec/dynlink_loader.h',
					'include/ffnvcodec/dynlink_nvcuvid.h',
					'include/ffnvcodec/nvEncodeAPI.h',
				])

ffnvcodec_dep = declare_dependency(include_directories : ffnvcodec_inc)

install_headers(headers)

pkg = import('pkgconfig')

pkg.generate(
	name : 'ffnvcodec',
	description : 'FFmpeg version of Nvidia Codec SDK headers'
)
