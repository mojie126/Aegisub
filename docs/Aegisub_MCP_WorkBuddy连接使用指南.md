# Aegisub MCP × WorkBuddy 连接与使用指南

> **先领邀请，一起用 WorkBuddy：**
> 👉 [点击注册 / 加入 WorkBuddy](https://www.workbuddy.cn/events/invite?inviteCode=2r19rnu8z)
>
> 通过上面的邀请链接完成注册，你和邀请人都能获得相应权益；后续下载客户端、连接 Aegisub MCP 都在这同一个账号下完成。

本文面向**第一次**想把 Aegisub 的字幕编辑能力接到 WorkBuddy 的用户，从注册账号、安装客户端，到启用 Aegisub 的内置 MCP 服务、在 WorkBuddy 里连上它，再到真正开始用，一步步讲清楚。

---

## 0. 先看这一节：几条关键前提

| 项                             | 说明                                                                                                                                                                                                                           |
|-------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| **什么是 aeg MCP**               | 你本地这个 **Aegisub fork**（即带 MCP 支持的修改版）内置了一个 MCP（Model Context Protocol）服务。它把 Aegisub 的字幕读写、样式管理、时间轴、视频帧截图、音频波形/频谱等能力，以 52 个工具的形式暴露给 AI 客户端。                                                                                   |
| **不是官方原版**                    | 官方原版 Aegisub（aegisub.org）**没有**这个 MCP 服务。你必须使用**本 fork 3.5.3 及以上版本**的 `Aegisub.exe`（可从 [GitHub Release](https://github.com/mojie126/Aegisub/releases) 下载预编译版）。低于 3.5.3 的本 fork 旧版也可能缺失/不稳定，请认准 3.5.3+。                       |
| **架构**                        | 你正常双击启动 Aegisub（带 GUI）→ 它内部自动起一个 HTTP 服务，监听 `127.0.0.1:7878`，提供两个端点：`/mcp`（无状态 POST，WorkBuddy 使用）与 `/sse`（HTTP+SSE，供标准 MCP 客户端使用）。WorkBuddy 通过 HTTP 连接这个服务。**你和 AI 操作的是同一个 Aegisub 实例**，AI 的修改在 GUI 里实时可见，你也能随时 `Ctrl+Z` 撤销。 |
| **没有鉴权 / 不跨网**                | 服务只监听本机回环 `127.0.0.1`，不需要 API Key、不需要 mcp-remote 桥接，直接用 `http://127.0.0.1:7878/mcp` 连接即可。                                                                                                                                    |
| **获取带 MCP 的 Aegisub（3.5.3+）** | **推荐**从 [GitHub Release](https://github.com/mojie126/Aegisub/releases) 下载 **3.5.3 及以上** 的预编译版，开箱即用。**版本必须 ≥ 3.5.3**，低于此版本 MCP 功能缺失。                                                                                          |

---

## 1. 注册 WorkBuddy 账号

1. 打开上面的邀请链接：
   [https://www.workbuddy.cn/events/invite?inviteCode=2r19rnu8z](https://www.workbuddy.cn/events/invite?inviteCode=2r19rnu8z)
2. 按页面提示用微信 / 手机号等方式**注册并登录** WorkBuddy 账号。
3. 登录成功后，页面通常会引导你进入 WorkBuddy 工作台或跳转到下载页。

> 如果链接打不开，也可以直接访问官网 [https://www.workbuddy.cn](https://www.workbuddy.cn) 注册，再在「设置 / 邀请」里填入邀请码 `2r19rnu8z`。

---

## 2. 下载并安装 WorkBuddy 桌面客户端（Windows）

1. 在 WorkBuddy 官网或注册后的引导页，找到**下载**入口，选择 **Windows 版** 客户端下载（安装包一般为 `.exe`）。
2. 双击安装包，按提示**一路下一步**完成安装。
3. 安装完成后，桌面会出现 **WorkBuddy** 图标。

> 环境要求：Windows 系统、可联网（用于调用 AI 模型）、已注册并登录 WorkBuddy 账号。

---

## 3. 登录并进入工作台

1. 双击启动 **WorkBuddy**。
2. 用第 1 步注册的账号**登录**。
3. 进入主界面后，你会看到三块区域：
	- **左侧边栏**：任务列表、专家 / 技能 / 连接器入口（底部是你的头像）。
	- **中间对话区**：发任务、看回复。
	- **右侧结果区**：产物、文件、变更、预览。

到这里，WorkBuddy 这边就准备好了。

---

## 4. 准备 Aegisub：启用并启动内置 MCP 服务

### 4.1 确认用的是「带 MCP 的 Aegisub」

启动 **本 fork 3.5.3 及以上版本** 的 **Aegisub.exe**。最稳妥的方式是从 [GitHub Release](https://github.com/mojie126/Aegisub/releases) 下载预编译版。注意两点：**不要**用 aegisub.org 的原版（没有 MCP 能力，连上也是空工具）；**不要**用低于 3.5.3 的本 fork 旧版（MCP 功能缺失）。

### 4.2 在偏好设置里启用 MCP

Aegisub 的 MCP 服务默认是**关闭**的，需要手动打开一次：

1. 打开 Aegisub，进入菜单 **「选项」→「偏好设置」**（或 `app/options`）。
2. 找到 **App → MCP** 分组，把 **`Enabled` 改为 `true`**。
3. 确认以下默认值（一般不用改）：
	- `Host`：`127.0.0.1`
	- `Port`：`7878`
4. 点「确定」保存。

设置页界面如下图所示：勾选「启用 MCP 服务」，确认监听地址为 `127.0.0.1`、端口为 `7878`。

<img width="720" height="650" alt="Aegisub MCP 服务偏好设置" src="https://github.com/user-attachments/assets/c9e594b6-05e6-4611-9c3b-7580df83a951" />

### 4.3 启动 Aegisub（服务随之启动）

MCP 服务**不需要任何特殊启动参数**。只要 Aegisub 正常启动，且上一步启用了 MCP，它就会在后台监听 `127.0.0.1:7878`。

注意：Aegisub 实例**要一直开着**，WorkBuddy 才能连上。关掉 Aegisub，MCP 服务也就停了。

### 4.4 验证服务确实起来了

> 💡 **关于 curl**：Windows 10 / 11 已**内置** `curl.exe`（位于 `C:\Windows\System32`），**无需额外安装**。但 PowerShell 把 `curl` 当成了 `Invoke-WebRequest` 的别名，因此在 PowerShell 里必须写 `curl.exe`（带 `.exe` 后缀）才能调用真正的 curl；在 Git Bash / CMD 里直接写 `curl` 即可。下面统一用 `curl.exe`。

打开命令行（PowerShell、Git Bash、CMD 都行），执行：

```bash
curl.exe -s -X POST http://127.0.0.1:7878/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
```

- 返回一段 JSON（含 `protocolVersion`、`capabilities` 等）说明服务正常。
- 如果提示 `Connection refused` / 连不上：回去检查 4.2 是否启用、Aegisub 是否在运行、端口是不是 7878。

再列一下工具确认数量（应当返回 52 个工具）：

```bash
curl.exe -s -X POST http://127.0.0.1:7878/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
```

> 不想用 curl？用 PowerShell **自带的原生命令** `Invoke-WebRequest` 也能验证（不受 `curl` 别名影响）：
> ```powershell
> Invoke-WebRequest -Uri http://127.0.0.1:7878/mcp -Method POST `
>   -ContentType "application/json" `
>   -Body '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
> ```

---

## 5. 在 WorkBuddy 里连接 aeg MCP

有两种接法：**方式一**手动改 `mcp.json`（5.1–5.4）；**方式二**直接用 AI 对话配置（5.5，更省事，推荐先试）。

### 5.1 打开 MCP 配置入口

在 WorkBuddy 主界面：

1. 点**左侧边栏**的「**专家 · 技能 · 连接器**」。
2. 切到顶部的「**连接器**」标签页。
3. 点右上角的「**自定义连接器**」。
4. 在弹出的 MCP 服务管理窗口里，点右上角「**配置 MCP**」（也叫「编辑 mcp.json」）。

### 5.2 编辑 `mcp.json`，加入 aeg 远程服务

`mcp.json` 一般在用户配置目录 `~/.workbuddy/mcp.json`（Windows 上即 `C:\Users\<你的用户名>\.workbuddy\mcp.json`）。

在已有的 `mcpServers` 对象里**新增**一项 `aeg`（不要覆盖原有内容，不要写第二个 `mcpServers`）：

```json
{
	"mcpServers": {
		"aeg": {
			"type": "remote",
			"url": "http://127.0.0.1:7878/mcp",
			"disabled": false
		}
	}
}
```

> - 部分 WorkBuddy 版本也接受 `"type": "http"`，二者等价，本地回环用 `remote` 即可。
> - `url` 的值必须是 `http://127.0.0.1:7878/mcp` ，且末尾的 `/mcp` 路径不能省略。
> - 保存前检查一遍：双引号、逗号、花括号是否完整。建议用编辑器（如 VS Code）写，避免引号出错。

### 5.3 信任（Trust）这个 MCP 服务器

1. 保存配置后，点「**返回 MCP 列表**」。
2. 新加入的 `aeg` 会显示一个**信任确认**提示。确认名称 `aeg`、地址 `http://127.0.0.1:7878/mcp` 无误后，点「**信任**」。
3. 若列表里没看到 `aeg`，**重启一次 WorkBuddy** 再回来查看。

> ⚠️ 安全提示：只信任来源明确、地址已核对的 MCP。MCP 工具可能读取数据或执行操作，或未知地址有风险。本例 `127.0.0.1` 是本机服务，安全。

### 5.4 验证连接成功

1. **重启 WorkBuddy**（或新开一个任务会话），让配置生效。
2. 在对话输入框输入 `/mcp`，回车，查看 MCP 服务器与诊断信息——应当能看到 `aeg` 显示为已连接，并列出它的工具。
3. 也可以直接开一个对话，问一句：「列出 aeg 提供的工具」，AI 应当能调出 aeg 的工具清单（52 个）。

### 5.5 方式二：直接用 AI 对话配置 aeg MCP（更省事）

不想手动改 `mcp.json`？你可以直接开一个对话，用自然语言让 WorkBuddy 的 AI 帮你把 aeg MCP 配好：

> 帮我添加并连接本地的 Aegisub MCP 服务，类型选 remote，名字叫 aeg，地址是 `http://127.0.0.1:7878/mcp`

AI 会自动把 aeg 远程服务写进配置（等价于 5.2 手动写入 `mcp.json` 的那段）。配置写完后，WorkBuddy **不会弹出**信任确认，你需要到**连接器界面**手动点击「信任」（见 5.3），之后按 5.4 用 `/mcp` 验证即可。

> 提示：如果 AI 反问你地址或类型，把上面这行原样贴给它即可；配置写完后，请到 WorkBuddy 的**连接器界面**手动点击「信任」。只要 Aegisub 已启动且 MCP 已启用（见第 4 节），就能连上。

---

## 6. 开始使用 aeg MCP

### 6.1 工具命名规则

在 WorkBuddy 里，aeg 的工具名都带前缀 **`mcp__aeg__`**。例如：

- `mcp__aeg__open_file`
- `mcp__aeg__get_dialogue_lines`
- `mcp__aeg__update_subtitle_fields`
- `mcp__aeg__open_video`
- `mcp__aeg__get_video_frame`

你在对话里用自然语言下指令即可，WorkBuddy 会自动匹配并调用这些 `mcp__aeg__*` 工具；无需手写 JSON-RPC。

### 6.2 工具速查表（按用途分类）

下面列出最常用的工具：

| 类别          | 工具                                                                        | 作用                           |
|-------------|---------------------------------------------------------------------------|------------------------------|
| **工程 / 文件** | `open_file(path)`                                                         | 按路径打开字幕文件（无对话框）              |
|             | `save_file(path?)`                                                        | 保存当前字幕                       |
|             | `new_file()`                                                              | 新建空白字幕                       |
|             | `get_project_info()`                                                      | 看当前工程状态（文件名、行数、样式数、是否有视频/音频） |
| **字幕读写**    | `get_dialogue_lines(limit?, offset?, include_comment?)`                   | 读取字幕行                        |
|             | `search_dialogue(pattern, regex?)`                                        | 在字幕文本中搜索                     |
|             | `update_subtitle_fields(line_index[], fields)`                            | 改某行的时间/文本/样式/边距等             |
|             | `insert_subtitle_line(fields)` / `delete_subtitle_line(line_index[])`     | 插入 / 删除行                     |
|             | `shift_times(offset / start_offset+end_offset, line_indices?)`            | 整体或局部偏移时间轴                   |
|             | `set_selection(line_indices, active_line?)`                               | 设置网格选中行                      |
| **样式管理**    | `get_styles()` / `get_style(name)`                                        | 读取样式                         |
|             | `add_style(fields)` / `update_style(name, fields)` / `delete_style(name)` | 增改删样式                        |
| **视频 / 帧**  | `open_video(path)` / `open_audio(path)`                                   | 按路径打开视频/音频                   |
|             | `get_video_frame(frame?/ms?, raw?)`                                       | 取某帧截图（返回 PNG，AI 可直接看图）       |
|             | `save_video_frame(path, frame?, format?, raw?)`                           | 把某帧存成图片                      |
|             | `export_gif(path, start_frame, end_frame, quality?, scale_factor?)`       | 导出帧区间为 GIF                   |
|             | `frame_from_ms(ms)` / `ms_from_frame(frame)`                              | 毫秒 ↔ 帧号                      |
|             | `video_size()` / `keyframes()` / `get_frame()`                            | 视频尺寸 / 关键帧 / 当前帧             |
| **音频**      | `get_audio_waveform(start_ms, end_ms, points?)`                           | 音频波形（切轴对齐用）                  |
|             | `get_audio_spectrum(ms, fft_size?)`                                       | 音频频谱（语音/音乐判别）                |
|             | `play_audio(start_ms, end_ms?)`                                           | 试听某段音频                       |
| **文本 / 时间** | `time_to_ms(time)` / `ms_to_time(ms)`                                     | ASS 时间字符串 ↔ 毫秒               |
|             | `strip_tags(text)`                                                        | 剥离 ASS 标签返回纯文本               |
|             | `character_count(text, 忽略开关?)`                                            | 字符计数与最长行                     |
|             | `vsmod_syntax(tag?, source?)`                                             | 查询 VSFilterMod 扩展标签语法        |
| **通用网关**    | `run_command(command)`                                                    | 执行 Aegisub 内置的 272 个命令之一     |
|             | `list_commands()`                                                         | 列出全部命令名 + 是否阻塞 + 对应专用工具      |

### 6.3 典型工作流示例（直接照抄给 AI 的对话）

连接成功后，在 WorkBuddy 对话里这样用：

**示例 A：打开工程并读取字幕**
> 请用 aeg 打开 `E:\video_test\xxx.ass`，然后列出前 20 行 dialogue 的内容，告诉我总行数和样式数。

AI 会依次调用 `mcp__aeg__open_file` → `mcp__aeg__get_project_info` → `mcp__aeg__get_dialogue_lines`，你也能在 Aegisub GUI 里看到文件被打开。

**示例 B：逐帧精修特效（进阶）**
> 用 aeg 打开 `xxx.ass` 和对应视频 `xxx.mkv`，把第 425 帧存成参考图，告诉我该帧的画面尺寸、脚本分辨率，以及是否分辨率不匹配。

AI 会调用 `open_file` → `open_video` → `save_video_frame`，并返回 `video_width/height`、`script_width/height`、`resolution_mismatch`。后续可继续让 AI 用 `update_subtitle_fields` 逐行写入与画面严丝合缝的 `\pos` / `\t` / `\move` 等参数。

**示例 C：改样式**
> 把 Default 样式的字号改成 60，描边改成 3，并新增一个叫 `Title` 的样式（红色粗体）。

AI 调用 `update_style` + `add_style`，修改实时反映到 Aegisub 的样式面板。

### 6.4 与 GUI 实时联动

- AI 的每一步修改都会**实时**写进你正在看的 Aegisub 窗口。
- 不满意？直接在 Aegisub 里 `Ctrl+Z` 撤销，或手动改，AI 下一轮能读到最新状态。
- 多个 AI 客户端可以同时连同一个 Aegisub（比如 WorkBuddy + Cursor 一起用）。

---

## 7. 常见问题排障

| 现象                                  | 可能原因                          | 处理                                                                    |
|-------------------------------------|-------------------------------|-----------------------------------------------------------------------|
| WorkBuddy 里 `aeg` 显示未连接 / 工具为空      | Aegisub 没开，或 MCP 没启用          | 确认 Aegisub 正在运行且偏好设置中 `App/MCP/Enabled=true`；用第 4.4 节的 curl 验证服务是否已启动 |
| 连接被拒 / `Connection refused`         | 端口不是 7878，或 Host 不是 127.0.0.1 | 检查 Aegisub 偏好设置里的 `Port`/`Host`，确保 `mcp.json` 的 `url` 与之完全一致          |
| 保存 `mcp.json` 后列表里看不到 `aeg`         | 配置未生效                         | **重启 WorkBuddy** 再进连接器列表；检查 JSON 格式是否有错（引号/逗号/花括号）                    |
| `/mcp` 里 `aeg` 报红 / 信任后不出现工具        | 未点「信任」，或地址不对                  | 回到 MCP 列表点「信任」；核对 `url` 必须是 `http://127.0.0.1:7878/mcp`               |
| 调用工具时返回 502 / 连接重置                  | Aegisub 进程崩溃（见下）              | 重启 Aegisub，重新连接；MCP 无法从进程崩溃中恢复，只能重启                                   |
| `run_command` 返回 `executed:false`   | 前置条件不满足（如没有视频/音频/选中行）         | 先 `open_video` / `open_file` / `set_selection` 满足前置条件                 |
| `run_command` 报 `did you mean: ...` | 命令名写错                         | 先调 `list_commands` 拿到准确命令名                                            |

---

## 8. 注意事项与限制

1. **阻塞类命令别用 `run_command` 调**：像 `subtitle/open`、`time/shift`、`edit/font`、`video/open` 等会弹 GUI 对话框，会卡住 AI 调用。优先用专用工具：`open_file`、`open_video`、`save_file`、`shift_times`、`add_style` 等。权威阻塞清单以 `list_commands` 返回的 `blocking:true` 为准。
2. **`.txt` / `.sub` 不支持**：`open_file` 读 `.txt`/`.sub`、或 `save_file` 存 `.sub` 会被直接拒绝（需要交互式 FPS 对话框）。先转成 `.ass` 再操作。
3. **多音轨文件会弹轨道选择框**：打开多轨 `.mkv`/`.mka` 时，Aegisub GUI 会弹「选择音轨/视频轨」框，需要你在 GUI 里手动选，MCP 侧会等待。
4. **进程崩溃只能重启**：MCP 只能拦截可抛出的 C++ 异常；若 Aegisub 发生进程级崩溃（如访问违例 `0xC0000005`），MCP 连接会断开且无法恢复，**只能重启 Aegisub**。（本 fork 已修复已知的 `new_file`/打开含 Garbage 段文件等崩溃。）
5. **Aegisub 要一直开着**：关掉 Aegisub，MCP 服务随之停止，WorkBuddy 那边会断开。
6. **非交互模式**：`open_video`/`open_audio` 等以非交互方式加载，失败不会弹错误框，而是把原因作为 JSON-RPC error 返回给 AI，由 AI 处理。

---

> 总结：注册 WorkBuddy（用邀请码 `2r19rnu8z`）→ 装好客户端 → 在 Aegisub 偏好设置启用 MCP 并启动 Aegisub → 在 WorkBuddy「自定义连接器 → 配置 MCP」里加入 `aeg` 远程服务并信任 → 用 `/mcp` 验证 → 直接对话让 AI 操作字幕即可。
