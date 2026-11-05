# QScreenCut

Windows 上的剪贴板历史 + 截图标注工具，Qt 6 Widgets 实现。

## 功能

**剪贴板历史**

- 自动记录文本、图片、文件（含多选）三类内容，文本超长截断、图片只存缩略图
- 列表直接显示摘要和缩略图：图片、以及含图片/视频的文件条目显示第一张缩略图，
  其它类型显示带后缀的彩色徽章
- 点开详情看完整内容，支持一键复制回剪贴板、单条删除、清空历史
- 列表分页加载，几千条记录也不会一次性塞进界面

**截图**

- 全局热键唤起（默认 `Ctrl+Alt+A`，可在设置里改），多显示器分别抓屏，混合 DPI 不错位
- 选区可拖动、八方向拉伸，标注工具：画笔、矩形、椭圆、三角形、直线、文字、橡皮擦
- 文字标注支持独立字号与颜色，写完之后整条可拖动调整位置
- 结果可复制、另存为 PNG/JPEG，或以「置顶窗口」贴在屏幕上（可拖动、缩放、保存）

**识别与翻译**

- 截图后可直接提取文字，或翻译成指定语言
- 两条通道：在线大模型（OpenAI 兼容接口）优先，未配置时退回百度图片翻译


## 系统要求

- Windows 10 / 11 x64
- Qt 6.11（`msvc2022_64` 套件）
- Visual Studio 2022 / 2026 + Qt VS Tools（工程文件为 `QScreenCut.slnx`，x64 Debug/Release）
- 编译选项需 `/std:c++20`（Qt 6.11 在 C++17 下会报 C2968）

## 依赖获取

Qt 与 QWindowKit 都是外部依赖，不包含在本仓库里。

**Qt**：安装 [Qt 6.11](https://www.qt.io/download) 并选中 MSVC 2022 64-bit 组件，
然后在 Visual Studio 的 Qt VS Tools 里把版本指向 `D:\Qt\6.11.0\msvc2022_64`（换成你的实际路径）。

**QWindowKit**（无边框窗口框架，1.5.1，Apache-2.0）需要先拉源码再编译：

```bash
git clone https://github.com/stdware/qwindowkit.git third_party/src-qwindowkit
third_party\build-qwindowkit.bat Debug
third_party\build-qwindowkit.bat Release
```

脚本会调用 CMake + Ninja 产出 `third_party/qwindowkit/{Debug,Release}`（include + 静态库），
工程通过 `$(Configuration)` 自动选用对应的一份。

## 编译

1. 用 Visual Studio 打开 `QScreenCut.slnx`
2. 确认 Qt 版本选择 6.11（msvc2022_64），平台选 x64
3. 生成 Debug 或 Release

也可以只用命令行验证能否编过：

```bash
third_party\verify-build.bat
```

（脚本手工调用 moc/rcc/cl/link，产物在 `build-verify` 目录，不影响 Visual Studio 的构建）

> 注意：`third_party` 下的两个脚本把 MSVC / Windows SDK / Qt 的路径写死成了开发机的绝对路径
> （`D://Microsoft Visual Studio\...`、`D://Qt//6.11.0//msvc2022_64`），换机器运行前先改成你自己的路径。

## 数据与隐私

程序自身是绿色免安装的，所有数据都在 exe 同级的 `data` 目录里：

| 文件 | 内容 |
| --- | --- |
| `data/settings.ini` | 截图快捷键、开机自启、大模型与百度翻译的接口配置（**含明文密钥**） |
| `data/clipboard.db` | 剪贴板历史：文本原文、图片缩略图、文件路径 |
| `logs/*.log` | 运行日志，按天分文件，保留 7 天 |

`data/`、`logs/`、`.workbuddy/`、各类编译产物都已在 `.gitignore` 中排除，
提交前请确认没有把本地配置或剪贴板内容带进仓库。

## 目录结构

```
QScreenCut.cpp/.h        主窗口：历史列表、托盘、全局热键、剪贴板监听、自绘标题栏
ClipboardStore.cpp/.h    剪贴板历史的 SQLite 存取
ScreenshotOverlay.cpp/.h 截图覆盖层：选区、蒙版、工具条、标注绘制
HistoryListWidget.cpp/.h 历史列表（缩放时先冻结画面，避免整块重绘掉帧）
EntryDetailDialog.cpp/.h 条目详情弹窗（文本全文 / 图片预览 / 文件预览网格）
PinnedImageWindow.cpp/.h 置顶图片窗口
SettingsWindow.cpp/.h    设置界面：快捷键录制、开机自启、接口配置
AppSettings.cpp/.h       配置单例（ini + 注册表自启项）
AiClient.cpp/.h          在线大模型识别/翻译
BaiduTranslateClient.*   百度图片翻译
ThumbnailUtils.cpp/.h    系统缩略图接口（图片/视频预览）
IconUtils.cpp/.h         图标着色（SVG 渲染后按目标颜色填充）
AppLogger.cpp/.h         日志落盘
images/                  界面图标（SVG/PNG），全部打进 QScreenCut.qrc
third_party/             QWindowKit 构建脚本、命令行编译验证脚本
```

## 开源依赖

| 组件 | 版本 | 许可证 | 地址 |
| --- | --- | --- | --- |
| [Qt](https://www.qt.io/) | 6.11 | LGPL-3.0（亦可使用商业许可） | https://code.qt.io/cgit/qt/ |
| [QWindowKit](https://github.com/stdware/qwindowkit) | 1.5.1 | Apache-2.0 | https://github.com/stdware/qwindowkit |
| [Tabler Icons](https://github.com/tabler/tabler-icons) | — | MIT | https://github.com/tabler/tabler-icons |

图标来源的详细说明见 `images/fileicons/README-ICONS.md`：大部分 SVG 来自 Tabler Icons（MIT，
许可证见 `images/fileicons/LICENSE`），其中 `clipboard-line.svg` / `clipboard-fill.svg`
取自阿里巴巴矢量图标库（iconfont.cn），该平台图标的授权由上传者设定，如需商用请自行确认或替换。

项目以动态链接方式使用 Qt（发布目录里是独立的 Qt DLL），二进制分发时可按 LGPL-3.0
保留替换 Qt 库的能力。

## 许可证

本项目使用 MIT 许可证，全文见 [LICENSE](LICENSE)。
