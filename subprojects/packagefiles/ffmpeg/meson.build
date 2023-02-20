# Copyright (c) 2018 Mathieu Duponchelle <mathieu@centricular.com>
#
# This file is part of the FFmpeg Meson build
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.

project('FFmpeg', 'c',
  meson_version: '>= 0.59',
  license: 'LGPL2.1+',
  default_options: [ 'buildtype=debugoptimized', 'c_std=c99', 'cpp_std=c++11'],
  version: '4.4.3',
)

# HOWTO:
#
# Get meson >= 0.59 from pip or your distro's package manager then:
#
# meson build && ninja -C build

# Overview of the configure process:
#
# This port is relatively faithful to the original configure script:
# We first gather system information (compiler checks, dependencies, ..),
# then pass this information through a temporary configure file to a
# script named "depresolver.py". This script implements the logic implemented
# in the configure script by the "check_deps" function, the dependency
# graph lives in a python file named "depgraph.py".
#
# That graph is made up of a set of nodes, each optionally containing
# information about the components it selects, depends on or conflicts
# with.
#
# The depresolver script returns two things:
#
# * A set of key-value pairs, indicating for each component whether
# it was enabled or not.
# * A second set of key-value pairs, associating to each component
# the set of enabled components it depended upon.
#
# The first set is used to determine which sources to compile in,
# using the dictionaries present in the relevant meson.build definitions
# in the subdirectories (eg. libavutil/meson.build)
#
# The second set is used to compute the external dependencies passed
# to the various build targets, when a component is compiled into library
# we check whether a @0@_flattened_deps.format(component) variable was
# set, and add it to the set of dependencies for that library.
#
# Various other python scripts have been implemented to reproduce other
# bits of logic in the configure script, eg. find_things.py ...
# TODO:
#
# * Assembler extensions (mmx etc..)
# * Porting the FATE test suite
# * Continue going over the configure script and importing various
#   bits of logic

common_incs = [include_directories('.')]

python3 = import('python').find_installation()

cc = meson.get_compiler('c')

languages_map = {}

if cc.get_id() == 'msvc'
  # Ignore several spurious warnings for things FFmpeg does very commonly
  # If a warning is completely useless and spammy, use '/wdXXXX' to suppress it
  # If a warning is harmless but hard to fix, use '/woXXXX' so it's shown once
  # NOTE: Only add warnings here if you are sure they're spurious or they're
  # for a pattern that ffmpeg does not care about
  add_project_arguments(
      '/wo4005', # macro redefinition (this likely need to be fixed)
      '/wd4018', # implicit signed/unsigned conversion
      '/wd4146', # unary minus on unsigned (beware INT_MIN)
      '/wd4244', # lossy type conversion (e.g. double -> int)
      '/wd4305', # truncating type conversion (e.g. double -> float)
      '/wd4554', # operator precedence
      '/wd4114', # same type qualifier used more than once
      '/wd4028', # formal parameter {n} different from declaration
      '/wd4090', # 'function': different 'const' qualifiers
      '/wd4133', # 'type' : incompatible types - from 'type1' to 'type2'
      cc.get_supported_arguments(['/utf-8']), # set the input encoding to utf-8
      language : 'c')
endif

project_c_args = [
  '-D_ISOC99_SOURCE',
  '-D_GNU_SOURCE',
  '-D_LARGEFILE_SOURCE',
  '-DPIC',
  '-Wno-parentheses',
  '-Wno-pointer-sign',
  '-Wno-switch',
  '-Wno-format-truncation',
  '-Wno-deprecated-declarations',
  '-Wno-unused-function',
  '-Wno-maybe-uninitialized',
  '-Wno-discarded-qualifiers',
  '-Wno-unused-variable',
  '-Wno-bool-operation',
  '-Wno-incompatible-pointer-types',
  '-Wno-address',
]

# Work around meson subprojects ignoring default_options
# https://github.com/mesonbuild/meson/issues/1889#issuecomment-1275991958
if meson.is_subproject() and meson.version().version_compare('< 0.63')
  project_c_args += ['-std=c99']
endif

project_c_args = cc.get_supported_arguments(project_c_args)

thread_dep = dependency('threads')

common_deps = [thread_dep]

if host_machine.system() == 'windows'
  common_deps += [cc.find_library('ws2_32')]
endif

conf = configuration_data()

document_list = [
  'doc',
  'htmlpages',
  'manpages',
  'podpages',
  'txtpages',
]

example_list = [
  'avio_dir_cmd_example',
  'avio_reading_example',
  'decode_audio_example',
  'decode_video_example',
  'demuxing_decoding_example',
  'encode_audio_example',
  'encode_video_example',
  'extract_mvs_example',
  'filter_audio_example',
  'filtering_audio_example',
  'filtering_video_example',
  'http_multiclient_example',
  'hw_decode_example',
  'metadata_example',
  'muxing_example',
  'qsvdec_example',
  'remuxing_example',
  'resampling_audio_example',
  'scaling_video_example',
  'transcode_aac_example',
  'transcoding_example',
  'vaapi_encode_example',
  'vaapi_transcode_example',
]

arch_ext_list_arm = [
  'armv5te',
  'armv6',
  'armv6t2',
  'armv8',
  'neon',
  'vfp',
  'vfpv3',
  'setend',
]

arch_ext_list_ppc = [
  'altivec',
  'dcbzl',
  'ldbrx',
  'power8',
  'ppc4xx',
  'vsx',
]

arch_ext_list_x86_simd = [
  'aesni',
  'amd3dnow',
  'amd3dnowext',
  'avx',
  'avx2',
  'avx512',
  'fma3',
  'fma4',
  'mmx',
  'mmxext',
  'sse',
  'sse2',
  'sse3',
  'sse4',
  'sse42',
  'ssse3',
  'xop',
]

arch_ext_list_x86 = arch_ext_list_x86_simd + [
  'cpunop',
  'i686',
]

arch_ext_list_mips = [
  'mipsfpu',
  'mips32r2',
  'mips32r5',
  'mips64r2',
  'mips32r6',
  'mips64r6',
  'mipsdsp',
  'mipsdspr2',
  'msa',
]

arch_ext_list_loongson = [
  'loongson2',
  'loongson3',
  'mmi',
]

arch_ext_list = arch_ext_list_arm + arch_ext_list_ppc + arch_ext_list_x86 + arch_ext_list_mips + arch_ext_list_loongson

arch_features = [
  'aligned_stack',
  'fast_64bit',
  'fast_clz',
  'fast_cmov',
  'local_aligned',
  'simd_align_16',
  'simd_align_32',
  'simd_align_64',
]

builtin_list = [
  'atomic_cas_ptr',
  'machine_rw_barrier',
  'MemoryBarrier',
  'mm_empty',
  'rdtsc',
  'sem_timedwait',
  'sync_val_compare_and_swap',
]

complex_funcs = [
  'cabs',
  'cexp',
]

have_list_cmdline = [
  'inline_asm',
  'symver',
  'x86asm',
]

have_list_pub = [
  'bigendian',
  'fast_unaligned',
]

headers_list = [
  'arpa_inet_h',
  'asm_types_h',
  'cdio_paranoia_h',
  'cdio_paranoia_paranoia_h',
  'cuda_h',
  'dispatch_dispatch_h',
  'dev_bktr_ioctl_bt848_h',
  'dev_bktr_ioctl_meteor_h',
  'dev_ic_bt8xx_h',
  'dev_video_bktr_ioctl_bt848_h',
  'dev_video_meteor_ioctl_meteor_h',
  'direct_h',
  'dirent_h',
  'dxgidebug_h',
  'dxva_h',
  'ES2_gl_h',
  'gsm_h',
  'io_h',
  'linux_perf_event_h',
  'machine_ioctl_bt848_h',
  'machine_ioctl_meteor_h',
  'malloc_h',
  'opencv2_core_core_c_h',
  'OpenGL_gl3_h',
  'poll_h',
  'sys_param_h',
  'sys_resource_h',
  'sys_select_h',
  'sys_soundcard_h',
  'sys_time_h',
  'sys_un_h',
  'sys_videoio_h',
  'termios_h',
  'udplite_h',
  'unistd_h',
  'valgrind_valgrind_h',
  'windows_h',
  'winsock2_h',
]

intrinsics_list = [
  'intrinsics_neon',
]

math_funcs = [
  'atanf',
  'atan2f',
  'cbrt',
  'cbrtf',
  'copysign',
  'cosf',
  'erf',
  'exp2',
  'exp2f',
  'expf',
  'hypot',
  'isfinite',
  'isinf',
  'isnan',
  'ldexpf',
  'llrint',
  'llrintf',
  'log2',
  'log2f',
  'log10f',
  'lrint',
  'lrintf',
  'powf',
  'rint',
  'round',
  'roundf',
  'sinf',
  'trunc',
  'truncf',
]

system_features = [
  'dos_paths',
  'libc_msvcrt',
  'MMAL_PARAMETER_VIDEO_MAX_NUM_CALLBACKS',
  'section_data_rel_ro',
  'threads',
  'uwp',
  'winrt',
]

system_funcs = [
  'access',
  'aligned_malloc',
  'arc4random',
  'clock_gettime',
  'closesocket',
  'CommandLineToArgvW',
  'fcntl',
  'getaddrinfo',
  'gethrtime',
  'getopt',
  'GetProcessAffinityMask',
  'GetProcessMemoryInfo',
  'GetProcessTimes',
  'getrusage',
  'GetSystemTimeAsFileTime',
  'gettimeofday',
  'glob',
  'glXGetProcAddress',
  'gmtime_r',
  'inet_aton',
  'isatty',
  'kbhit',
  'localtime_r',
  'lstat',
  'lzo1x_999_compress',
  'mach_absolute_time',
  'MapViewOfFile',
  'memalign',
  'mkstemp',
  'mmap',
  'mprotect',
  'nanosleep',
  'PeekNamedPipe',
  'posix_memalign',
  'pthread_cancel',
  'sched_getaffinity',
  'SecItemImport',
  'SetConsoleTextAttribute',
  'SetConsoleCtrlHandler',
  'setmode',
  'setrlimit',
  'Sleep',
  'strerror_r',
  'sysconf',
  'sysctl',
  'usleep',
  'UTGetOSTypeFromString',
  'VirtualAlloc',
  'wglGetProcAddress',
]

system_libraries = [
  'bcrypt',
  'vaapi_drm',
  'vaapi_x11',
  'vdpau_x11',
]

threads_list = [
  'pthreads',
  'os2threads',
  'w32threads',
]

toolchain_features = [
  'as_arch_directive',
  'as_dn_directive',
  'as_fpu_directive',
  'as_func',
  'as_object_arch',
  'asm_mod_q',
  'blocks_extension',
  'ebp_available',
  'ebx_available',
  'gnu_as',
  'gnu_windres',
  'ibm_asm',
  'inline_asm_direct_symbol_refs',
  'inline_asm_labels',
  'inline_asm_nonlocal_labels',
  'pragma_deprecated',
  'rsync_contimeout',
  'symver_asm_label',
  'symver_gnu_asm',
  'vfp_args',
  'xform_asm',
  'xmm_clobbers',
]

types_list = [
  'kCMVideoCodecType_HEVC',
  'socklen_t',
  'struct_addrinfo',
  'struct_group_source_req',
  'struct_ip_mreq_source',
  'struct_ipv6_mreq',
  'struct_msghdr_msg_flags',
  'struct_pollfd',
  'struct_rusage_ru_maxrss',
  'struct_sctp_event_subscribe',
  'struct_sockaddr_in6',
  'struct_sockaddr_sa_len',
  'struct_sockaddr_storage',
  'struct_stat_st_mtim_tv_nsec',
  'struct_v4l2_frmivalenum_discrete',
]

conf.set('runtime_cpudetect', 1)
asm = get_option('asm')
inline_asm = get_option('inline_asm')

conf.set10('asm', asm.allowed())

if inline_asm.allowed()
  conf.set10('inline_asm', 
    cc.compiles(
      'void foo(void){ __asm__ volatile(@0@); }'.format('"" ::'),
      name: 'inline_asm'
    )
  )
else
  conf.set10('inline_asm', false)
endif

arch_ext_list_external = []
arch_ext_list_inline = []

foreach arch_ext : arch_ext_list
  conf.set10(arch_ext, conf.get('asm') == 1)
  conf.set10('@0@_external'.format(arch_ext), conf.get('asm') == 1)
  conf.set10('@0@_inline'.format(arch_ext), conf.get('asm') == 1 and conf.get('inline_asm') == 1)
  arch_ext_list_external += ['@0@_external'.format(arch_ext)]
  arch_ext_list_inline += ['@0@_inline'.format(arch_ext)]
endforeach

have_list = (arch_ext_list + arch_ext_list_external + arch_ext_list_inline + arch_features +
  builtin_list + complex_funcs + have_list_cmdline +
  have_list_pub + headers_list + intrinsics_list + math_funcs + system_features + system_funcs +
  system_libraries + threads_list + toolchain_features + types_list + [
  'makeinfo',
  'makeinfo_html',
  'mmx2',
  'opencl_d3d11',
  'opencl_drm_arm',
  'opencl_drm_beignet',
  'opencl_dxva2',
  'opencl_vaapi_beignet',
  'opencl_vaapi_intel_media',
  'perl',
  'pod2man',
  'texi2html',
])

arch_features = [
  ['aligned_stack', ['aarch64', 'ppc', 'x86']],
  ['fast_64bit', ['aarch64', 'alpha', 'ia64', 'mips64', 'parisc64', 'ppc64', 'sparc64', 'x86_64']],
  ['fast_clz', ['aarch64', 'alpha', 'avr32', 'mips', 'ppc', 'x86']],
  ['fast_unaligned', ['aarch64', 'ppc', 'x86']],
  ['simd_align_16', ['altivec', 'neon', 'sse']],
  ['simd_align_32', ['avx']],
  ['simd_align_64', ['avx512']],
]

arch_list = [
  'aarch64',
  'alpha',
  'arm',
  'avr32',
  'avr32_ap',
  'avr32_uc',
  'bfin',
  'ia64',
  'm68k',
  'mips',
  'mips64',
  'parisc',
  'ppc',
  'ppc64',
  's390',
  'sh4',
  'sparc',
  'sparc64',
  'tilegx',
  'tilepro',
  'tomi',
  'x86',
  'x86_32',
  'x86_64',
]

ioctl_meteor_headers = [
  ['dev/bktr/ioctl_meteor.h', 'dev/bktr/ioctl_bt848.h'],
  ['machine/ioctl_meteor.h', 'machine/ioctl_bt848.h'],
  ['dev/video/meteor/ioctl_meteor.h', 'dev/video/bktr/ioctl_bt848.h'],
  ['dev/ic/bt8xx.h']]

headers_found = false

foreach headers : ioctl_meteor_headers
  if not headers_found
    n_headers_found = 0
    foreach header : headers
      header_found = cc.has_header(header)
      if header_found
        n_headers_found += 1
      endif
    endforeach

    headers_found = headers.length() == n_headers_found

    foreach header : headers
      conf.set10(header.underscorify(), headers_found)
    endforeach
  endif
endforeach

check_structs = [
  ['struct rusage', 'ru_maxrss', '#include <sys/time.h>\n#include<sys/resource.h>\n'],
  ['struct stat', 'st_mtim.tv_nsec', '#include <sys/stat.h>\n#define _BSD_SOURCE'],
]

hwaccel_autodetect_library_list = [
  'amf',
  'audiotoolbox',
  'crystalhd',
  'cuda',
  'cuvid',
  'd3d11va',
  'dxva2',
  'ffnvcodec',
  'nvdec',
  'nvenc',
  'vaapi',
  'vdpau',
  'videotoolbox',
  'v4l2_m2m',
  'xvmc',
]

external_autodetect_library_list = [
  'alsa',
  'appkit',
  'avfoundation',
  'bzlib',
  'coreimage',
  'iconv',
  'libxcb',
  'libxcb_shm',
  'libxcb_shape',
  'libxcb_xfixes',
  'lzma',
  'mediafoundation',
  'schannel',
  'sdl2',
  'securetransport',
  'sndio',
  'xlib',
  'zlib',
]

library_gpl_list = [
  'avisynth',
  'frei0r',
  'libcdio',
  'libdavs2',
  'librubberband',
  'libvidstab',
  'libx264',
  'libx265',
  'libxavs',
  'libxavs2',
  'libxvid',
]

library_nonfree_list = [
  'decklink',
  'libfdk_aac',
  'openssl',
  'libtls',
]

library_version3_list = [
  'gmp',
  'libaribb24',
  'liblensfun',
  'libopencore_amrnb',
  'libopencore_amrwb',
  'libvo_amrwbenc',
  'rkmpp',
]

library_gplv3_list = [
  'libsmbclient',
]

all_checks = []

cpp_cond = '''
#include <@0@>

#if !(@1@)
#error "unsatisfied condition: @1@"
#endif

int main(void) {
  return 0;
}
'''

check_winapi_family = '''
#ifdef WINAPI_FAMILY
#include <winapifamily.h>
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#error not desktop
#endif
#endif

int main(void) {
  return 0;
}
'''

libc_type = ''

if cc.links(cpp_cond.format('features.h', 'defined __UCLIBC__'))
  libc_type = 'uclibc'
  project_c_args += ['-D_POSIX_C_SOURCE=200112', '-D_XOPEN_SOURCE=600']
elif cc.links(cpp_cond.format('features.h', 'defined __GLIBC__'))
  libc_type = 'glibc'
  project_c_args += ['-D_POSIX_C_SOURCE=200112', '-D_XOPEN_SOURCE=600']
elif cc.links(cpp_cond.format('newlib.h', 'defined _NEWLIB_VERSION'))
  libc_type = 'newlib'
  project_c_args += ['-U__STRICT_ANSI__', '-D_XOPEN_SOURCE=600']
elif cc.links(cpp_cond.format('_mingw.h', 'defined __MINGW64_VERSION_MAJOR'))
  libc_type = 'mingw64'
  if cc.links(cpp_cond.format('_mingw.h', '__MINGW64_VERSION_MAJOR < 3'))
    # FIXME compat
  endif
  project_c_args += ['-U__STRICT_ANSI__', '-D__USE_MINGW_ANSI_STDIO=1']
  if cc.get_id() == 'gcc'
    project_c_args += ['-D__printf__=__gnu_printf__']
  endif
  if cc.links(cpp_cond.format('windows.h', '!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600'))
    project_c_args += ['-D_WIN32_WINNT=0x0600']
  endif
elif (cc.links(cpp_cond.format('_mingw.h', 'defined __MINGW_VERSION')) or
    cc.links(cpp_cond.format('_mingw.h', 'defined __MINGW32_VERSION')))
  libc_type = 'mingw32'
  if cc.links(cpp_cond.format('_mingw.h', '__MINGW32_MAJOR_VERSION > 3 || (__MINGW32_MAJOR_VERSION == 3 && __MINGW32_MINOR_VERSION >= 15'))
    error('MinGW32 runtime version must be >= 3.15')
  endif
  project_c_args += ['-U__STRICT_ANSI__', '-D__USE_MINGW_ANSI_STDIO=1']
  if cc.links(cpp_cond.format('_mingw.h', '__MSVCRT_VERSION__ < 0x0700'))
    project_c_args += ['-D__MSVCRT_VERSION__=0x0700']
  endif
  if cc.links(cpp_cond.format('windows.h', '!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600'))
    project_c_args += ['-D_WIN32_WINNT=0x0600']
  endif
  if cc.get_id() == 'gcc'
    project_c_args += ['-D__printf__=__gnu_printf__']
  endif
elif cc.links(cpp_cond.format('crtversion.h', 'defined _VC_CRT_MAJOR_VERSION'))
  libc_type = 'msvcrt'
  if cc.links(cpp_cond.format('crtversion.h', '_VC_CRT_MAJOR_VERSION < 14'))
    # FIXME compat
  endif
  project_c_args += ['-D_USE_MATH_DEFINES', '-D_CRT_SECURE_NO_WARNINGS', '-D_CRT_NONSTDC_NO_WARNINGS']
  if (cc.links(cpp_cond.format('stdlib.h', 'defined(_WIN32_WINNT)')) or
      cc.links(check_winapi_family))
    project_c_args += ['-D_WIN32_WINNT=0x0600']
  endif
  if not cc.has_function('strtoll')
    project_c_args += ['-Dstrtoll=_strtoi64']
  endif
  if not cc.has_function('strtoull')
    project_c_args += ['-Dstrtoull=_strtoui64']
  endif
elif cc.links(cpp_cond.format('stddef.h', 'defined __KLIBC__'))
  libc_type = 'klibc'
elif cc.links(cpp_cond.format('stddef.h', 'defined __BIONIC__'))
  libc_type = 'bionic'
elif cc.links(cpp_cond.format('sys/brand.h', 'defined LABELED_BRAND_NAME'))
  libc_type = 'solaris'
  project_c_args += ['-D__EXTENSIONS__', '-D_XOPEN_SOURCE=600']
endif

if not cc.has_function('localtime_r', prefix: '#include <time.h>', args: project_c_args)
  if cc.has_function('localtime_r', prefix: '#include <time.h>', args: project_c_args + ['-D_POSIX_C_SOURCE=200112', '-D_XOPEN_SOURCE=600'])
    project_c_args += ['-D_POSIX_C_SOURCE=200112', '-D_XOPEN_SOURCE=600']
  endif
endif

if libc_type != ''
  conf.set('libc_@0@'.format(libc_type), 1)
endif

conf.set10('pic', conf.get('pic', cc.links(cpp_cond.format('stdlib.h', 'defined(__PIC__) || defined(__pic__) || defined(PIC)'))))

check_pkg_config = [
  ['alsa', ['alsa/asoundlib.h'], ['snd_pcm_htimestamp'], {}],
  ['ffnvcodec', ['ffnvcodec/nvEncodeAPI.h', 'ffnvcodec/dynlink_cuda.h', 'ffnvcodec/dynlink_cuviddec.h', 'ffnvcodec/dynlink_nvcuvid.h'],
    [], {'versions': ['>= 9.1.23.1', ['>= 9.0.18.3', '< 9.1'], ['>= 8.2.15.10', '< 8.3'], ['>= 8.1.24.11', '< 8.2']]}],
  ['libgme', ['gme/gme.h'], ['gme_new_emu'], {}],
  ['gnutls', ['gnutls/gnutls.h'], ['gnutls_global_init'], {}],
  ['libaom', ['aom/aom_codec.h'], ['aom_codec_version'], {'pkg': 'aom', 'version': '>= 0.1.0'}],
  ['libaribb24', ['aribb24/aribb24.h'], ['arib_instance_new'], {'pkg': 'aribb24', 'version': '> 1.0.3'}],
  ['lv2', ['lilv-0/lilv/lilv.h'], ['lilv_world_new'], {'pkg': 'lilv-0'}],
  ['libass', ['ass/ass.h'], ['ass_library_init'], {}],
  ['libbluray', ['libbluray/bluray.h'], ['bd_open'], {}],
  ['libbs2b', ['bs2b.h'], ['bs2b_open'], {}],
  ['libcaca', ['caca.h'], ['caca_create_canvas'], {'pkg': 'caca'}],
  ['libcdio', ['cdio/cdda.h', 'cdio/paranoia.h'], ['cdio_cddap_open'], {'pkg': 'libcdio_paranoia'}],
  ['libcdio', ['cdio/paranoia/cdda.h', 'cdio/paranoia/paranoia.h'], ['cdio_cddap_open'], {'pkg': 'libcdio_paranoia'}],
  ['libdavs2', ['davs2.h'], ['davs2_decoder_open'], {'pkg': 'davs2', 'version': '>= 1.6.0'}],
  ['libdc1394', ['dc1394/dc1394.h'], ['dc1394_new'], {'pkg': 'libdc1394-2'}],
  ['libdrm', ['xf86drm.h'], ['drmGetVersion'], {}],
  ['libfdk_aac', ['fdk-aac/aacenc_lib.h'], ['aacEncOpen'], {'pkg': 'fdk-aac'}],
  ['libfontconfig', ['fontconfig/fontconfig.h'], ['FcInit'], {'pkg': 'fontconfig'}],
  ['libfreetype', ['ft2build.h', 'FT_FREETYPE_H'], ['FT_Init_FreeType'], {'pkg': 'freetype2'}],
  ['libfribidi', ['fribidi.h'], ['fribidi_version_info'], {'pkg': 'fribidi'}],
  ['libjack', ['jack/jack.h'], ['jack_port_get_latency_range'], {'pkg': 'jack'}],
  ['libklvanc', ['libklvanc/vanc.h'], ['klvanc_context_create'], {}],
  ['libkvazaar', ['kvazaar.h'], ['kvz_api_get'], {'pkg': 'kvazaar', 'version': '>= 0.8.1'}],
  ['liblensfun', ['lensfun.h'], ['lf_db_new'], {'pkg': 'lensfun'}],
  ['libmfx', ['mfx/mfxvideo.h'], ['MFXInit'], {}],
  ['libmodplug', ['libmodplug/modplug.h'], ['ModPlug_Load'], {}],
  ['libopencv', ['opencv2/core/core_c.h'], ['cvCreateImageHeader'], {'pkg': 'opencv'}],
  ['libopencv', ['opencv/cxcore.h'], ['cvCreateImageHeader'], {'pkg': 'opencv'}],
  ['libopenh264', ['wels/codec_api.h'], ['WelsGetCodecVersion'], {'pkg': 'openh264'}],
  ['libdav1d', ['dav1d/dav1d.h'], ['dav1d_version'], {'pkg': 'dav1d', 'version': '>= 0.4.0'}],
  ['libopenjpeg', ['openjpeg.h'], ['opj_version'], {'pkg': 'libopenjp2',
                                                    'version': '>= 2.1.0'}],
  ['libopenjpeg', ['openjpeg.h'], ['opj_version'], {'pkg': 'libopenjp2',
                                                    'version': '>= 2.1.0',
                                                    'c_args': ['-DOPJ_STATIC']}],
  ['libopenmpt', ['libopenmpt/libopenmpt.h'], ['openmpt_module_create'], {'version': '>= 0.2.6557'}],
  ['libopus', ['opus_multistream.h'], ['opus_multistream_decoder_create', 'opus_multistream_surround_encoder_create'], {'pkg': 'opus'}],
  ['libpulse', ['pulse/pulseaudio.h'], ['pa_context_new'], {}],
  ['librabbitmq', ['amqp.h'], ['amqp_new_connection'], {'pkg': 'librabbitmq', 'version': '>= 0.7.1'}],
  ['librav1e', ['rav1e.h'], ['rav1e_context_new'], {'pkg': 'rav1e', 'version': '>= 0.1.0'}],
  ['librist', ['librist/librist.h'], ['rist_receiver_create'], {'pkg': 'rist', 'version': '>= 0.2'}],
  ['librsvg', ['librsvg-2.0/librsvg/rsvg.h'], ['rsvg_handle_render_cairo'], {'pkg': 'librsvg-2.0'}],
  ['librtmp', ['librtmp/rtmp.h'], ['RTMP_Socket'], {}],
  ['librubberband', ['rubberband/rubberband-c.h'], ['rubberband_new'], {'pkg': 'rubberband', 'version': '>= 1.8.1'}],
  ['libshine', ['shine/layer3.h'], ['shine_encode_buffer'], {'pkg': 'shine'}],
  ['libsmbclient', ['libsmbclient.h'], ['smbc_init'], {'pkg': 'smbclient'}],
  ['libssh', ['libssh/sftp.h'], ['sftp_init'], {}],
  ['libspeex', ['speex/speex.h'], ['speex_decoder_init'], {'pkg': 'speex'}],
  ['libsrt', ['srt/srt.h'], ['srt_socket'], {'pkg': 'srt', 'version': '>= 1.2.0'}],
  ['libsvtav1', ['EbSvtAv1Enc.h'], ['svt_av1_enc_init_handle'], {'pkg': 'SvtAv1Enc', 'version': '>= 0.8.4'}],
  ['libtesseract', ['tesseract/capi.h'], ['TessBaseAPICreate'], {'pkg': 'tesseract'}],
  ['libtls', ['tls.h'], ['tls_configure'], {}],
  ['libv4l2', ['libv4l2.h'], ['v4l2_ioctl'], {}],
  ['libvidstab', ['vid.stab/libvidstab.h'], ['vsMotionDetectInit'], {'pkg': 'vidstab', 'version': '>= 0.98'}],
  ['libvmaf', ['libvmaf.h'], ['compute_vmaf'], {'version': '>= 0.6.2'}],
  ['libvorbis', ['vorbis/codec.h'], ['vorbis_info_init'], {'pkg': 'vorbis'}],
  ['libvorbisenc', ['vorbis/vorbisenc.h'], ['vorbis_encode_init'], {'pkg': 'vorbisenc'}],
  ['libvpx', ['vpx/vpx_decoder.h', 'vpx/vpx_encoder.h'], [], {'pkg': 'vpx', 'version': '>= 1.4.0'}],
  ['libvpx_vp8_decoder', ['vpx/vpx_decoder.h', 'vpx/vp8dx.h'], ['vpx_codec_vp8_dx'], {'pkg': 'vpx', 'version': '>= 1.4.0'}],
  ['libvpx_vp8_encoder', ['vpx/vpx_encoder.h', 'vpx/vp8cx.h'], ['vpx_codec_vp8_cx'], {'pkg': 'vpx', 'version': '>= 1.4.0'}],
  ['libvpx_vp9_decoder', ['vpx/vpx_decoder.h', 'vpx/vp8dx.h'], ['vpx_codec_vp9_dx'], {'pkg': 'vpx', 'version': '>= 1.4.0'}],
  ['libvpx_vp9_encoder', ['vpx/vpx_encoder.h', 'vpx/vp8cx.h'], ['vpx_codec_vp9_cx'], {'pkg': 'vpx', 'version': '>= 1.4.0'}],
  ['libwebp', ['webp/encode.h'], ['WebPGetEncoderVersion'], {'version': '>= 0.2.0'}],
  ['libwebp_anim_encoder', ['webp/mux.h'], ['WebPAnimEncoderOptionsInit'], {'pkg': 'libwebpmux', 'version': '>= 0.4.0'}],
  ['libx264', ['stdint.h', 'x264.h'], ['x264_encoder_encode'], {'pkg': 'x264'}],
  ['libx265', ['x265.h'], ['x265_api_get'], {'pkg': 'x265'}],
  ['libxcb', ['xcb/xcb.h'], ['xcb_connect'], {'pkg': 'xcb', 'version': '>= 1.4'}],
  ['libxcb_shm', ['xcb/shm.h'], ['xcb_shm_attach'], {'pkg': 'xcb-shm'}],
  ['libxcb_shape', ['xcb/shape.h'], ['xcb_shape_get_rectangles'], {'pkg': 'xcb-shape'}],
  ['libxcb_xfixes', ['xcb/xfixes.h'], ['xcb_xfixes_get_cursor_image'], {'pkg': 'xcb-xfixes'}],
  ['libxml2', ['libxml2/libxml/xmlversion.h'], ['xmlCheckVersion'], {'pkg': 'libxml-2.0'}],
  ['libzimg', ['zimg.h'], ['zimg_get_api_version'], {'pkg': 'zimg', 'version': '>= 2.7.0'}],
  ['libzmq', ['zmq.h'], ['zmq_ctx_new'], {}],
  ['libzvbi', ['libzvbi.h'], ['vbi_decoder_new'], {'pkg': 'zvbi-0.2'}],
  ['openssl', ['openssl/ssl.h'], ['OPENSSL_init_ssl', 'SSL_library_init'], {'has_any_func': true}],
  ['pocketsphinx', ['pocketsphinx/pocketsphinx.h'], ['ps_init'], {}],
  ['rkmpp', ['rockchip/rk_mpi.h'], ['mpp_create'], {'pkg': 'rockchip_mpp'}],
  ['rockchip_mpp', ['rockchip/rk_mpi.h'], ['mpp_create'], {'version': '>= 1.3.7'}],
  ['sdl2', ['SDL_events.h'], ['SDL_PollEvent'], {'version': ['>= 2.0.1', '< 3.0.0']}],
  ['vapoursynth', ['VSScript.h'], ['vsscript_init'], {'pkg': 'vapoursynth-script', 'version': '>= 42'}],
  ['vulkan', ['vulkan/vulkan.h'], ['vkCreateInstance'], {'pkg': 'vulkan', 'version': '>= 1.1.97'}],
]

foreach check : check_pkg_config
  all_checks += [['pkg-config', check]]
endforeach

conf.set10('vdpau', cc.has_header('vdpau/vdpau.h') and
  cc.get_define('VDP_DECODER_PROFILE_MPEG4_PART2_ASP', prefix: '#include <vdpau/vdpau.h>') != '')

conf.set10('nanosleep', cc.has_function('nanosleep', prefix: '#include <time.h>'))

check_components = [
  ['advapi32', ['windows.h'], ['RegCloseKey'], ['advapi32']],
  ['avsynth', ['avisynth/avisynth_c.h'], [], []],
  ['alsa', ['alsa/asoundlib.h'], ['snd_pcm_htimestamp'], ['asound']],
  ['android', ['android/native_window.h'], ['ANativeWindow_acquire'], ['android']],
  ['bcrypt', ['windows.h', 'bcrypt.h'], ['BCryptGenRandom'], ['bcrypt']],
  ['bzlib', ['bzlib.h'], ['BZ2_bzlibVersion'], ['bz2']],
  ['camera2ndk', ['stdbool.h', 'stdint.h', 'camera/NdkCameraManager.h'], ['ACameraManager_create'], ['camera2ndk']],
  ['decklink', ['DeckLinkAPI.h'], [], []],
  ['libcelt', ['celt/celt.h'], ['celt_decoder_create_custom'], ['celt0']],
  ['chromaprint', ['chromaprint.h'], ['chromaprint_get_version'], ['chromaprint']],
  ['clock_gettime', ['time.h'], ['clock_gettime'], []],
  ['clock_gettime', ['time.h'], ['clock_gettime'], ['rt']],
  ['cuda_sdk', ['cuda.h'], ['cuCtxCreate'], ['cuda']],
  ['crystalhd', ['stdint.h', 'libcrystalhd/libcrystalhd_if.h'], ['DtsCrystalHDVersion'], ['crystalhd']],
  ['frei0r', ['frei0r.h', 'dlfcn.h'], [], []],
  ['libgme', ['gme/gme.h'], ['gme_new_emu'], ['gme', 'stdc++']],
  ['gcrypt', ['gcrypt.h'], ['gcry_mpi_new'], ['gcrypt']],
  ['gmp', ['gmp.h'], ['mpz_export'], ['gmp']],
  ['ladspa', ['ladspa.h', 'dlfcn.h'], [], []],
  ['libcdio', ['cdio/cdda.h', 'cdio/paranoia.h'], ['cdio_cddap_open'], ['cdio_paranoia', 'cdio_cdda', 'cdio']],
  ['libcdio', ['cdio/paranoia/cdda.h', 'cdio/paranoia/paranoia.h'], ['cdio_cddap_open'], ['cdio_paranoia', 'cdio_cdda', 'cdio']],
  ['libcodec2', ['codec2/codec2.h'], ['codec2_create'], ['codec2']],
  ['libdl', ['dlfcn.h'], ['dlopen', 'dlsym'], []],
  ['libdl', ['dlfcn.h'], ['dlopen', 'dlsym'], ['dl']],
  ['libfdk_aac', ['fdk-aac/aacenc_lib.h'], ['aacEncOpen'], ['fdk-aac']],
  ['libflite', ['flite/flite.h'], ['flite_init'], ['flite_cmu_time_awb', 'flite_cmu_us_awb', 'flite_cmu_us_kal',
                                                   'flite_cmu_us_kal16', 'flite_cmu_us_rms', 'flite_cmu_us_slt',
                                                   'flite_usenglish', 'flite_cmulex', 'flite']],
  ['libgsm', ['gsm.h'], ['gsm_create'], ['gsm']],
  ['libgsm', ['gsm/gsm.h'], ['gsm_create'], ['gsm']],
  ['libiec61883', ['libiec61883/iec61883.h'], ['iec61883_cmp_connect'], ['raw1394', 'avc1394', 'rom1394', 'iec61883']],
  ['libilbc', ['ilbc.h'], ['WebRtcIlbcfix_InitDecode'], ['lbc']],
  ['libm', ['math.h'], ['sin'], ['m'], {'always-check': true}], # always check for libm don't provide an option to disable it
  ['libopenvino', ['c_api/ie_c_api.h'], ['ie_c_api_version'], ['inference_engine_c_api']],
  ['libnpp', ['npp.h'], ['nppGetLibVersion'], ['nppig', 'nppicc', 'nppc']],
  ['libnpp', ['npp.h'], ['nppGetLibVersion'], ['nppi', 'nppc']],
  ['libopencore_amrnb', ['opencore-amrnb/interf_dec.h'], ['Decoder_Interface_init'], ['opencore-amrnb']],
  ['libopencore_amrwb', ['opencore-amrwb/dec_if.h'], ['D_IF_init'], ['opencore-amrwb']],
  ['libopencv', ['opencv2/core/core_c.h'], ['cvCreateImageHeader'], ['opencv_core', 'opencv_imgproc']],
  ['libsmbclient', ['libsmbclient.h'], ['smbc_init'], ['smbclient']],
  ['libsnappy', ['snappy-c.h'], ['snappy_compress'], ['snappy', 'stdc++']],
  ['libsoxr', ['soxr.h'], ['soxr_create'], ['soxr']],
  ['libtensorflow', ['tensorflow/c/c_api.h'], ['TF_Version'], ['tensorflow']],
  ['libtheora', ['theora/theoraenc.h'], ['th_info_init'], ['theoraenc', 'theoradec', 'ogg']],
  ['libtwolame', ['twolame.h'], ['twolame_init', 'twolame_encode_buffer_float32_interleaved'], ['twolame']],
  ['libvo_amrwbenc', ['vo-amrwbenc/enc_if.h'], ['E_IF_init'], ['vo-amrwbenc']],
  ['libvpx_vp8_decoder', ['vpx/vpx_decoder.h', 'vpx/vp8dx.h'], ['vpx_codec_vp8_dx'], ['vpx'],
    {'defines': ['VPX_IMG_FMT_HIGHBITDEPTH'], 'extra_deps': ['libm_extra_deps', 'thread_dep']}],
  ['libvpx_vp8_encoder', ['vpx/vpx_encoder.h', 'vpx/vp8cx.h'], ['vpx_codec_vp8_cx'], ['vpx'],
    {'defines': ['VPX_IMG_FMT_HIGHBITDEPTH'], 'extra_deps': ['libm_extra_deps', 'thread_dep']}],
  ['libvpx_vp9_decoder', ['vpx/vpx_decoder.h', 'vpx/vp8dx.h'], ['vpx_codec_vp9_dx'], ['vpx'],
    {'defines': ['VPX_IMG_FMT_HIGHBITDEPTH'], 'extra_deps': ['libm_extra_deps', 'thread_dep']}],
  ['libvpx_vp9_encoder', ['vpx/vpx_encoder.h', 'vpx/vp8cx.h'], ['vpx_codec_vp9_cx'], ['vpx'],
    {'defines': ['VPX_IMG_FMT_HIGHBITDEPTH'], 'extra_deps': ['libm_extra_deps', 'thread_dep']}],
  ['libxvid', ['xvid.h'], ['xvid_global'], ['xvidcore']],
  ['linux_fb_h', ['linux/fb.h'], [], []],
  ['lzma', ['lzma.h'], ['lzma_version_number'], ['lzma']],
  ['mediafoundation', ['mfapi.h'], ['MFCreateAlignedMemoryBuffer'], ['mfplat', 'mfuuid', 'ole32', 'strmiids'], {'defines': ['mftransform_h']}],
  ['mediandk', ['stdint.h', 'media/NdkImage.h'], ['AImage_delete'], ['mediandk']],
  ['mmal', ['interface/mmal/mmal.h'], ['mmal_port_connect'], ['mmal_core', 'mmal_util', 'mmal_vc_client', 'bcm_host']],
  ['nanosleep', ['time.h'], ['nanosleep'], ['rt']],
  ['ole32', ['windows.h'], ['CoTaskMemFree'], ['ole32']],
  ['omx', ['OMX_Core.h'], [], []],
  ['openal', ['AL/al.h'], ['alGetError'], ['openal']],
  ['openal', ['AL/al.h'], ['alGetError'], ['OpenAL32']],
  # FIXME- Get opencl working - remove the disabler() at that time.
  ['opencl', ['CL/cl.h'], ['clEnqueueNDRangeKernel'], ['OpenCL', disabler()]],
  ['opengl', ['GL/glx.h'], ['glXGetProcAddress'], ['GL']],
  ['opengl', ['windows.h'], ['wglGetProcAddress'], ['opengl32', 'gdi32']],
  ['openssl', ['openssl/ssl.h'], ['SSL_library_init'], ['ssl', 'crypto']],
  ['openssl', ['openssl/ssl.h'], ['SSL_library_init'], ['ssl32', 'eay32']],
  ['openssl', ['openssl/ssl.h'], ['SSL_library_init'], ['ssl', 'crypto', 'ws2_32', 'gdi32']],
  ['psapi', ['windows.h', 'psapi.h'], ['GetProcessMemoryInfo'], ['psapi']],
  ['shell32', ['windows.h', 'shellapi.h'], ['CommandLineToArgvW'], ['shell32']],
  ['sndio', ['sndio.h'], ['sio_open'], ['sndio']],
  ['user32', ['windows.h', 'winuser.h'], ['GetShellWindow'], ['user32']],
  ['vfw32', ['windows.h', 'vfw.h'], ['capCreateCaptureWindow'], ['vfw3']],
  ['vaapi', ['va/va.h'], ['vaInitialize'], ['va']],
  ['zlib', ['zlib.h'], ['zlibVersion'], ['z'], {'meson-dep': 'zlib'}],

  # Libraries with preconditions
  ['vaapi_drm', ['va/va.h', 'va/va_drm.h'], ['vaGetDisplayDRM'], ['va', 'va-drm'],
    {'preconditions': ['vaapi']}],
  ['vaapi_x11', ['va/va.h', 'va/va_x11.h'], ['vaGetDisplay'], ['va', 'va-x11', 'X11'],
    {'preconditions': ['vaapi']}],
  ['vdpau_x11', ['vdpau/vdpau.h', 'vdpau/vdpau_x11.h'], ['vdp_device_create_x11'], ['vdpau', 'X11'],
    {'preconditions' : ['vdpau']}],
  ['libmfx', ['mfx/mfxvideo.h'], ['MFXInit'], ['libmfx'],
    {'preconditions': ['advapi32'], 'extra_deps': ['advapi32_extra_deps']}],
  ['libmp3lame', ['lame/lame.h'], ['lame_set_VBR_quality'], ['mp3lame'],
    {'preconditions': ['libm'], 'extra_deps': ['libm_extra_deps']}],
  ['libmysofa', ['mysofa.h'], ['mysofa_load'], ['mysofa'],
    {'preconditions': ['zlib'], 'extra_deps': ['zlib_extra_deps']}],
  ['libx264', ['stdint.h', 'x264.h'], ['x264_encoder_encode'], ['x264'],
    {'preconditions': ['libm'], 'extra_deps': ['libm_extra_deps']}],
  ['libxavs', ['stdint.h', 'xavs.h'], ['xavs_encoder_encode'], ['xavs'],
    {'preconditions': ['libm'], 'extra_deps': ['libm_extra_deps']}],
]

foreach check : check_components
  all_checks += [['component', check]]
endforeach

check_header_funcs = [
  ['aligned_malloc', ['malloc.h'], {'funcs': ['_aligned_malloc']}],
  ['arc4random', ['stdlib.h']],
  ['kbhit', ['conio.h']],
  ['setmode', ['io.h']],
  ['lzo1x_999_compress', ['lzo/lzo1x.h']],
  ['mach_absolute_time', ['mach/mach_time.h']],
  ['getenv', ['stdlib.h']],
  ['lstat', ['sys/stat.h']],
  ['GetProcessAffinityMask', ['windows.h']],
  ['GetProcessTimes', ['windows.h']],
  ['GetSystemTimeAsFileTime', ['windows.h']],
  ['LoadLibrary', ['windows.h']],
  ['MapViewOfFile', ['windows.h']],
  ['PeekNamedPipe', ['windows.h']],
  ['SetConsoleTextAttribute', ['windows.h']],
  ['SetConsoleCtrlHandler', ['windows.h']],
  ['Sleep', ['windows.h']],
  ['VirtualAlloc', ['windows.h']],
  ['glob', ['glob.h']],
  ['XvGetPortAttribute', ['X11/Xlib.h', 'X11/extensions/Xvlib.h'], {'link_with': ['Xv', 'X11', 'Xext']}],
  ['CreateDIBSection', ['windows.h'], {'link_with': ['gdi32']}],
  ['InitializeSecurityContext', ['windows.h', 'security.h'], {'link_with': ['secur32']}],
  ['atomic_cas_ptr', ['atomic.h']],
  ['machine_rw_barrier', ['mbarrier.h'], {'funcs': ['__machine_rw_barrier']}],
  ['MemoryBarrier', ['windows.h']],
  ['x264_csp_bgr', ['stdint.h', 'x264.h'], {'defines': ['X264_CSP_BGR']}],
  ['posix_memalign', ['stdlib.h']],
  ['sem_timedwait', ['semaphore.h'], {'preconditions': ['pthreads'], 'extra_deps': ['thread_dep']}],
]

foreach func : complex_funcs
  check_header_funcs += [[func, ['complex.h', 'math.h']]]
endforeach

foreach func : math_funcs
  check_header_funcs += [[func, ['math.h'], {'extra_deps': ['libm_extra_deps']}]]
endforeach

check_funcs = [
  ['getaddrinfo'],
  ['inet_aton'],
  ['memalign'],
  ['access'],
  ['fcntl'],
  ['fork'],
  ['gethrtime'],
  ['getopt'],
  ['getrusage'],
  ['gettimeofday'],
  ['isatty'],
  ['mkstemp'],
  ['mmap'],
  ['mprotect'],
  ['sched_getaffinity'],
  ['setrlimit'],
  ['strerror_r'],
  ['sysconf'],
  ['sysctl'],
  ['usleep'],
  ['pthread_join', {'preconditions': ['pthreads'], 'extra_deps': ['thread_dep']}],
  ['pthread_create', {'preconditions': ['pthreads'], 'extra_deps': ['thread_dep']}],
  ['pthread_cancel', {'preconditions': ['pthreads'], 'extra_deps': ['thread_dep']}],
  ['sync_val_compare_and_swap', {'funcs': ['__sync_val_compare_and_swap']}],
  ['gmtime_r'],
  ['localtime_r'],
]

check_types = [
  ['DXVA_PicParams_HEVC', ['windows.h', 'dxva.h'], {'c_args': ['-DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP', '-D_CRT_BUILD_DESKTOP_APP=0']}],
  ['DXVA_PicParams_VP9', ['windows.h', 'dxva.h'], {'c_args': ['-DWINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP', '-D_CRT_BUILD_DESKTOP_APP=0']}],
  ['ID3D11VideoDecoder', ['windows.h', 'd3d11.h']],
  ['ID3D11VideoContext', ['windows.h', 'd3d11.h']],
  ['DXVA2_ConfigPictureDecode', ['d3d9.h', 'dxva2api.h'], {'c_args': ['-D_WIN32_WINNT=0x0602']}],
  ['VAPictureParameterBufferHEVC', ['va/va.h', 'va/va_dec_hevc.h']],
  ['VAPictureParameterBufferVP8', ['va/va.h', 'va/va_dec_vp8.h']],
  ['VAProcPipelineParameterBuffer', ['va/va.h', 'va/va_vpp.h']],
  ['VAProcPipelineParameterBuffer', ['va/va.h', 'va/va_vpp.h']],
  ['VAEncPictureParameterBufferH264', ['va/va.h', 'va/va_enc_h264.h']],
  ['VAEncPictureParameterBufferHEVC', ['va/va.h', 'va/va_enc_hevc.h']],
  ['VAEncPictureParameterBufferJPEG', ['va/va.h', 'va/va_enc_mpeg2.h']],
  ['VAEncPictureParameterBufferMPEG2', ['va/va.h', 'va/va_enc_mpeg2.h']],
  ['VAEncPictureParameterBufferVP8', ['va/va.h', 'va/va_enc_vp8.h']],
  ['VAEncPictureParameterBufferVP9', ['va/va.h', 'va/va_enc_vp9.h']],
  ['VdpPictureInfoHEVC', ['vdpau/vdpau.h']],
  ['IBaseFilter', ['dshow.h']],
  ['clCreateImageFromFdINTEL_fn', ['CL/cl_intel.h'],
    {'preconditions': ['libdrm', 'opencl']}],
  ['clCreateFromVA_APIMediaSurfaceINTEL_fn', ['CL/cl.h', 'CL/va_ext.h'],
    {'preconditions': ['vaapi', 'opencl', 'libmfx']}],
  ['cl_dx9_surface_info_khr', ['CL/cl_dx9_media_sharing.h'],
    {'preconditions': ['dxva2', 'opencl']}],
  ['clGetDeviceIDsFromD3D11KHR_fn', ['CL/cl_d3d11.h'],
    {'preconditions': ['d3d11va', 'opencl']}],
  ['struct addrinfo', ['netdb.h']],
  ['struct group_source_req', ['netinet/in.h']],
  ['struct ip_mreq_source', ['netinet/in.h']],
  ['struct ipv6_mreq', ['netinet/in.h']],
  ['struct pollfd', ['poll.h']],
  ['struct sctp_event_subscribe', ['netinet/sctp.h']],
  ['struct sockaddr_in6', ['netinet/in.h']],
  ['struct sockaddr_storage', ['sys/types.h', 'sys/socket.h']],
  ['socklen_t', ['sys/types.h', 'sys/socket.h']],
]

if cc.has_header('arpa/inet.h')
  conf.set('arpa_inet_h', 1)
  check_funcs += [['closesocket']]
elif cc.has_header('winsock2.h')
  conf.set('winsock2_h', 1)
  # FIXME network_extralibs
  check_header_funcs += [
    ['closesocket', ['winsock2.h']],
    ['getaddrinfo', ['ws2tcpip.h']],
    ]
  check_types += [
    ['socklen_t', ['ws2tcpip.h']],
    ['struct addrinfo', ['ws2tcpip.h']],
    ['struct group_source_req', ['ws2tcpip.h']],
    ['struct ip_mreq_source', ['ws2tcpip.h']],
    ['struct ipv6_mreq', ['ws2tcpip.h']],
    ['struct pollfd', ['winsock2.h']],
    ['struct sockaddr_in6', ['ws2tcpip.h']],
    ['struct sockaddr_storage', ['ws2tcpip.h']],
  ]
endif

foreach check : check_funcs
  all_checks += [['func', check]]
endforeach

foreach check : check_header_funcs
  all_checks += [['header-func', check]]
endforeach

foreach check : check_types
  all_checks += [['type', check]]
endforeach

check_headers = [
  ['direct.h'],
  ['dirent.h'],
  ['dxgidebug.h'],
  ['msa.h'],
  ['net/udplite.h'],
  ['sys/param.h'],
  ['sys/resource.h'],
  ['sys/select.h'],
  ['sys/soundcard.h'],
  ['sys/un.h'],
  ['sys/videoio.h'],
  ['termios.h'],
  ['unistd.h'],
  ['valgrind/valgrind.h'],
  ['X11/extensions/XvMClib.h'],
  ['asm/types.h'],
]

if host_machine.system() == 'darwin'
  check_headers += [['dispatch/dispatch.h']]
elif host_machine.system() == 'android'
  check_headers += [['jni.h']]
endif

foreach check : check_headers
  all_checks += [['header', check]]
endforeach

external_library_list = library_gpl_list + library_nonfree_list + library_version3_list + library_gplv3_list + [
  'chromaprint',
  'gcrypt',
  'gnutls',
  'jni',
  'ladspa',
  'libaom',
  'libass',
  'libbluray',
  'libbs2b',
  'libcaca',
  'libcelt',
  'libcodec2',
  'libdav1d',
  'libdc1394',
  'libdrm',
  'libflite',
  'libfontconfig',
  'libfreetype',
  'libfribidi',
  'libglslang',
  'libgme',
  'libgsm',
  'libiec61883',
  'libilbc',
  'libjack',
  'libklvanc',
  'libkvazaar',
  'libmodplug',
  'libmp3lame',
  'libmysofa',
  'libopencv',
  'libopenh264',
  'libopenjpeg',
  'libopenmpt',
  'libopus',
  'libpulse',
  'librabbitmq',
  'librav1e',
  'librist',
  'librsvg',
  'librtmp',
  'libshine',
  'libsmbclient',
  'libsnappy',
  'libsoxr',
  'libspeex',
  'libsrt',
  'libssh',
  'libsvtav1',
  'libtesseract',
  'libtheora',
  'libtwolame',
  'libuavs3d',
  'libv4l2',
  'libvmaf',
  'libvorbis',
  'libvpx',
  'libwebp',
  'libxml2',
  'libzimg',
  'libzmq',
  'libzvbi',
  'lv2',
  'mediacodec',
  'openal',
  'opengl',
  'pocketsphinx',
  'vapoursynth',
]

hwaccel_library_nonfree_list = [
  'cuda_sdk',
  'libnpp',
]

hwaccel_library_list = hwaccel_library_nonfree_list + [
  'libmfx',
  'mmal',
  'omx',
  'opencl',
  'vulkan'
]

subsystem_list = [
  'dct',
  'dwt',
  'error_resilience',
  'faan',
  'fast_unaligned',
  'fft',
  'lsp',
  'lzo',
  'mdct',
  'pixelutils',
  'network',
  'rdft',
]

meta_list = [
  'autodetect',
  'fontconfig',
  'linux_perf',
  'memory_poisoning',
  'neon_clobber_test',
  'ossfuzz',
  'pic',
  'thumb',
  'valgrind_backtrace',
  'xmm_clobber_test',
]

feature_list = [
  'ftrapv',
  'gray',
  'hardcoded_tables',
  'omx_rpi',
  'runtime_cpudetect',
  'safe_bitstream_reader',
  'shared',
  'small',
  'static',
  'swscale_alpha',
]

network_funcs = [
  'getaddrinfo',
  'inet_aton',
]

extra_funcs = []

config_extra = [
  'aandcttables',
  'ac3dsp',
  'adts_header',
  'audio_frame_queue',
  'audiodsp',
  'blockdsp',
  'bswapdsp',
  'cabac',
  'cbs',
  'cbs_h264',
  'cbs_h265',
  'cbs_mpeg2',
  'dirac_parse',
  'dnn',
  'dvprofile',
  'exif',
  'faandct',
  'faanidct',
  'fdctdsp',
  'flacdsp',
  'fmtconvert',
  'frame_thread_encoder',
  'g722dsp',
  'golomb',
  'gplv3',
  'h263dsp',
  'h264chroma',
  'h264dsp',
  'h264parse',
  'h264pred',
  'h264qpel',
  'hevcparse',
  'hpeldsp',
  'huffman',
  'huffyuvdsp',
  'huffyuvencdsp',
  'idctdsp',
  'iirfilter',
  'mdct15',
  'intrax8',
  'iso_media',
  'ividsp',
  'jpegtables',
  'lgplv3',
  'libx262',
  'llauddsp',
  'llviddsp',
  'llvidencdsp',
  'lpc',
  'lzf',
  'me_cmp',
  'mpeg_er',
  'mpegaudio',
  'mpegaudiodsp',
  'mpegaudioheader',
  'mpegvideo',
  'mpegvideoenc',
  'mss34dsp',
  'pixblockdsp',
  'qpeldsp',
  'qsv',
  'qsvdec',
  'qsvenc',
  'qsvvpp',
  'rangecoder',
  'riffdec',
  'riffenc',
  'rtpdec',
  'rtpenc_chain',
  'rv34dsp',
  'sinewin',
  'snappy',
  'srtp',
  'startcode',
  'texturedsp',
  'texturedspenc',
  'tpeldsp',
  'vaapi_1',
  'vaapi_encode',
  'vc1dsp',
  'videodsp',
  'vp3dsp',
  'vp56dsp',
  'vp8dsp',
  'wma_freqs',
  'wmv2dsp',
]

license_list = [
  'gpl',
  'nonfree',
  'version3',
]

library_list = [
  'avcodec',
  'avdevice',
  'avfilter',
  'avformat',
  'avresample',
  'avutil',
  'postproc',
  'swresample',
  'swscale',
]

program_list = [
  'ffplay',
  'ffprobe',
  'ffmpeg',
]

apple_frameworks = [
  'CoreFoundation',
  'CoreMedia',
  'CoreVideo',
  'AppKit',
  'AudioToolbox',
  'AVFoundation',
  'CoreImage',
  'VideoToolbox',
  'CoreGraphics',
  'CoreServices',
  'ApplicationServices',
]

avcodec_components = [
  'bsfs',
  'decoders',
  'encoders',
  'hwaccels',
  'parsers',
]

avdevice_components = [
  'indevs',
  'outdevs',
]

avfilter_components = [
  'filters',
]

avformat_components = [
  'demuxers',
  'muxers',
  'protocols',
]

component_list = avcodec_components + avdevice_components + avfilter_components + avformat_components

foreach component : component_list
  conf.set10(component, get_option(component).allowed())
endforeach

config_list = (
  document_list +
  example_list +
  external_library_list +
  external_autodetect_library_list +
  hwaccel_library_list +
  hwaccel_autodetect_library_list +
  feature_list +
  license_list +
  library_list +
  program_list +
  subsystem_list +
  [
    'autodetect',
    'fontconfig',
    'linux_perf',
    'memory_poisoning',
    'neon_clobber_test',
    'ossfuzz',
    'pic',
    'thumb',
    'valgrind_backtrace',
    'xmm_clobber_test',
  ] +
  component_list
)

check_compiles = [
  ['v4l2_m2m', ['linux/videodev2.h'], 'int i = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_VIDEO_M2M | V4L2_BUF_FLAG_LAST;'],
  ['pragma_deprecated', [], 'int main(void) {_Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\""); return 0; }'],
  ['const_nan', ['math.h'], 'struct { double d; } static const bar[] = { { NAN } };'],
  ['intrinsics_neon', ['arm_neon.h'], 'int16x8_t test = vdupq_n_s16(0);'],
]

check_links = [
  ['stdatomic', ['stdatomic.h'], 'int main(void) {atomic_int foo, bar = ATOMIC_VAR_INIT(-1); atomic_store(&foo, 0); foo += bar;}', {'link_with': ['atomic']}],
  ['stdatomic', ['stdatomic.h'], 'int main(void) {atomic_int foo, bar = ATOMIC_VAR_INIT(-1); atomic_store(&foo, 0); foo += bar;}'],
]

v4l2m2m_formats = [
  ['vc1', 'VC1_ANNEX_G'],
  ['mpeg1'],
  ['mpeg2'],
  ['mpeg4'],
  ['hevc'],
  ['h263'],
  ['h264'],
  ['vp8'],
  ['vp9'],
]

foreach format : v4l2m2m_formats
  suffix = format.length() > 1 ? format[1] : format[0].to_upper()
  check_compiles += [['@0@_v4l2_m2m'.format(format[0]), ['sys/time.h', 'linux/videodev2.h'], 'int i = V4L2_PIX_FMT_@0@;'.format(suffix)]]
endforeach

foreach check : check_compiles
  all_checks += [['compiles', check]]
endforeach

foreach check : check_links
  all_checks += [['links', check]]
endforeach

foreach arch : arch_list
  conf.set(arch, 0)
endforeach

foreach lib : hwaccel_autodetect_library_list
  conf.set(lib, 0)
endforeach

foreach check_struct : check_structs
  conf_name = '@0@_@1@'.format(check_struct[0].underscorify(), check_struct[1].underscorify())

  conf.set10(conf_name,
    cc.has_member(check_struct[0], check_struct[1], prefix : check_struct[2]))
endforeach

foreach license : license_list
  conf.set10(license, not get_option(license).disabled())
endforeach

foreach lib : library_list
  conf.set(lib, get_option(lib).enabled().to_int())
endforeach

arch = 'c'

cpu = host_machine.cpu_family()

if cpu == 'x86'
  arch = 'x86'
  conf.set('x86', 1)
  conf.set('i686', 1)
  conf.set('x86_32', 1)
elif cpu == 'x86_64'
  arch = 'x86'
  conf.set('x86', 1)
  conf.set('i686', 1)
  conf.set('x86_64', 1)
elif cpu == 'aarch64'
  # FIXME: Implement assembly support for MSVC
  # It relies on Meson adding support for armasm and MASM,
  # see foreach check : asm_as_checks below.
  if cc.get_id() == 'msvc'
    warning('Using aarch64 assembly on MSVC is not yet supported by Meson.')
  else
    arch = 'aarch64'
    conf.set('aarch64', 1)
  endif
elif cpu == 'arm'
  arch = 'arm'
  conf.set('arm', 1)
endif

foreach feature : arch_features
  name = feature[0]
  have = false
  conditions = feature[1]
  foreach condition: conditions
    if conf.get(condition, 0) == 1
      have = true
    endif
  endforeach

  conf.set10(name, have)
endforeach

if cc.get_id() == 'clang' 
  if conf.get('x86_32') == 1
    # Clang doesn't support maintaining alignment without assuming the
    # same alignment in every function. If 16 byte alignment would be
    # enabled, one would also have to either add attribute_align_arg on
    # every single entry point into the libraries or enable -mstackrealign
    # (doing stack realignment in every single function).

    if ['windows', 'freebsd', 'netbsd', 'openbsd'].contains(host_machine.system())
      conf.set('aligned_stack', 0)
    else
      project_c_args += cc.get_supported_arguments([
        '-mllvm -stack-alignment=16',
        '-mstack-alignment=16'
      ])
    endif
  else
    project_c_args += cc.get_supported_arguments([
        '-mllvm -stack-alignment=16',
        '-mstack-alignment=16'
      ])
  endif
elif cc.get_id() == 'msvc'
  if conf.get('x86_32') == 1
    conf.set('aligned_stack', 0)
  endif
  # the new SSA optimzer in VS2015 U3 is mis-optimizing some parts of the code
  # Issue has been fixed in MSVC v19.00.24218.
  if cc.links(cpp_cond.format('windows.h', '_MSC_FULL_VER < 190024218'))
    project_c_args += cc.get_supported_arguments([
      'd2SSAOptimizer-'
    ])
  endif
  if conf.get('x86_32') == 1 and (get_option('buildtype') == 'debug' or get_option('optimization') == '0')
    project_c_args += cc.get_supported_arguments([
      '-Oy-'
    ])
  endif
endif

conf.set10('shared', get_option('default_library') != 'static')
conf.set('pthreads', 0)
conf.set('w32threads', 0)
conf.set('os2threads', 0)
conf.set10('bigendian', host_machine.endian() == 'big')
conf.set10('fast_unaligned', ['x86', 'x86_64', 'aarch', 'ppc'].contains(host_machine.cpu_family()))

conf.set('frame_thread_encoder', 1)

if ['linux', 'darwin', 'android'].contains(host_machine.system())
  conf.set10('pthreads', thread_dep.found())
elif host_machine.system() == 'windows'
  conf.set10('w32threads', thread_dep.found())
  conf.set('symver', 0)
  conf.set('dos_paths', 1)
endif

if ['linux', 'openbsd', 'android'].contains(host_machine.system())
  conf.set('section_data_rel_ro', 1)
endif

# Our option names can't be 1:1 mapping because newer meson versions disallow
# options with language prefixes i.e. "cuda_sdk" in our case
options_map = {
  'cuda_sdk' : 'ff_cuda_sdk',
  'linux_fb_h' : 'linux_fbdev',
}

foreach check : all_checks
  check_type = check[0]
  check = check[1]

  name = check[0]
  opt_name = options_map.get(name, name)

  if check_type == 'func'
    headers = []
  elif check_type == 'header'
    headers = [name]
  else
    headers = check[1]
  endif

  opts = {}
  link_withs = []
  funcs = []
  compiles = ''
  links = ''
  type = ''

  if check_type == 'pkg-config'
    if check.length() > 3
      opts = check[3]
    endif
  elif check_type == 'component'
    link_withs = check[3]
    if check.length() > 4
      opts = check[4]
    endif
  elif check_type == 'header-func'
    if check.length() > 2
      opts = check[2]
    endif
  elif check_type == 'func' or check_type == 'header'
    if check.length() > 1
      opts = check[1]
    endif
  elif check_type == 'compiles'
    if check.length() > 3
      opts = check[3]
    endif
    compiles = check[2]
  elif check_type == 'links'
    if check.length() > 3
      opts = check[3]
    endif
    links = check[2]
  elif check_type == 'type'
    if check.length() > 2
      opts = check[2]
    endif
    type = name
  endif

  if check_type == 'header-func' or check_type == 'func'
    funcs = opts.get('funcs', [name])
  elif check_type == 'pkg-config' or check_type == 'component'
    funcs = check[2]
  endif

  if check_type != 'component'
    link_withs = opts.get('link_with', [])
  endif

  found = false

  if conf.get(name.underscorify(), 0) == 0
    defines = opts.get('defines', [])
    has_any_func = opts.get('has_any_func', false)
    c_args = opts.get('c_args', [])
    extra_deps = []

    foreach dep_name : opts.get('extra_deps', [])
      extra_deps += [get_variable(dep_name)]
    endforeach

    prefix = ''

    found = true
    skipchecks = false

    preconditions = opts.get('preconditions', [])

    foreach precondition : preconditions
      found = found and conf.get(precondition) == 1
    endforeach

    # FIXME: or should these just be preconditions in the list above instead?
    disabled = false
    if get_option('gpl').disabled()
      gpl_list = library_gpl_list + library_gplv3_list
      disabled = gpl_list.contains(name)
    endif
    if get_option('version3').disabled()
      v3_list = library_gplv3_list + library_version3_list
      disabled = disabled or v3_list.contains(name)
    endif
    if get_option('nonfree').disabled()
      nonfree_list = library_nonfree_list + hwaccel_library_nonfree_list
      disabled = disabled or nonfree_list.contains(name)
    endif

    if disabled
      if get_option(opt_name).enabled()
        error('Incompatible options combination: @0@ requested, but has disabled license.'.format(name))
      endif
      message('Skipping @0@ because of license'.format(name))
      found = false
    endif

    if found
      if check_type != 'pkg-config'
        foreach link_with : link_withs
          if check_type == 'component' and not ('always-check' in opts)
            req = get_option(opt_name)
          else
            req = false
          endif
          dep = cc.find_library(link_with, required : req)
          if not dep.found() and opts.has_key('meson-dep')
            dep = dependency(opts.get('meson-dep'), required: false)
            # We can't do compiler checks for non-external dependencies, so skip them and trust the meson dependency
            skipchecks = true
          endif
          found = found and dep.found()
          extra_deps += dep
        endforeach
      else
        req = get_option(opt_name)
        pkg_name = opts.get('pkg', name)
        if opts.has_key('versions')
          foreach version: opts['versions']
            dep = dependency(pkg_name, required: false, version: version)
          endforeach

          if not dep.found() and req.enabled()
            error('@0@ @1@ could not be found'.format(pkg_name, opts['versions']))
          endif
        elif opts.has_key('version')
          dep = dependency(pkg_name, required : req, version: opts.get('version'))
        else
          dep = dependency(pkg_name, required : req)
        endif
        found = dep.found()
        skipchecks = dep.type_name() == 'internal'  # same here, trust the meson dependency
        extra_deps += dep
      endif
    endif

    if found
      foreach header : headers
        if header.endswith('.h')
          prefix += '#include <@0@>\n'.format(header)
        else
          prefix += '#include @0@\n'.format(header)
        endif

        if not conf.has(header.underscorify())
          has_header = skipchecks or cc.has_header(header, dependencies: extra_deps)
          conf.set10(header.underscorify().to_lower(), has_header)
          found = found and has_header
        endif
      endforeach
    endif

    if found and not skipchecks
      n_funcs_found = 0
      foreach symbol : funcs
        if check_type == 'header-func'
          if cc.has_header_symbol(headers[0], symbol, dependencies : extra_deps, prefix: prefix, args : c_args + project_c_args)
            n_funcs_found += 1
          endif
        else
          if cc.has_function(symbol, dependencies : extra_deps, prefix: prefix, args : c_args + project_c_args)
            n_funcs_found += 1
          endif
        endif
      endforeach
      if has_any_func
        found = n_funcs_found >= 1
      else
        found = n_funcs_found == funcs.length()
      endif
    endif

    if found and not skipchecks
      n_defines_found = 0
      foreach symbol : defines
        if cc.get_define(symbol, dependencies : extra_deps, prefix: prefix, args : c_args + project_c_args) != ''
          n_defines_found += 1
        endif
      endforeach
      found = n_defines_found == defines.length()
    endif

    if found and compiles != '' and not skipchecks
      found = cc.compiles('\n\n'.join([prefix, compiles]), dependencies : extra_deps, args : c_args + project_c_args, name: name)
    endif

    if found and links != '' and not skipchecks
      found = cc.links('\n\n'.join([prefix, links]), dependencies : extra_deps, args : c_args + project_c_args, name: name)
    endif

    if found and type != '' and not skipchecks
      found = cc.has_type(type, prefix : prefix, dependencies : extra_deps, args : c_args + project_c_args)
    endif

    if check_type != 'header'
      conf.set10(name.underscorify().to_lower(), found)
    endif

    foreach symbol : funcs + defines
      conf.set10(symbol.to_lower(), found)
    endforeach

    if found
      set_variable('@0@_extra_deps'.format(name.to_lower().underscorify()), extra_deps)
    else
      set_variable('@0@_extra_deps'.format(name.to_lower().underscorify()), [])
    endif
  endif
endforeach

if conf.get('gnutls') == 1 and conf.get('openssl') == 1
  # Simply picking one for now
  conf.set('openssl', 0)
endif

if conf.get('opengl') == 1 and not cc.has_header('GL/glext.h')
  conf.set('opengl', 0)
endif

if conf.get('libx265') == 1
  x265_build = cc.get_define('X265_BUILD', prefix: '#include <x265.h>', dependencies: libx265_extra_deps)

  if x265_build == ''
    message ('X265_BUILD not found, disabling')
    conf.set('libx265', 0)
  elif x265_build.to_int() < 68
    message ('X265_BUILD too old, disabling')
    conf.set('libx265', 0)
  else
    message ('X265_BUILD >= 68, keeping enabled')
  endif
endif

if conf.get('libzvbi') == 1
  conf.set('libzvbi', cc.compute_int(
    'VBI_VERSION_MAJOR > 0 || VBI_VERSION_MINOR > 2 || VBI_VERSION_MINOR == 2 && VBI_VERSION_MICRO >= 28',
    prefix: '#include <libzvbi.h>', dependencies: libzvbi_extra_deps))
endif

x86asm_type = 'none'

x86asm_checks = []

asm_inline_checks = [
  ['"1:\\n"', 'inline_asm_labels'],
  ['"Label:\\n"', 'inline_asm_nonlocal_labels']
]

extern_prefix = cc.symbols_have_underscore_prefix() ? '_' : ''

asm_insn_checks = []

asm_as_checks = []

if host_machine.system() == 'windows'
  asm_format = conf.get('x86_64') == 1 ? 'win64' : 'win32'
elif host_machine.system() == 'darwin'
  asm_format = conf.get('x86_64') == 1 ? 'macho64' : 'macho32'
else
  asm_format = conf.get('x86_64') == 1 ? 'elf64' : 'elf32'
endif

x86asm = disabler()
conf.set('x86asm', 0)

if conf.get('x86') == 1
  conf.set10('rdtsc', cc.has_function('__rdtsc', prefix: '#include <intrin.h>'))
  conf.set10('mm_empty', cc.has_function('_mm_empty', prefix: '#include <mmintrin.h>'))

  conf.set('local_aligned', 1)

  if conf.get('asm', 0) == 1
    nasm_version = '>=0'
    if host_machine.system() == 'windows'
      nasm_version = '>=2.12'
    endif
    x86asm = find_program('nasm', version: nasm_version, required: false)
    x86asm_type = 'nasm'
    if not x86asm.found() and host_machine.system() == 'windows'
      # Note that on Windows we require NASM because when optimizations are
      # disabled, the Windows linker does not optimize out functions with
      # EXTERNAL_* SIMD calls and errors out with unresolved symbols
      subproject('win-nasm')
      x86asm = find_program('nasm', version: nasm_version)
    endif
    if not x86asm.found()
      x86asm = find_program('yasm', required: false)
      x86asm_type = 'yasm'
    endif
    conf.set10('x86asm', x86asm.found())
    if x86asm.found()
      x86asm_checks += [
        ['meson_asm_checks/cpunop.asm', 'cpunop'],
        ['meson_asm_checks/avx2_external.asm', 'avx2_external'],
        ['meson_asm_checks/avx512_external.asm', 'avx512_external'],
        ['meson_asm_checks/fma4_external.asm', 'fma4_external'],
        ['meson_asm_checks/xop_external.asm', 'xop_external'],
      ]

      asm_inline_checks += [
        ['"":::"%xmm0"', 'xmm_clobbers'],
        ['"pabsw %xmm0, %xmm0"', 'ssse3_inline'],
        ['"pmaxub %mm0, %mm1"', 'mmxext_inline'],
      ]
    endif
  endif

  ebp_available = cc.run('''
  int main() {
    volatile int i=0;
    __asm__ volatile ("xorl %%ebp, %%ebp" ::: "%ebp");
    return i;
  }
  ''', name: 'if_ebp_available_this_should_crash')

  # FFmpeg upstream doesn't know that EBP is always unavailable
  # under Clang if stack realignment is used. This is probably
  # because the only compilers still supporting the architecture
  # are the Android NDK and MSYS CLANG32.
  # Thus an additional check on aligned_stack is done here.
  # See:
  # - https://github.com/android/ndk/issues/693
  # - https://bugs.llvm.org/show_bug.cgi?id=37542
  conf.set10('ebp_available', conf.get('aligned_stack') == 1 and ebp_available.compiled() and ebp_available.returncode() != 0)

  asm_inline_checks += [
    ['""::"b"(0)', 'ebx_available_1'],
    ['"":::"%ebx"', 'ebx_available_2'],
    ['"movl @0@test, %eax"'.format(extern_prefix), 'inline_asm_direct_symbol_refs_1'],
    ['"movl @0@test(%rip), %eax"'.format(extern_prefix), 'inline_asm_direct_symbol_refs_2'],
    ['"":::"%xmm0"', 'xmm_clobbers'],
    ['"pabsw %xmm0, %xmm0"', 'ssse3_inline'],
    ['"pmaxub %mm0, %mm1"', 'mmxext_inline'],
  ]
elif conf.get('aarch64') == 1
  asm_insn_checks += [
    ['prfm   pldl1strm, [x0]', 'armv8'],
    ['ext   v0.8B, v0.8B, v1.8B, #1', 'neon'],
    ['fmadd d0,    d0,    d1,    d2', 'vfp'],
  ]
elif conf.get('arm') == 1
  if cc.get_id() == 'msvc'
    conf.set10('thumb', cc.has_header_symbol('stddef.h', '_M_ARMT'))
  endif
  if cc.has_header_symbol('stddef.h', '__thumb__')
    conf.set10('thumb', cc.compiles(
      'float func(float a, float b){ return a+b; }',
      name: 'weak_thumb'))
  endif
  if conf.get('thumb', 0) == 1
    if cc.has_argument('-mthumb')
      project_c_args += ['-mthumb']
    elif cc.has_argument('-marm')
      project_c_args += ['-marm']
    endif
  endif

  if cc.has_header_symbol('stddef.h', '__ARM_PCS_VFP')
    conf.set('vfp_args', 1)
  elif cc.links(cpp_cond.format('stddef.h', 'defined _M_ARM_FP && _M_ARM_FP >= 30'), name: 'vfp_args_fp')
    conf.set('vfp_args', 1)
  elif not cc.links(cpp_cond.format('stddef.h', 'defined __ARM_PCS || defined __SOFTFP__'), name: 'arm_pcs_or_softfp') and host_machine.system() != 'darwin'
    if not cc.has_header_symbol('stddef.h', '__SOFTFP__')
      conf.set('vfp_args', 1)
      conf.set('fpabi', 'vfp')
    elif cc.links('__asm__ (".eabi_attribute 28, 1"); int main(void) { return 0; }', name: 'vfp_abi_check')
      conf.set('vfp_args', 1)
      conf.set('fpabi', 'vfp')
    else
      conf.set('fpabi', 'soft')
    endif

    message('Floating point ABI: @0@'.format(conf.get('fpabi')))
  endif

  asm_insn_checks += [
    ['qadd r0, r0, r0', 'armv5te'],
    ['sadd16 r0, r0, r0', 'armv6'],
    ['movt r0, #0', 'armv6t2'],
    ['vadd.i16 q0, q0, q0', 'neon'],
    ['fadds s0, s0, s0', 'vfp'],
    ['vmov.f32 s0, #1.0', 'vfpv3'],
    ['setend be', 'setend']
  ]

  asm_inline_checks += [
    ['"add r0, %Q0, %R0" :: "r"((long long)0)', 'asm_mod_q']
  ]

  asm_as_checks += [
    ['.arch armv7-a', 'as_arch_directive'],
    ['ra .dn d0.i16', 'as_dn_directive'],
    ['.fpu neon', 'as_fpu_directive']
  ]

  # llvm's integrated assembler supports .object_arch from llvm 3.5
  if asm_format == 'elf32' or asm_format == 'elf64'
    asm_as_checks += [
      ['.object_arch armv4', 'as_object_arch']
    ]
  endif
endif

asm_args = ['-DPIC']
# Add debug args explicitly
# yasm: `-g <format>`
# nasm: ` -g -F <format>`
# The only modern debugging format supported by either is DWARF
#
# nasm and yasm don't support DWARF in the PE format (Windows)
if asm_format.startswith('win')
  debug_format = 'null'
else
  # Supported on Linux, BSD, macOS
  debug_format = 'dwarf'
endif
# Add the format args
if x86asm_type == 'yasm'
  if debug_format == 'dwarf'
    asm_args += ['-g', 'dwarf2']
  else
    asm_args += ['-g', 'null']
  endif
elif x86asm_type == 'nasm'
  if debug_format == 'dwarf'
    asm_args += ['-g', '-F', 'dwarf']
  endif
endif

asm_inc = join_paths(meson.current_source_dir(), 'libavutil/x86/')

if extern_prefix != ''
  asm_args += ['-DPREFIX']
endif

foreach ext : arch_ext_list_x86_simd
  if ext == 'mmx'
    continue
  endif

  inline = '@0@_inline'.format(ext)
  external = '@0@_external'.format(ext)
  conf.set10(inline, conf.get(ext) == 1 and conf.get('inline_asm') == 1 and conf.get('x86') == 1)
  conf.set10(external, conf.get(ext) == 1 and conf.get('x86asm') == 1)
endforeach

foreach check: x86asm_checks
  result = run_command(x86asm, '-f', asm_format, asm_args, '-Werror', check[0], '-o', join_paths(meson.current_build_dir(), 'tmpasm.o'), check: false)

  if conf.get('asm') == 1
    conf.set10(check[1], result.returncode() == 0)

    if result.returncode() != 0
    message('@0@ x86 asm check failed: @1@'.format(check[1], result.stderr().strip()))
    endif
  else
    conf.set10(check[1], false)
  endif
endforeach

foreach check : asm_insn_checks
  asm_inline_checks += [
    ['"@0@"'.format(check[0]), '@0@_inline'.format(check[1])]
  ]
  asm_as_checks += [
    [check[0], '@0@_external'.format(check[1])]
  ]
endforeach

foreach check: asm_inline_checks
  if conf.get('inline_asm') == 1
    result = cc.compiles(
      'void foo(void){ __asm__ volatile(@0@); }'.format(check[0]),
      name: check[1]
    )

    conf.set10(check[1], result)
  else
    conf.set10(check[1], false)
  endif
endforeach

if (conf.get('arm', 0) == 1 or conf.get('aarch64', 0) == 1) and cc.get_id() != 'msvc'
  asm_as_checks += [
    ['''
    .macro m n, y:vararg=0
    .int \y
    .endm
    m x
    ''', 'vararg'],
  ]

  if conf.get('arm', 0) == 1
    asm_as_checks += [
      ['.altmacro', 'gnu_as']
    ]
  endif

  asm_as_checks += [
    ['''.func test
      .endfunc''', 'as_func']
  ]
endif


# FIXME: this won't work until Meson detects and
# exposes the underlying assembler.
# These checks cannot be done with run_command either
# because they rely on setup-time input.
# See:
# - https://github.com/mesonbuild/meson/issues/9889
# - https://github.com/mesonbuild/meson/pull/10916
# - https://github.com/mesonbuild/meson/pull/10922
foreach check : asm_as_checks
  warning('Meson does not yet expose the ASM compiler, skipping @0@ check'.format(check[1]))
  # if conf.get('asm') == 1
  #   conf.set10(check[1], cc.compiles(
  #     check[0],
  #     args: ['-x', 'assembler'],
  #     name: check[1]
  #   ))
  # else 
    conf.set10(check[1], false)
  # endif
endforeach

if conf.get('x86') == 1
  conf.set10('ebx_available', conf.get('ebx_available_1') == 1 and conf.get('ebx_available_2') == 1)
  message('EBX available: @0@'.format(conf.get('ebx_available')))
  conf.set10('inline_asm_direct_symbol_refs', conf.get('inline_asm_direct_symbol_refs_1') == 1 or conf.get('inline_asm_direct_symbol_refs_2') == 1)
  message('Inline assembly direct symbol references available: @0@'.format(conf.get('inline_asm_direct_symbol_refs')))
endif

arm_skip_checks = conf.get('arm', 0) == 1 and (host_machine.system() == 'linux' or host_machine.system() == 'android')

foreach ext : arch_ext_list_arm
  inline = '@0@_inline'.format(ext)
  external = '@0@_external'.format(ext)
  if not arm_skip_checks
    conf.set10(ext, conf.get(inline, 0) == 1 or conf.get(external, 0) == 1)
  endif
endforeach

if get_option('b_lto')
  conf.set10('inline_asm_direct_symbol_refs', 0)
endif

extern_things = [
  ['AVOutputFormat', 'muxer', 'libavformat/allformats.c', 'muxer_list'],
  ['AVInputFormat', 'demuxer', 'libavformat/allformats.c', 'demuxer_list'],
  ['AVCodec', 'encoder', 'libavcodec/allcodecs.c', 'encoder_list'],
  ['AVCodec', 'decoder', 'libavcodec/allcodecs.c', 'decoder_list'],
  ['AVCodecParser', 'parser', 'libavcodec/parsers.c', 'parser_list'],
  ['AVBitStreamFilter', 'bsf', 'libavcodec/bitstream_filters.c', 'bsf_list'],
  ['AVHWAccel', 'hwaccel', 'libavcodec/hwaccels.h', 'hwaccel_list'],
  ['URLProtocol', 'protocol', 'libavformat/protocols.c', 'protocol_list'],
  ['AVOutputFormat', 'muxer', 'libavdevice/alldevices.c', 'outdev_list', 'outdev'],
  ['AVInputFormat', 'demuxer', 'libavdevice/alldevices.c', 'indev_list', 'indev'],
]

find_things_extern = find_program('find_things_extern.py')

foreach extern_thing : extern_things
  c_type = extern_thing[0]
  type = extern_thing[1]
  filename = extern_thing[2]
  variable_name = extern_thing[3]

  if extern_thing.length() > 4
    thing_suffix = extern_thing[4]
  else
    thing_suffix = type
  endif

  result = run_command(find_things_extern,
                       '--type=@0@'.format(c_type),
                       '--suffix=@0@'.format(type),
                       '--thing-suffix=@0@'.format(thing_suffix),
                       files(filename),
                       check: true)
  list = result.stdout().strip().split('\n')

  thing_allowed = get_option('@0@s'.format(thing_suffix)).allowed()

  foreach elem : list
    opt = get_option(elem)
    conf.set(elem, (opt.enabled() or (opt.allowed() and thing_allowed)).to_int())
  endforeach

  set_variable(variable_name, list)
endforeach

find_things = find_program('find_things.py')

result = run_command(find_things, files('libavfilter/allfilters.c'), check: true)
filter_list = result.stdout().strip().split('\n')

filters_allowed = get_option('filters').allowed()

foreach filter : filter_list
  opt = get_option(filter)
  conf.set(filter, (opt.enabled() or (opt.allowed() and filters_allowed)).to_int())
endforeach

foreach program : program_list
  conf.set(program, get_option(program).allowed().to_int())
endforeach

result = run_command(find_things, '--full', files('libavfilter/allfilters.c'), check: true)
full_filter_list = result.stdout().strip().split('\n')

avcodec_components_list = bsf_list + decoder_list + encoder_list + hwaccel_list + parser_list
avdevice_components_list = indev_list + outdev_list
avfilter_components_list = filter_list
avformat_components_list = demuxer_list + muxer_list + protocol_list

all_components = avcodec_components_list + avdevice_components_list + avfilter_components_list + avformat_components_list

tmpconfig_h = configure_file(configuration: conf, output: 'tmpconfig.h')

# Working around Windows command line limitation
components_to_resolve = configuration_data()
foreach comp : config_list + config_extra + have_list + all_components
  components_to_resolve.set(comp, true)
endforeach
components_h = configure_file(configuration: components_to_resolve, output: 'components.h')

dep_resolver = find_program('depresolver.py')

res = run_command(dep_resolver, tmpconfig_h, components_h, check: true)

conf = configuration_data()

split = res.stdout().split('//BEGIN_DEPENDS')
resolved = split[0]
flattened_deps = split[1]

foreach kv : resolved.split()
  split = kv.split('=')
  k = split[0]
  v = split[1].to_int()

  conf.set(k, v)
endforeach

foreach kv : flattened_deps.split()
  split = kv.split('=')
  k = split[0]
  v = split[1].split(',')

  set_variable('@0@_flattened_deps'.format(k), v)
endforeach

dshow_indev_extra_deps = []

if host_machine.system() == 'windows'
  foreach libname: ['psapi', 'ole32', 'strmiids', 'uuid', 'oleaut32', 'shlwapi']
    dshow_indev_extra_deps += [cc.find_library(libname, required: false)]
  endforeach
endif

foreach comp : avcodec_components_list
  avcodec_flattened_deps += get_variable('@0@_flattened_deps'.format(comp), [])
endforeach

foreach comp : avdevice_components_list
  avdevice_flattened_deps += get_variable('@0@_flattened_deps'.format(comp), [])
endforeach

foreach comp : avfilter_components_list
  avfilter_flattened_deps += get_variable('@0@_flattened_deps'.format(comp), [])
endforeach

foreach comp : avformat_components_list
  avformat_flattened_deps += get_variable('@0@_flattened_deps'.format(comp), [])
endforeach

subdir('libavutil')
subdir('libpostproc')
subdir('libswresample')
subdir('libswscale')
subdir('libavcodec')
subdir('libavformat')
subdir('libavfilter')
subdir('libavdevice')
subdir('libavresample')
subdir('fftools')

util_list = ['cuda', 'd3d11va', 'dxva2', 'libdrm', 'lzo', 'mediacodec', 'opencl', 'qsv', 'vaapi', 'vdpau', 'videotoolbox']

if conf.get('schannel') == 1
  secur32_dep = cc.find_library('secur32')
  if secur32_dep.found()
    libavformat_optional_deps += [secur32_dep]
  else
    conf.set('schannel', 0)
  endif
endif

things_to_print = [
  ['libavcodec/codec_list.c', 'AVCodec', 'codec_list', encoder_list + decoder_list],
  ['libavcodec/parser_list.c', 'AVCodecParser', 'parser_list', parser_list],
  ['libavcodec/bsf_list.c', 'AVBitStreamFilter', 'bitstream_filters', bsf_list],
  ['libavformat/muxer_list.c', 'AVOutputFormat', 'muxer_list', muxer_list],
  ['libavformat/demuxer_list.c', 'AVInputFormat', 'demuxer_list', demuxer_list],
  ['libavformat/protocol_list.c', 'URLProtocol', 'url_protocols', protocol_list],
  ['libavdevice/outdev_list.c', 'AVOutputFormat', 'outdev_list', outdev_list],
  ['libavdevice/indev_list.c', 'AVInputFormat', 'indev_list', indev_list],
  ['libavfilter/filter_list.c', 'AVFilter', 'filter_list', filter_list],
]

print_things = find_program('print_things.py')

foreach thing : things_to_print
  list = []
  ctr = 0
  foreach comp : thing[3]
    if conf.get(comp) == 1
      if thing[1] == 'AVFilter'
        comp_name = full_filter_list[ctr]
      elif thing[2] == 'indev_list'
        comp_name = '@0@_demuxer'.format(comp.split('_indev')[0])
      elif thing[2] == 'outdev_list'
        comp_name = '@0@_muxer'.format(comp.split('_outdev')[0])
      else
        comp_name = comp
      endif

      list += [comp_name]
    endif
    ctr += 1
  endforeach

  if thing[1] == 'AVFilter'
    list += ['asrc_abuffer', 'vsrc_buffer', 'asink_abuffer', 'vsink_buffer']
  endif

  res = run_command(print_things,
    '--filename=@0@'.format(join_paths(meson.current_build_dir(), thing[0])),
    '--struct-name=@0@'.format(thing[1]),
    '--name=@0@'.format(thing[2]),
    list, check: true)
endforeach

if not cc.check_header('stdatomic.h') and thread_dep.found()
  if host_machine.system() == 'windows'
    common_incs += [include_directories('compat/atomics/win32')]
  elif cc.get_id() == 'gcc'
    common_incs += [include_directories('compat/atomics/gcc')]
  endif
endif

add_project_arguments(project_c_args, language: 'c')

libavutil_optional_deps = [vaapi_drm_extra_deps + vaapi_x11_extra_deps + vdpau_x11_extra_deps]
libavdevice_extra_deps = [xvgetportattribute_extra_deps]
libswresample_optional_deps = [libsoxr_extra_deps]

common_deps += libm_extra_deps

built_libs = [
  ['avutil', []],
  ['postproc', ['libavutil']],
  ['swresample', ['libavutil']],
  ['swscale', ['libavutil']],
  ['avcodec', ['libavutil', 'libswresample']],
  ['avformat', ['libavutil', 'libavcodec']],
  ['avresample', ['libavutil']],
  ['avfilter', ['libavutil', 'libavcodec', 'libavformat', 'libswscale', 'libswresample', 'libavresample', 'libpostproc']],
  ['avdevice', ['libavutil', 'libavformat', 'libavfilter', 'libavcodec']],
]

windows = import('windows')

makedef = find_program('compat/windows/makedef.py')

makedef_args = [makedef]

if cc.get_id() != 'msvc'
  nm = find_program('nm', required: true)
  makedef_args += ['--nm', nm]
else
  dumpbin = find_program('dumpbin', required: true)
  makedef_args += ['--dumpbin', dumpbin]
endif

makedef_args += ['--prefix', extern_prefix]

foreach built_lib : built_libs
  sources = get_variable('lib@0@_sources'.format(built_lib[0])) + [ffversion_h]
  x86asm_sources = []
  tests = get_variable('lib@0@_tests'.format(built_lib[0]), [])
  gen_objects = []
  optional_deps = get_variable('lib@0@_optional_deps'.format(built_lib[0]), [])

  sources += get_variable('lib@0@_@1@_sources'.format(built_lib[0], arch), [])

  if host_machine.system() == 'windows'
    sources += windows.compile_resources(
      'lib@0@/@1@res.rc'.format(built_lib[0], built_lib[0]),
      include_directories: common_incs,
      depends: ffversion_h
    )
  endif

  foreach arch_ext : arch_list
    if conf.get(arch_ext, 0) == 1
      sources += get_variable('lib@0@_@1@_sources'.format(built_lib[0], arch_ext), [])
    endif
  endforeach

  foreach arch_ext : arch_ext_list
    if conf.get(arch_ext, 0) == 1
      sources += get_variable('lib@0@_@1@_@2@_sources'.format(built_lib[0], arch, arch_ext), [])
    endif
  endforeach

  if conf.get('x86asm') == 1
    x86asm_sources += get_variable('lib@0@_@1@_x86asm_sources'.format(built_lib[0], arch), [])
  endif

  optional_sources = get_variable('lib@0@_optional_sources'.format(built_lib[0]), {})
  optional_tests = get_variable('lib@0@_optional_tests'.format(built_lib[0]), {})

  foreach comp_name, comp_sources : optional_sources
    if conf.get(comp_name, 0) == 1
      sources += comp_sources
    endif

    if languages_map.has_key(comp_name)
      foreach lang: languages_map[comp_name]
        add_languages(languages_map[comp_name], required: get_option(comp_name).enabled(), native: false)
      endforeach
    endif
  endforeach

  optional_sources = get_variable('lib@0@_@1@_optional_sources'.format(built_lib[0], arch), {})
  foreach comp_name, comp_sources : optional_sources
    if conf.get(comp_name, 0) == 1
      sources += comp_sources
    endif
  endforeach

  if conf.get('x86asm') == 1
    optional_sources = get_variable('lib@0@_@1@_x86asm_optional_sources'.format(built_lib[0], arch), {})
    foreach comp_name, comp_sources : optional_sources
      if conf.get(comp_name, 0) == 1
        x86asm_sources += comp_sources
      endif
    endforeach
  endif

  foreach arch_ext : arch_ext_list
    if conf.get(arch_ext, 0) == 1
      optional_sources = get_variable('lib@0@_@1@_@2@_optional_sources'.format(built_lib[0], arch, arch_ext), {})
      foreach comp_name, comp_sources : optional_sources
        if conf.get(comp_name, 0) == 1
          sources += comp_sources
        endif
      endforeach
    endif
  endforeach

  foreach comp_name, comp_tests : optional_tests
    if conf.get(comp_name, 0) == 1
      tests += comp_tests
    endif
  endforeach

  foreach dep_comp_name: get_variable('@0@_flattened_deps'.format(built_lib[0]), [])
    optional_deps += get_variable('@0@_extra_deps'.format(dep_comp_name), [])
  endforeach

  deps = common_deps + optional_deps

  link_with = []

  foreach dep : built_lib[1]
    link_with += [get_variable(dep)]
  endforeach

  version = get_variable('lib@0@_majorver'.format(built_lib[0]))

  if x86asm_sources.length() > 0
    # FIXME dedup should happen in meson
    deduped_sources = []
    foreach source : x86asm_sources
      if not deduped_sources.contains(source)
        deduped_sources += [source]
      endif
    endforeach

    asm_gen = generator(x86asm,
      output : '@BASENAME@.o',
      arguments : asm_args + [
        '-f', asm_format,
        '-i', meson.current_source_dir() + '/',
        '-i', meson.current_build_dir() + '/',
        '-i', join_paths(meson.current_source_dir(), 'lib@0@'.format(built_lib[0]), 'x86', ''),
        '-P', join_paths(meson.current_build_dir(), 'config.asm'),
        '@INPUT@',
        '-o', '@OUTPUT@'])

    gen_objects += asm_gen.process(deduped_sources)
  endif

  vflag = '-Wl,--version-script,@0@/lib@1@/lib@1@.ver'.format(meson.current_build_dir(), built_lib[0])
  c_args = ['-DHAVE_AV_CONFIG_H', '-DBUILDING_@0@'.format(built_lib[0])]

  link_args = cc.get_supported_link_arguments([vflag])

  static_link_args = []

  if cc.get_id() == 'msvc'
    message('Applying MSVC link workaround for Debug mode and static libraries.')
    static_link_args += cc.get_supported_link_arguments('/FORCE:UNRESOLVED')

    if get_option('buildtype') == 'debug' or get_option('optimization') == '0'
      link_args += cc.get_supported_link_arguments('/FORCE:UNRESOLVED')
    endif
  endif

  static_lib = static_library('@0@-static'.format(built_lib[0]), sources + gen_objects,
    dependencies: deps,
    include_directories: common_incs,
    link_with: link_with,
    link_args: static_link_args,
    c_args: c_args,
    implicit_include_directories : false,
  )

  vs_module_def = custom_target('@0@-def'.format(built_lib[0]),
    output: '@0@.def'.format(built_lib[0]),
    command: [makedef_args, '@0@/lib@1@/lib@1@.ver'.format(meson.current_build_dir(), built_lib[0]), static_lib],
    capture: true
  )

  lib = library(built_lib[0],
    dependencies: deps,
    soversion: version,
    include_directories: common_incs,
    link_with: link_with,
    link_whole: static_lib,
    link_args: link_args,
    c_args: c_args,
    vs_module_defs: vs_module_def,
    implicit_include_directories : false,
    install: true,
  )

  set_variable('lib@0@'.format(built_lib[0]), lib)

  declared_dep = declare_dependency(link_with: lib,
    include_directories: include_directories('.'),
    dependencies: deps,
    version: get_variable('lib@0@_version'.format(built_lib[0]))
  )

  set_variable('lib@0@_dep'.format(built_lib[0]), declared_dep)

  if not get_option('tests').disabled()
    foreach test_ : tests
      test_name = test_[0]
      test_sources = test_[1]
      opts = test_.get(2, {})
      test_deps = deps

      extra_libs = opts.get('extra_libs', [])
      foreach libname: extra_libs
        test_deps += [cc.find_library(libname)]
      endforeach

      exe = executable('test_@0@_@1@'.format(built_lib[0], test_name), test_sources,
        c_args: c_args,
        dependencies: test_deps,
        include_directories: common_incs,
        link_args: static_link_args,
        link_with: [static_lib] + link_with,
        implicit_include_directories : false)

      test('@0@_@1@'.format(built_lib[0], test_name), exe,
        timeout: 180,
        workdir: join_paths(meson.current_source_dir(), 'lib@0@'.format(built_lib[0]), 'tests'))
    endforeach
  endif
endforeach

built_exe_list = []
if not get_option('cli').disabled()
  foreach program : program_list
    if conf.get(program) == 1
      built_exe_list += program
    endif
  endforeach
endif

foreach built_exe : built_exe_list
  sources = get_variable('@0@_sources'.format(built_exe)) + [ffversion_h]
  optional_deps = []

  message('Checking @0@ dependencies'.format(built_exe))

  optional_sources = get_variable('@0@_optional_sources'.format(built_exe), {})

  foreach comp_name, comp_sources : optional_sources
    if conf.get(comp_name, 0) == 1
      sources += comp_sources
    endif
  endforeach

  foreach dep_comp_name: get_variable('@0@_flattened_deps'.format(built_exe), [])
    optional_deps += get_variable('@0@_extra_deps'.format(dep_comp_name), [])
  endforeach

  message('Gathered @0@ optional deps'.format(optional_deps.length()))

  deps = common_deps + optional_deps

  exe = executable(built_exe, sources,
    install: true,
    dependencies: deps,
    include_directories: common_incs,
    link_with: [libavutil, libavcodec, libavfilter, libavdevice, libavformat, libavresample, libswscale, libswresample, libpostproc],
    implicit_include_directories : false,
  )

  set_variable('@0@_exe'.format(built_exe[0]), exe)
endforeach

pkg = import('pkgconfig')

pkg_infos = [
  ['Multimedia utilities', 'libavutil'],
  ['FFmpeg postprocessing library', 'libpostproc'],
  ['FFmpeg audio resampling library', 'libswresample'],
  ['FFmpeg image rescaling library', 'libswscale'],
  ['Multimedia codecs', 'libavcodec'],
  ['Multimedia formats', 'libavformat'],
  ['Media device handling', 'libavdevice'],
  ['Audio/video filtering library', 'libavfilter'],
  ['Libav audio resampling library', 'libavresample'],
]

foreach pkg_info : pkg_infos
  libname = pkg_info[1]
  pkg.generate(get_variable(libname),
    filebase:  libname,
    description: pkg_info[0],
    version: get_variable(libname + '_version')
  )
endforeach

final_conf = configuration_data()

final_conf.set_quoted('FFMPEG_CONFIGURATION', '') # FIXME
final_conf.set('CONFIG_THIS_YEAR', 2022)
final_conf.set_quoted('FFMPEG_DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
final_conf.set_quoted('AVCONV_DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
final_conf.set_quoted('CC_IDENT', '@0@ @1@'.format(cc.get_id(), cc.version()))
final_conf.set_quoted('BUILDSUF', '')
final_conf.set_quoted('SLIBSUF', host_machine.system() == 'windows' ? '.dll' : '.so')
final_conf.set('SWS_MAX_FILTER_SIZE', 256)
final_conf.set('EXTERN_ASM', extern_prefix)
final_conf.set_quoted('EXTERN_PREFIX', extern_prefix)

if not get_option('nonfree').disabled()
  license = 'nonfree and unredistributable'
elif not get_option('gpl').disabled() and not get_option('version3').disabled()
  license = 'GPL version 3 or later'
elif not get_option('version3').disabled()
  license = 'LGPL version 3 or later'
elif not get_option('gpl').disabled()
  license = 'GPL version 2 or later'
else
  license = 'LGPL version 2.1 or later'
endif
final_conf.set_quoted('FFMPEG_LICENSE', license)
message('License: ' + license)

if not get_option('version3').disabled()
  if not get_option('gpl').disabled()
    conf.set('gplv3', 1)
  else
    conf.set('lgplv3', 1)
  endif
endif

conf.set('mmx2', conf.get('mmxext'))

foreach restrict_kw : ['restrict', '__restrict__', '__restrict', '']
  if not final_conf.has('av_restrict') and cc.compiles('void foo(char * @0@ p);'.format(restrict_kw))
    final_conf.set('av_restrict', restrict_kw)
  endif
endforeach

foreach arch: arch_list
  final_conf.set('ARCH_@0@'.format(arch.to_upper()), conf.get(arch.to_lower()))
endforeach

foreach have: have_list
  if have == 'unistd_h' and conf.get(have) == 0
    # an atrocious hack to get this working with the zlib subproject on windows...
    # zlib also uses a HAVE_UNISTD_H define, but it checks it with #ifdef and not #if
    continue
  endif
  final_conf.set('HAVE_@0@'.format(have.to_upper()), conf.get(have.to_lower()))
endforeach

foreach config: config_list + config_extra + all_components
  final_conf.set('CONFIG_@0@'.format(config.to_upper()), conf.get(config.to_lower()))
endforeach

configure_file(
  output: 'config.h',
  configuration: final_conf,
)

configure_file(
  output: 'config.asm',
  output_format: 'nasm',
  configuration: final_conf,
)

# Put this at the end so people see it
if conf.get('x86_32') == 1 and conf.get('asm') == 1 and cc.get_id() == 'msvc' and cc.version().version_compare('<19.30')
  # -Dasm=enabled seems to hang reliably on some Visual Studio 2019 installations, ask users to report back to us
  warning('Using VS 2019 x86 with `asm` enabled: if you see a hang while linking, please report an issue at https://gitlab.freedesktop.org/gstreamer/meson-ports/ffmpeg/-/issues')
endif
