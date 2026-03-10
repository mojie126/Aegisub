# Aegisub 热键命令参考

本文档列出所有可在"设置 → 热键"中配置的命令及其功能说明。

## 快速索引

- [应用程序](#应用程序-app)
- [音频](#音频-audio)
- [自动化](#自动化-automation)
- [编辑](#编辑-edit)
- [字幕网格](#字幕网格-grid)
- [帮助](#帮助-help)
- [关键帧](#关键帧-keyframe)
- [播放](#播放-play)
- [最近文件](#最近文件-recent)
- [字幕文件](#字幕文件-subtitle)
- [时间调整](#时间调整-time)
- [时间码](#时间码-timecode)
- [工具](#工具-tool)
- [视频](#视频-video)
- [可视化工具](#可视化工具-visual-tools)

---

## 应用程序 (app)

| 命令 | 功能 |
|------|------|
| `app/about` | 关于 Aegisub |
| `app/bring_to_front` | 将所有打开的文档窗口置于最前 |
| `app/clear_autosave` | 清除自动保存和自动备份文件 |
| `app/clear_cache` | 清除音频和视频索引缓存文件 |
| `app/clear_log` | 清除累积的日志文件 |
| `app/clear_recent` | 清除所有最近打开的文件列表 |
| `app/display/audio_subs` | 仅显示音频和字幕网格 |
| `app/display/full` | 显示音频、视频和字幕网格 |
| `app/display/subs` | 仅显示字幕网格 |
| `app/display/video_subs` | 仅显示视频和字幕网格 |
| `app/exit` | 退出应用程序 |
| `app/language` | 选择 Aegisub 界面语言 |
| `app/log` | 查看事件日志 |
| `app/maximize` | 最大化活动窗口 |
| `app/minimize` | 最小化活动窗口 |
| `app/new_window` | 打开一个新的应用程序窗口 |
| `app/options` | 配置 Aegisub |
| `app/toggle/global_hotkeys` | 切换全局热键覆盖（Medusa 模式） |
| `app/toggle/toolbar` | 切换主工具栏显示/隐藏 |
| `app/updates` | 检查是否有新版本的 Aegisub 可用 |

## 音频 (audio)

| 命令 | 功能 |
|------|------|
| `audio/close` | 关闭当前打开的音频文件 |
| `audio/commit` | 提交任何待处理的音频时间更改 |
| `audio/commit/default` | 提交音频时间更改并将下一行重置为默认时间 |
| `audio/commit/next` | 提交音频时间更改并移动到下一行 |
| `audio/commit/stay` | 提交音频时间更改并停留在当前行 |
| `audio/go_to` | 滚动音频显示以居中显示当前音频选择 |
| `audio/go_to/end` | 滚动音频显示以居中显示当前音频选择的结束位置 |
| `audio/go_to/start` | 滚动音频显示以居中显示当前音频选择的开始位置 |
| `audio/karaoke` | 切换卡拉OK模式 |
| `audio/open` | 打开音频文件 |
| `audio/open/blank` | 打开 2 小时 30 分钟的空白音频（用于调试） |
| `audio/open/noise` | 打开 2 小时 30 分钟的噪音音频（用于调试） |
| `audio/open/video` | 从当前视频文件打开音频 |

| `audio/opt/autocommit` | 自动提交所有更改 |
| `audio/opt/autonext` | 提交时自动转到下一行 |
| `audio/opt/autoscroll` | 自动滚动音频显示到选定行 |
| `audio/opt/spectrum` | 频谱分析器模式 |
| `audio/opt/vertical_link` | 链接垂直缩放和音量滑块 |
| `audio/play/current` | 播放当前音频选择（忽略播放时所做的更改） |
| `audio/play/line` | 播放当前行的音频 |
| `audio/play/selection` | 播放音频选择直到结束 |
| `audio/play/selection/after` | 播放选择后 500 毫秒 |
| `audio/play/selection/before` | 播放选择前 500 毫秒 |
| `audio/play/selection/begin` | 播放选择的前 500 毫秒 |
| `audio/play/selection/end` | 播放选择的最后 500 毫秒 |
| `audio/play/to_end` | 从选择开始播放到文件结束 |
| `audio/play/toggle` | 播放音频选择或停止 |
| `audio/reload` | 重新加载当前音频文件 |
| `audio/save/clip` | 保存选定行的音频片段 |
| `audio/scroll/left` | 向左滚动音频显示 |
| `audio/scroll/right` | 向右滚动音频显示 |
| `audio/stop` | 停止音频和视频播放 |
| `audio/view/spectrum` | 将音频显示为频率-功率频谱图 |
| `audio/view/waveform` | 将音频显示为线性振幅图 |

## 自动化 (automation)

| 命令 | 功能 |
|------|------|
| `am/manager` | 打开自动化管理器 |
| `am/meta` | 打开自动化管理器（Ctrl: 重新扫描自动加载文件夹，Ctrl+Shift: 重新扫描并重新加载所有脚本） |
| `am/reload` | 重新加载所有自动化脚本并重新扫描自动加载文件夹 |
| `am/reload/autoload` | 重新扫描自动化自动加载文件夹 |

## 编辑 (edit)

| 命令 | 功能 |
|------|------|
| `edit/clear` | 清除选定的文本 |
| `edit/clear/text` | 清除文本字段 |
| `edit/color/outline` | 在光标位置设置轮廓颜色 (\\3c) |
| `edit/color/primary` | 在光标位置设置主填充颜色 (\\c) |
| `edit/color/secondary` | 在光标位置设置次要（卡拉OK）填充颜色 (\\2c) |
| `edit/color/shadow` | 在光标位置设置阴影颜色 (\\4c) |
| `edit/find_replace` | 在字幕中查找和替换文字 |
| `edit/font` | 选择字体和大小 |
| `edit/insert_original` | 插入原始文本 |
| `edit/line/copy` | 将字幕复制到剪贴板 |
| `edit/line/cut` | 剪切字幕 |
| `edit/line/delete` | 删除当前选定的行 |
| `edit/line/duplicate` | 复制选定的行 |
| `edit/line/join/as_karaoke` | 将选定的行合并为一行，作为卡拉OK |
| `edit/line/join/concatenate` | 将选定的行合并为一行，连接文本 |
| `edit/line/join/keep_first` | 将选定的行合并为一行，保留第一行的文本并丢弃其余文本 |
| `edit/line/paste` | 粘贴字幕 |
| `edit/line/paste/over` | 粘贴字幕覆盖其他字幕 |
| `edit/line/recombine` | 在拆分和合并后重新组合字幕 |
| `edit/line/split/after` | 在当前帧之后拆分行 |
| `edit/line/split/before` | 在当前帧之前拆分行 |
| `edit/line/split/by_karaoke` | 使用卡拉OK时间将行拆分为多个较小的行 |
| `edit/line/split/estimate` | 在光标处拆分（估计时间） |
| `edit/line/split/preserve` | 在光标处拆分（保留时间） |
| `edit/line/split/video` | 在视频帧处拆分 |
| `edit/redo` | 重做上次撤消的操作 |
| `edit/revert` | 将文件恢复到上次保存的版本 |
| `edit/style/bold` | 切换当前选择或光标位置的粗体 (\\b) |
| `edit/style/italic` | 切换当前选择或光标位置的斜体 (\\i) |
| `edit/style/strikeout` | 切换当前选择或光标位置的删除线 (\\s) |
| `edit/style/underline` | 切换当前选择或光标位置的下划线 (\\u) |
| `edit/text_ltr` | 将文本方向设置为从左到右 |
| `edit/text_rtl` | 将文本方向设置为从右到左 |
| `edit/undo` | 撤消上次操作 |

## 字幕网格 (grid)

| 命令 | 功能 |
|------|------|
| `grid/fold/clear` | 移除选定行周围的折叠 |
| `grid/fold/clear_all` | 移除所有折叠 |
| `grid/fold/close` | 折叠选定行周围的折叠 |
| `grid/fold/close_all` | 关闭所有折叠 |
| `grid/fold/create` | 创建新折叠，将选定的行折叠成一组 |
| `grid/fold/open` | 展开选定行下的折叠 |
| `grid/fold/open_all` | 打开所有折叠 |
| `grid/fold/toggle` | 打开或关闭选定行周围的折叠 |
| `grid/line/next` | 移动到下一个字幕行 |
| `grid/line/next/create` | 移动到下一个字幕行，如果需要则创建新行 |
| `grid/line/prev` | 移动到上一行 |
| `grid/move/down` | 将选定的行向下移动一行 |
| `grid/move/up` | 将选定的行向上移动一行 |
| `grid/sort/actor` | 按演员名称对所有字幕排序 |
| `grid/sort/actor/selected` | 按演员名称对选定的字幕排序 |
| `grid/sort/effect` | 按特效对所有字幕排序 |
| `grid/sort/effect/selected` | 按特效对选定的字幕排序 |
| `grid/sort/end` | 按结束时间对所有字幕排序 |
| `grid/sort/end/selected` | 按结束时间对选定的字幕排序 |
| `grid/sort/layer` | 按图层编号对所有字幕排序 |
| `grid/sort/layer/selected` | 按图层编号对选定的字幕排序 |
| `grid/sort/start` | 按开始时间对所有字幕排序 |
| `grid/sort/start/selected` | 按开始时间对选定的字幕排序 |
| `grid/sort/style` | 按样式名称对所有字幕排序 |
| `grid/sort/style/selected` | 按样式名称对选定的字幕排序 |
| `grid/sort/text` | 按文本（包括样式标签）对所有字幕排序 |
| `grid/sort/text/selected` | 按文本（包括样式标签）对选定的字幕排序 |
| `grid/sort/text_stripped` | 按去除标签的文本对所有字幕排序 |
| `grid/sort/text_stripped/selected` | 按去除标签的文本对选定的字幕排序 |
| `grid/swap` | 交换两个选定的行 |
| `grid/tag/cycle_hiding` | 循环切换标签隐藏模式 |
| `grid/tags/hide` | 在字幕网格中隐藏覆盖标签 |
| `grid/tags/show` | 在字幕网格中显示完整的覆盖标签 |
| `grid/tags/simplify` | 用简化的占位符替换字幕网格中的覆盖标签 |

## 帮助 (help)

| 命令 | 功能 |
|------|------|
| `help/bugs` | 访问 Aegisub 的错误跟踪器以报告错误和请求新功能 |
| `help/contents` | 帮助主题 |
| `help/irc` | 访问 Aegisub 的官方 IRC 频道 |
| `help/video` | 打开可视化排版的手册页面 |
| `help/website` | 访问 Aegisub 的官方网站 |

## 关键帧 (keyframe)

| 命令 | 功能 |
|------|------|
| `keyframe/close` | 丢弃当前加载的关键帧并使用视频中的关键帧（如果有） |
| `keyframe/open` | 打开关键帧列表文件 |
| `keyframe/save` | 将当前关键帧列表保存到文件 |

## 播放 (play)

| 命令 | 功能 |
|------|------|
| `play/toggle/av` | 在加载视频时切换音频和视频的播放 |

## 最近文件 (recent)

| 命令 | 功能 |
|------|------|
| `recent/audio/` | 打开最近的音频 |
| `recent/keyframes/` | 打开最近的关键帧 |
| `recent/subtitle/` | 打开最近的字幕 |
| `recent/timecodes/` | 打开最近的时间码 |
| `recent/video/` | 打开最近的视频 |

## 字幕文件 (subtitle)

| 命令 | 功能 |
|------|------|
| `subtitle/apply/mocha` | 将 Mocha 运动追踪数据应用到当前字幕条目 |
| `subtitle/attachment` | 打开附件管理器对话框 |
| `subtitle/close` | 关闭 |
| `subtitle/find` | 在字幕中搜索文本 |
| `subtitle/find/next` | 查找上次搜索的下一个匹配项 |
| `subtitle/insert/after` | 在当前行之后插入新行 |
| `subtitle/insert/after/videotime` | 在当前行之后插入新行，从视频时间开始 |
| `subtitle/insert/before` | 在当前行之前插入新行 |
| `subtitle/insert/before/videotime` | 在当前行之前插入新行，从视频时间开始 |
| `subtitle/new` | 新建字幕 |
| `subtitle/open` | 打开字幕文件 |
| `subtitle/open/autosave` | 打开 Aegisub 自动保存的文件的先前版本 |
| `subtitle/open/charset` | 使用特定文件编码打开字幕文件 |
| `subtitle/open/video` | 从当前视频文件打开字幕 |
| `subtitle/properties` | 打开脚本属性窗口 |
| `subtitle/save` | 保存当前字幕 |
| `subtitle/save/as` | 用另一个名称保存字幕 |
| `subtitle/select/all` | 选择所有对话行 |
| `subtitle/select/visible` | 选择当前视频帧上可见的所有对话行 |
| `subtitle/spellcheck` | 打开拼写检查器 |

## 时间调整 (time)

| 命令 | 功能 |
|------|------|
| `time/align` | 通过关键点将字幕对齐到视频 |
| `time/continuous/end` | 将行的结束时间更改为下一行的开始时间 |
| `time/continuous/start` | 将行的开始时间更改为上一行的结束时间 |
| `time/frame/current` | 移动选择，使活动行从当前帧开始 |
| `time/lead/both` | 为选定的行添加前导和后导 |
| `time/lead/in` | 为选定的行添加前导时间 |
| `time/lead/out` | 为选定的行添加后导时间 |
| `time/length/decrease` | 减少当前时间单位的长度 |
| `time/length/decrease/shift` | 减少当前时间单位的长度并移动后续项目 |
| `time/length/increase` | 增加当前时间单位的长度 |
| `time/length/increase/shift` | 增加当前时间单位的长度并移动后续项目 |
| `time/next` | 下一行或音节 |
| `time/prev` | 上一行或音节 |
| `time/shift` | 按时间或帧移动字幕 |
| `time/snap/end_video` | 将选定字幕的结束设置为当前视频帧 |
| `time/snap/scene` | 将字幕的开始和结束设置为当前视频帧周围的关键帧 |
| `time/snap/start_video` | 将选定字幕的开始设置为当前视频帧 |
| `time/start/decrease` | 向后移动当前时间单位的开始时间 |
| `time/start/increase` | 向前移动当前时间单位的开始时间 |

## 时间码 (timecode)

| 命令 | 功能 |
|------|------|
| `timecode/close` | 关闭当前打开的时间码文件 |
| `timecode/open` | 打开 VFR 时间码 v1 或 v2 文件 |
| `timecode/save` | 保存 VFR 时间码 v2 文件 |

## 工具 (tool)

| 命令 | 功能 |
|------|------|
| `tool/assdraw` | 启动 ASSDraw3 矢量绘图工具 |
| `tool/close_subtitles` | 关闭字幕和相关资源 |
| `tool/export` | 以不同格式保存字幕副本或对其应用处理 |
| `tool/font_collector` | 打开字体收集器 |
| `tool/line/select` | 根据定义的条件选择行 |
| `tool/resampleres` | 重新采样字幕以在不同的脚本分辨率下保持其当前外观 |
| `tool/style/assistant` | 打开样式助手 |
| `tool/style/manager` | 打开样式管理器 |
| `tool/styling_assistant/commit` | 提交更改并移动到下一行 |
| `tool/styling_assistant/preview` | 提交更改并停留在当前行 |
| `tool/time/kanji` | 打开日文汉字计时器复制器 |
| `tool/time/postprocess` | 对字幕时间进行后处理，添加前导和后导，将时间对齐到场景变化等 |
| `tool/translation_assistant` | 打开翻译助手 |
| `tool/translation_assistant/commit` | 提交更改并移动到下一行 |
| `tool/translation_assistant/insert_original` | 插入未翻译的文本 |
| `tool/translation_assistant/next` | 移动到下一行而不提交更改 |
| `tool/translation_assistant/prev` | 移动到上一行而不提交更改 |
| `tool/translation_assistant/preview` | 提交更改并停留在当前行 |

## 视频 (video)

| 命令 | 功能 |
|------|------|
| `video/aspect/cinematic` | 强制视频为 2.35 宽高比 |
| `video/aspect/custom` | 强制视频为自定义宽高比 |
| `video/aspect/default` | 使用视频的原始宽高比 |
| `video/aspect/full` | 强制视频为 4:3 宽高比 |
| `video/aspect/wide` | 强制视频为 16:9 宽高比 |
| `video/close` | 关闭当前打开的视频文件 |
| `video/copy_coordinates` | 将鼠标在视频上的当前坐标复制到剪贴板 |
| `video/detach` | 将视频显示从主窗口分离，在单独的窗口中显示 |
| `video/details` | 显示视频详细信息 |
| `video/focus_seek` | 在视频滑块和之前有焦点的内容之间切换焦点 |
| `video/frame/copy` | 将当前显示的帧复制到剪贴板 |
| `video/frame/copy/raw` | 将当前显示的帧（不含字幕）复制到剪贴板 |
| `video/frame/copy/subs` | 仅将当前显示帧的字幕复制到剪贴板 |
| `video/frame/next` | 跳到下一帧 |
| `video/frame/next/boundary` | 跳到下一个字幕边界 |
| `video/frame/next/keyframe` | 跳到下一个关键帧 |
| `video/frame/next/large` | 快速向前跳转 |
| `video/frame/prev` | 跳到上一帧 |
| `video/frame/prev/boundary` | 跳到上一个字幕边界 |
| `video/frame/prev/keyframe` | 跳到上一个关键帧 |
| `video/frame/prev/large` | 快速向后跳转 |
| `video/frame/save` | 将当前显示的帧保存为 PNG 图像 |
| `video/frame/save/export` | 将视频帧导出为 JPEG 图像序列 |
| `video/frame/save/raw` | 将当前显示的帧（不含字幕）保存为 PNG 图像 |
| `video/frame/save/subs` | 仅将当前显示帧的字幕保存为 PNG 图像 |
| `video/import/image_sequence` | 使用 \\1img 标签将 PNG 图像文件导入为字幕行 |
| `video/jump` | 跳转到特定帧或时间 |
| `video/jump/end` | 跳转到视频结尾 |
| `video/jump/start` | 跳转到视频开头 |
| `video/open` | 打开视频文件 |
| `video/open/dummy` | 打开空白虚拟视频 |
| `video/open/image` | 打开图像序列作为视频 |
| `video/opt/autoscroll` | 切换自动将视频定位到活动行 |
| `video/pan_reset` | 将视频平移重置为居中 |
| `video/play` | 从此位置开始播放视频 |
| `video/play/line` | 播放当前行 |
| `video/reload` | 重新加载当前视频文件 |
| `video/save/clip` | 将选定行的视频帧导出为 JPEG 图像序列 |
| `video/save/gif` | 将视频片段导出为 GIF 动画 |
| `video/show_overscan` | 在视频上显示遮罩，指示可能在电视上被裁剪的区域 |
| `video/stop` | 停止视频播放 |
| `video/subtitles_provider/cycle` | 循环切换可用的字幕提供程序 |
| `video/subtitles_provider/reload` | 重新加载当前字幕提供程序 |
| `video/zoom/100` | 将缩放设置为 100% |
| `video/zoom/200` | 将缩放设置为 200% |
| `video/zoom/50` | 将缩放设置为 50% |
| `video/zoom/in` | 放大视频 |
| `video/zoom/out` | 缩小视频 |

## 可视化工具 (visual tools)

| 命令 | 功能 |
|------|------|
| `video/tool/clip` | 将字幕裁剪到矩形 |
| `video/tool/cross` | 标准模式，双击设置位置 |
| `video/tool/drag` | 拖动字幕 |
| `video/tool/perspective` | 旋转和倾斜字幕以适应给定四边形的透视 |
| `video/tool/perspective/grid` | 切换在可视化透视工具中显示 3D 网格 |
| `video/tool/perspective/lock_outer` | 当周围平面也可见时，切换锁定哪个四边形 |
| `video/tool/perspective/orgmode/center` | 将 \\org 放在透视四边形的中心 |
| `video/tool/perspective/orgmode/cycle` | 循环切换三种 \\org 模式 |
| `video/tool/perspective/orgmode/keep` | 固定 \\org 的位置 |
| `video/tool/perspective/orgmode/nofax` | 找到 \\fax 可以为零的 \\org 值（如果可能） |
| `video/tool/perspective/plane` | 切换显示环境 3D 平面的第二个四边形 |
| `video/tool/rotate/xy` | 在 X 和 Y 轴上旋转字幕 |
| `video/tool/rotate/z` | 在 Z 轴上旋转字幕 |
| `video/tool/scale` | 在 X 和 Y 轴上缩放字幕 |
| `video/tool/vclip/bicubic` | 附加贝塞尔双三次曲线 |
| `video/tool/vclip/convert` | 在直线和双三次曲线之间转换线段 |
| `video/tool/vclip/drag` | 拖动控制点 |
| `video/tool/vclip/freehand` | 绘制手绘形状 |
| `video/tool/vclip/freehand_smooth` | 绘制平滑的手绘形状 |
| `video/tool/vclip/insert` | 插入控制点 |
| `video/tool/vclip/line` | 附加直线 |
| `video/tool/vclip/remove` | 移除控制点 |
| `video/tool/vector_clip` | 将字幕裁剪到矢量区域 |

---

## 统计信息

- **总命令数**: 280+
- **分类数**: 15 个主要类别
- **支持语言**: 简体中文

## 使用说明

1. 在 Aegisub 中打开"设置 → 热键"
2. 在搜索框中输入命令名称（如 `video/play`）
3. 点击命令并设置您喜欢的快捷键组合
4. 点击"确定"保存设置

## 注意事项

- 部分命令需要特定条件才能使用（如视频已加载、有选中行等）
- 某些命令是切换型命令，重复执行会在两种状态间切换
- 最近文件命令（`recent/*`）后面会自动添加数字索引（0-15）
