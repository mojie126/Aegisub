"""
Utility functions for loading video files into Aegisub using the VapourSynth
video provider.

When encountering a file whose file extension is not .py or .vpy, the
VapourSynth audio and video providers will execute the respective default
script set in Aegisub's configuration, with the following string variables set:
- filename: The path to the file that's being opened.
- __aegi_data, __aegi_dictionary, __aegi_local, __aegi_script, __aegi_temp, __aegi_user:
  The values of ?data, ?dictionary, etc. respectively.
- __aegi_locale: The current Aegisub interface language code.
- __aegi_vscache: The path to a directory where the VapourSynth script can
  store cache files. This directory is cleaned by Aegisub when it gets too
  large (as defined by Aegisub's configuration).

The provider reads the video from the script's 0-th output node. By default,
the video is assumed to be CFR. The script can pass further information to
Aegisub using the following variables:
    - __aegi_timecodes: List[int] | str: The timecodes for the video, or the
      path to a timecodes file.
    - __aegi_keyframes: List[int] | str: List of frame numbers to load as
      keyframes, or the path to a keyframes file.
    - __aegi_hasaudio: int: If nonzero, Aegisub will try to load an audio track
      from the same file.

The script can control the progress dialog shown by Aegisub with certain log
messages. Check the functions defined below for more information.

This module provides some utility functions to obtain timecodes, keyframes, and
other data.
"""
import os
import os.path
import re
import gettext
from enum import Enum
from collections import deque
from typing import Any, Dict, List, Tuple, Callable

import vapoursynth as vs
core = vs.core

aegi_vscache: str = ""
aegi_vsplugins: str = ""
_translate: Callable[[str], str] = lambda message: message

plugin_extension = ".dll" if os.name == "nt" else ".so"


def _(message: str) -> str:
    return _translate(message)


def _init_translation(vars: dict):
    global _translate

    language = vars.get("__aegi_locale", "")
    data_dir = vars.get("__aegi_data", "")
    languages: List[str] = []
    if language:
        languages.append(language)
        if "_" in language:
            languages.append(language.split("_", 1)[0])

    for localedir in (os.path.join(data_dir, "locale"), os.path.join(data_dir, "po")):
        try:
            translation = gettext.translation("aegisub", localedir=localedir, languages=languages or None)
            _translate = translation.gettext
            return
        except OSError:
            continue

    _translate = lambda message: message

def progress_set_message(message: str):
    """
    Sets the message of Aegisub's progress dialog.
    """
    vs.core.log_message(vs.MESSAGE_TYPE_DEBUG, f"__aegi_set_message,{message}")


def progress_set_progress(percent: float):
    """
    Sets the progress shown in Aegisub's progress dialog to
    the given percentage.
    """
    vs.core.log_message(vs.MESSAGE_TYPE_DEBUG, f"__aegi_set_progress,{percent}")


def progress_set_indeterminate():
    """
    Sets Aegisub's progress dialog to show indeterminate progress.
    """
    vs.core.log_message(vs.MESSAGE_TYPE_DEBUG, f"__aegi_set_indeterminate,")


def set_paths(vars: dict):
    """
    Initialize the wrapper library with the given configuration directories.
    Should usually be called at the start of the default script as
        set_paths(locals())
    """
    global aegi_vscache
    global aegi_vsplugins
    _init_translation(vars)
    aegi_vscache = vars["__aegi_vscache"]
    aegi_vsplugins = vars["__aegi_vsplugins"]


def ensure_plugin(name: str, loadname: str, errormsg: str):
    """
    Ensures that the VapourSynth plugin with the given name exists.
    If it doesn't, it tries to load it from `loadname`.
    If that fails, it raises an error with the given error message.
    """
    if hasattr(core, name):
        return

    if aegi_vsplugins and loadname:
        try:
            core.std.LoadPlugin(os.path.join(aegi_vsplugins, loadname + plugin_extension))
            if hasattr(core, name):
                return
        except vs.Error:
            pass

    raise vs.Error(errormsg)


def make_lwi_cache_filename(filename: str) -> str:
    """
    Given a path to a video, will return a file name like the one LWLibavSource
    would use for a .lwi file.
    """
    max_len = 254
    extension = ".lwi"

    if len(filename) + len(extension) > max_len:
        filename = filename[-(max_len + len(extension)):]

    return "".join(("_" if c in "/\\:" else c) for c in filename) + extension


def make_keyframes_filename(filename: str) -> str:
    """
    Given a path `path/to/file.mkv`, will return the path
    `path/to/file_keyframes.txt`.
    """
    extlen = filename[::-1].find(".") + 1
    return filename[:len(filename) - extlen] + "_keyframes.txt"


lwindex_re1 = re.compile(r"Index=(?P<Index>-?[0-9]+),POS=(?P<POS>-?[0-9]+),PTS=(?P<PTS>-?[0-9]+),DTS=(?P<DTS>-?[0-9]+),EDI=(?P<EDI>-?[0-9]+)")
lwindex_re2 = re.compile(r"Key=(?P<Key>-?[0-9]+),Pic=(?P<Pic>-?[0-9]+),POC=(?P<POC>-?[0-9]+),Repeat=(?P<Repeat>-?[0-9]+),Field=(?P<Field>-?[0-9]+)")
streaminfo_re = re.compile(r"Codec=(?P<Codec>[0-9]+),TimeBase=(?P<TimeBase>[0-9/]+),Width=(?P<Width>[0-9]+),Height=(?P<Height>[0-9]+),Format=(?P<Format>[0-9a-zA-Z]+),ColorSpace=(?P<ColorSpace>[0-9]+)")
videoindex_re = re.compile(r"<ActiveVideoStreamIndex>(?P<VideoStreamIndex>[0-9+]+)</ActiveVideoStreamIndex>")


class LWIndexFrame:
    pts: int
    key: int
    index: int

    def __init__(self, raw: list[str]):
        match1 = lwindex_re1.match(raw[0])
        match2 = lwindex_re2.match(raw[1])
        if not match1 or not match2:
            raise ValueError(_("Invalid lwindex format"))
        self.index = int(match1.group("Index"))
        self.pts = int(match1.group("PTS"))
        self.key = int(match2.group("Key"))

    def __int__(self) -> int:
        return self.pts


def info_from_lwindex(indexfile: str) -> Dict[str, List[int]]:
    """
    Given a path to an .lwi file, will return a dictionary containing
    information about the video, with the keys
    - timcodes: The timecodes.
    - keyframes: Array of frame numbers of keyframes.
    """
    with open(indexfile, encoding="latin1") as f:
        index = f.read().splitlines()

    videoindex_str = next(l for l in index if l.startswith("<ActiveVideoStreamIndex>"))
    videoindex_match = videoindex_re.match(videoindex_str)
    if not videoindex_match:
        raise ValueError(_("Invalid lwindex format: Invalid ActiveVideoStreamIndex line"))
    videoindex = int(videoindex_match.group("VideoStreamIndex"))

    # The picture list starts after the last </StreamInfo> tag
    indexstart, indexend = (len(index) - index[::-1].index("</StreamInfo>")), index.index("</LibavReaderIndex>")
    frames = [LWIndexFrame(index[i:i+2]) for i in range(indexstart, indexend, 2)]
    frames = [f for f in frames if f.index == videoindex]    # select the first stream
    frames.sort(key=int)

    streaminfo = streaminfo_re.match(index[index.index(f"<StreamInfo={videoindex},0>") + 1]) # info of first stream
    if not streaminfo:
        raise ValueError(_("Invalid lwindex format"))

    timebase_num, timebase_den = [int(i) for i in streaminfo.group("TimeBase").split("/")]

    return {
        "timecodes": [(f.pts * 1000 * timebase_num) // timebase_den for f in frames],
        "keyframes": [i for i, f in enumerate(frames) if f.key],
    }


# 标准显示高度列表（与 C++ 端 kStandardHeights 保持一致）
_STANDARD_HEIGHTS = (480, 720, 1080, 1440, 2160, 4320)


def _calculate_adaptive_padding(frame_height: int, user_padding: int) -> tuple:
    """计算自适应黑边分配。

    根据帧高度和用户期望的单侧黑边值，自动匹配最近的标准分辨率高度，
    将总黑边平均分配到上下两侧。

    Args:
        frame_height: 原始帧高度（不含黑边）
        user_padding: 用户期望的单侧黑边行数

    Returns:
        (padding_top, padding_bottom) 元组
    """
    user_padding = int(user_padding)
    if user_padding <= 0 or frame_height <= 0:
        return (0, 0)

    symmetric_total_h = frame_height + user_padding * 2
    best_standard = 0
    best_diff = float("inf")
    for sh in _STANDARD_HEIGHTS:
        if sh < frame_height:
            continue
        diff = abs(sh - symmetric_total_h)
        if diff < best_diff and diff <= user_padding:
            best_diff = diff
            best_standard = sh

    if best_standard > 0 and best_standard > frame_height:
        total_padding = best_standard - frame_height
        half = total_padding // 2
        remainder = total_padding % 2
        return (half + remainder, half)

    return (user_padding, user_padding)


def wrap_lwlibavsource(filename: str, padding: str, cachedir: str | None = None, **kwargs: Any) -> Tuple[vs.VideoNode, Dict[str, List[int]]]:
    """
    Given a path to a video file and a directory to store index files in
    (usually __aegi_vscache), will open the video with LWLibavSource and read
    the generated .lwi file to obtain the timecodes and keyframes.
    Additional keyword arguments are passed on to LWLibavSource.
    """
    if cachedir is None:
        cachedir = aegi_vscache

    os.makedirs(cachedir, exist_ok=True)
    cachefile = os.path.join(cachedir, make_lwi_cache_filename(filename))

    progress_set_message(_("Loading video file"))
    progress_set_indeterminate()

    ensure_plugin("lsmas", "LSMASHSource", _("To use Aegisub's LWLibavSource wrapper, the `lsmas` plugin for VapourSynth must be installed"))

    import inspect
    if "cachedir" not in inspect.getfullargspec(vs.core.lsmas.LWLibavSource).args:
        raise vs.Error(_("To use Aegisub's LWLibavSource wrapper, the `lsmas` plugin must support the `cachedir` option for LWLibavSource."))

    lw_kwargs = dict(kwargs)
    lw_kwargs.setdefault("prefer_hw", 3)
    clip = core.lsmas.LWLibavSource(source=filename, cachefile=cachefile, cachedir=".", **lw_kwargs)

    # 自适应黑边：根据帧高度和用户期望padding计算最近标准分辨率的top/bottom分配
    padding_top, padding_bottom = _calculate_adaptive_padding(clip.height, padding)
    if padding_top > 0 or padding_bottom > 0:
        clip = core.std.AddBorders(clip, left=0, right=0, top=padding_top, bottom=padding_bottom)

    progress_set_message(_("Getting timecodes and keyframes from the index file"))
    result = info_from_lwindex(cachefile)

    # 向 Aegisub 报告硬件解码状态：
    # 1) hw_decode_requested: 是否请求了硬解（来自 prefer_hw）
    # 2) hw_decode_active: 实际激活状态
    #      1  = 硬解已激活
    #      0  = 确认使用软解
    #     -1  = 无法确定
    #
    # 检测策略：
    #   - 优先检查帧属性 _HWDecode / _HWAccel / _HardwareDecode（未来 lsmas 可能支持）
    #   - 若帧属性不可用，则统一报告 Unknown
    #
    # 原因：当前已知的 lsmas 分支都不会可靠上报“实际是否正在硬解”。
    # 即使是支持更多 prefer_hw 模式的分支，在上游文档中也允许回退到软解，
    # 因此不能仅凭“脚本成功执行”反推硬解一定已激活。
    hw_requested = int(lw_kwargs.get("prefer_hw", 0))
    result["hw_decode_requested"] = 1 if hw_requested > 0 else 0

    hw_active = 0
    if result["hw_decode_requested"]:
        # 优先尝试帧属性（最可靠，但当前 lsmas 两个分支均不设置）
        found_prop = False
        try:
            props = clip.get_frame(0).props
            for prop_name in ("_HWDecode", "_HWAccel", "_HardwareDecode"):
                if hasattr(props, prop_name):
                    hw_active = 1 if int(getattr(props, prop_name)) else 0
                    found_prop = True
                    break
        except Exception:
            pass

        if not found_prop:
            # 帧属性不可用时无法可靠判断实际解码路径，保守报告 Unknown
            hw_active = -1

    result["hw_decode_active"] = hw_active
    # 兼容旧脚本键名
    result["hw_decode"] = max(hw_active, 0)

    return clip, result


def make_keyframes(clip: vs.VideoNode, use_scxvid: bool = False,
                   resize_h: int = 360, resize_format: int = vs.GRAY8,
                   **kwargs: Any) -> List[int]:
    """
    Generates a list of keyframes from a clip, using either WWXD or Scxvid.

    :param clip:             Clip to process.
    :param use_scxvid:       Whether to use Scxvid. If False, the function uses WWXD.
    :param resize_h:         Height to resize the clip to before processing.
    :param resize_format:    Format to convert the clip to before processing.

    The remaining keyword arguments are passed on to the respective filter.
    """

    progress_set_message(_("Generating keyframes"))
    progress_set_progress(1)

    clip = core.resize.Bilinear(clip, width=resize_h * clip.width // clip.height, height=resize_h, format=resize_format)

    if use_scxvid:
        ensure_plugin("scxvid", "libscxvid", _("To use the keyframe generation, the scxvid plugin for VapourSynth must be installed"))
        clip = core.scxvid.Scxvid(clip, **kwargs)
    else:
        ensure_plugin("wwxd", "libwwxd64", _("To use the keyframe generation, the wwxdplugin for VapourSynth must be installed"))
        clip = core.wwxd.WWXD(clip, **kwargs)

    keyframes = {}
    done = 0
    def _cb(n: int, f: vs.VideoFrame) -> vs.VideoFrame:
        nonlocal done
        keyframes[n] = f.props._SceneChangePrev if use_scxvid else f.props.Scenechange # type: ignore
        done += 1
        if done % max(1, clip.num_frames // 200) == 0:
            progress_set_progress(100 * done / clip.num_frames)
        return f

    deque(clip.std.ModifyFrame(clip, _cb).frames(close=True), 0)
    progress_set_progress(100)
    return [n for n in range(clip.num_frames) if keyframes[n]]


def save_keyframes(filename: str, keyframes: List[int]):
    """
    Saves a list of keyframes in Aegisub's keyframe format v1 to a file with
    the given filename.
    """
    with open(filename, "w") as f:
        f.write("# keyframe format v1\n")
        f.write("fps 0\n")
        f.write("".join(f"{n}\n" for n in keyframes))


class GenKeyframesMode(Enum):
    NEVER = 0
    ALWAYS = 1
    ASK = 2


def ask_gen_keyframes(_: str) -> bool:
    from tkinter.messagebox import askyesno
    progress_set_message(_("Asking whether to generate keyframes"))
    progress_set_indeterminate()
    result = askyesno(_("Generate Keyframes"), _("No keyframes file was found for this video file.\nShould Aegisub detect keyframes from the video?\nThis will take a while."), default="no")
    progress_set_message("")
    return result


def get_keyframes(filename: str, clip: vs.VideoNode, fallback: str | List[int],
                  generate: GenKeyframesMode = GenKeyframesMode.ASK,
                  ask_callback: Callable = ask_gen_keyframes, **kwargs: Any) -> str | List[int]:
    """
    Looks for a keyframes file for the given filename.
    If no file was found, this function can generate a keyframe file for the given clip next
    to the given filename using WWXD or Scxvid (see the make_keyframes docstring).
    Whether or not keyframes are generated depends on the `generate` argument.
    Depending on the `generate` argument, the function will
    - always generate keyframes when no file was found
    - never generate keyframes when no file was found
        (and return the fallback keyframes instead)
    - show a dialog to ask the user whether keyframes should be
      generated or not
    Additional keyword arguments are passed on to make_keyframes.
    """
    progress_set_message(_("Looking for keyframes"))
    progress_set_indeterminate()

    kffilename = make_keyframes_filename(filename)

    if not os.path.exists(kffilename):
        if generate == GenKeyframesMode.NEVER:
            return fallback
        if generate == GenKeyframesMode.ASK and not ask_callback(filename):
            return fallback

        keyframes = make_keyframes(clip, **kwargs)
        save_keyframes(kffilename, keyframes)

    return kffilename


def check_audio(filename: str, **kwargs: Any) -> bool:
    """
    检查文件是否包含音频轨道。

    依次尝试以下方式探测：
      1. BestSource (bs) 插件
      2. L-SMASH Works (lsmas) 插件的 LWLibavAudioSource

    任一方式成功即返回 True。全部失败或插件不可用时返回 False。
    kwargs 仅传递给 BestSource。
    """
    progress_set_message(_("Checking if the file has an audio track"))
    progress_set_indeterminate()

    # 方式 1：BestSource
    try:
        ensure_plugin("bs", "BestSource", "")
        vs.core.bs.AudioSource(source=filename, **kwargs)
        return True
    except (AttributeError, vs.Error):
        pass

    # 方式 2：lsmas LWLibavAudioSource
    try:
        if hasattr(core, "lsmas") and hasattr(core.lsmas, "LWLibavAudioSource"):
            core.lsmas.LWLibavAudioSource(source=filename)
            return True
    except (AttributeError, vs.Error):
        pass

    return False
