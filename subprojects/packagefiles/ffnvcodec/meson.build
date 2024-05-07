project('ffnvcodec', 'cpp', version : 'n12.2.72.0')

ffnvcodec_inc = include_directories('include')

ffnvcodec_headers = [
	'include/ffnvcodec/dynlink_cuda.h',
	'include/ffnvcodec/dynlink_cuviddec.h',
	'include/ffnvcodec/dynlink_loader.h',
	'include/ffnvcodec/dynlink_nvcuvid.h',
	'include/ffnvcodec/nvEncodeAPI.h'
]

install_headers(
	ffnvcodec_headers,
	subdir : 'ffnvcodec'
)

ffnvcodec_dep = declare_dependency(
	include_directories : ffnvcodec_inc
)
