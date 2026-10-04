# RightDial（右键轮盘）

轻量级 Windows 全局轮盘菜单：**按住鼠标右键拖动即可唤出**，可装载自定义组合快捷键（如 Ctrl+V）、文件/文件夹与网址，外观高度可自定义。

- 单文件 `RightDial.exe` 约 **1.3 MB**（含内置 Lucide 图标库），常驻**私有内存约 2 MB**（工作集约 10 MB 含共享 DLL），远低于 10 MB 目标
- 原生 Win32 + Direct2D 自绘，无任何运行时依赖（静态链接 CRT）

## 功能

### 手势
| 操作 | 效果 |
|---|---|
| 按住右键拖动（超过阈值） | 轮盘在按下位置出现 |
| 保持按住，移到扇区，松开 | 立即执行该项 |
| 轮盘打开时滚动滚轮 | 切换页面 |
| 轮盘打开时按 Esc | 取消 |
| 普通右键单击（未拖动） | 完全不受影响，正常弹出系统菜单 |

### 槽位动作类型
- **发送快捷键组合**：Win/Ctrl/Alt/Shift + 任意键（F1~F24、字母、数字、方向键、Enter、Tab 等）；录入时可用修饰键勾选框（Win 组合推荐勾选方式）
- **打开文件 / 文件夹**：系统默认关联程序打开
- **打开网址**：默认浏览器

### 轮盘结构
- 每页 6 / 8 / 10 / 12 个扇区可选
- 多页面：滚轮翻页，中心显示页码
- 槽位支持插入 / 删除 / 上移 / 下移，槽位支持自定义名称

### 外观自定义（设置"外观"页，全部实时预览）
- **色彩模式**：深色（深底白字）/ 浅色（浅底黑字）/ 自动（按轮盘展开位置屏幕背景的明暗自动选择）/ 跟随系统深浅模式；深、浅两套配色（背景、悬停高亮、边框、文字）均可自定义，内置图标随主题自动变色（设置预览中自动模式以系统深浅显示，实际唤出时按轮盘位置实时判定）；悬停高亮色可勾选**跟随系统主题色**（Windows 强调色，设置页“材质与色彩”底部）
- 直径、图标大小、扇区间隙、出现动画及时长
- 背景色 + 整体透明度、悬停高亮色、边框颜色与宽度、文字颜色
- 三种背景：纯色半透明 / 亚克力模糊（实验性）/ **液态玻璃（折射）**
- 液态玻璃参数：模糊半径、折射深度、折射强度，以及色散 / 边缘高光 / 色彩鲜活开关（轮盘打开时背景实时折射）；**折射管线在 GPU 上运行**（D3D11，无硬件加速时自动回退 CPU）
- 性能调节：**截取清晰度**（实时折射背景的截取分辨率，25%–100%）与**截取频率**（10–60Hz），在卡顿设备上可降低以换取流畅度
- 文字标签：显示开关、字体、字号
- 中心区：页码 / 自定义文字 / 不显示

### 图标
- 默认自动提取目标文件/程序/文件夹自身的图标（Shell 官方接口）
- **内置 Lucide 图标库（1744 枚）**：槽位设置点「📚 图标库…」打开选择器，支持按名称实时搜索（忽略大小写与 `-`/`_`）、方向键浏览、双击或「使用」应用；保存为 `builtin:lucide:<名称>`（如 `builtin:lucide:folder`），描边颜色随深/浅主题自动变色
- 支持自定义替换：`png / jpg / jpeg / bmp / ico / svg`

### 后台行为
- 托盘图标 + 右键菜单（设置 / 启用 / 开机自启 / 使用说明 / 退出）
- **可选隐藏托盘图标**；隐藏后**再次运行 RightDial.exe 即打开设置面板**（单实例转发），或命令行 `/settings`
- 开机自启（写入 HKCU Run 键，安装版与便携版均可）
- 排除名单：按进程名（如 `game.exe`）在指定程序的前台窗口中禁用轮盘，可从当前运行进程中直接选取；支持一键排除所有无边框全屏程序（游戏）
- 单实例运行

## 分发包

| 文件 | 说明 |
|---|---|
| `bin/RightDial-1.0.4-setup.msi` | 安装版（MSI，中文安装向导，装到 Program Files，含开始菜单快捷方式） |
| `bin/RightDial-1.0.4-portable.zip` | 便携版（解压即用，配置保存在 exe 同目录，附使用说明） |

安装版与便携版的行为差异仅在配置文件位置：
- 安装版：`%APPDATA%\RightDial\config.json`
- 便携版：exe 同目录 `config.json`（由 `portable.ini` 或已存在的 `config.json` 自动识别）

## 命令行参数

```
RightDial.exe /settings   打开设置面板
RightDial.exe /exit       退出正在运行的实例（第二实例自动转发）
RightDial.exe /testwheel  在屏幕中央显示一次轮盘（诊断用）
```

## 构建方法

依赖：VS 2022 Build Tools（含 VC++ 工具集与 Windows SDK）。第三方库（nanosvg、nlohmann/json）以单头文件形式放在 `third_party/`。内置图标数据 `src/lucide_icons.cpp` 由 `tools/gen_lucide.py` 生成（仅更新图标集时需要 Python，日常构建不需要）。

```bat
build.cmd        :: 编译 bin\RightDial.exe（cl /O1 /MT）
build-msi.cmd    :: 生成 bin\RightDial-1.0.4-setup.msi（WiX 5，dotnet tool install --global wix --version 5.0.2）
build-hooklat.cmd :: 编译 bin\hooklat.exe（低层钩子延迟回归探针，见下）
```

> `build.cmd` 在 `vcvars64.bat` 不可用（如受管控环境里它调用的 `reg.exe` 被拦）时会自动改为手工拼装工具链环境，无需额外配置。

**发版注意**：每次发布前必须递增版本号，否则 MSI 无法覆盖升级旧版本（Windows Installer 按 ProductCode + Version 判断升级）。需同步修改：`installer/product.wxs`（Package Version）、`src/app.rc`（VERSIONINFO）、`src/app.manifest`、`build-msi.cmd` 与 `tools/update-portable.ps1` 的产物文件名、README 分发包表格。

便携 zip 手工打包：`RightDial.exe` + `portable.ini`（空文件）+ 使用说明，压缩即可（`dist/` 下有现成结构）。

## 低层钩子的硬性约束（勿违反）

`WH_MOUSE_LL` 钩子**全局阻塞操作系统原始输入线程**：钩子过程返回之前，整个系统的鼠标输入都在等它。因此钩子过程里只能做「纯内存判断」，任何耗时操作都必须 `PostMessage` 交给消息循环执行。

违反此约束的历史后果：**普通右键单击延迟约 300 ms**（右键开始菜单要等半秒），因为 `SendInput()` 在钩子内部调用时会等原始输入线程返回，最终只等到低层钩子超时。

| 位置 | 禁止做的事 | 现在的做法 |
|---|---|---|
| `hook.cpp` 补发被吞掉的单击 | 在钩子内调 `SendInput` | `PostMessage(WM_APP_INJECT_CLICK)`，由 `main.cpp` 在消息循环里补发 |
| `hook.cpp` 拖拽越过阈值 | 在钩子内建窗口 / 截屏 / D2D 渲染 | `PostMessage(WM_APP_SHOWWHEEL)`，由 `main.cpp` 调 `WheelShowAt` |
| `wheel.cpp` 悬停与翻页 | 在钩子内 `RenderNow()` | `RequestRender()` 合并后 `PostMessage(WM_WHEEL_RENDER)` |

回归探针（复现「吞掉按下、抬起时补发」的模式并逐事件打点）：

```bat
build-hooklat.cmd
bin\hooklat.exe -mode 0     :: 钩子内 SendInput —— 复现故障，约 306~313 ms
bin\hooklat.exe -mode 1     :: 延后补发     —— 修复后，约 0.013 ms
```

两种模式的日志分别在 `bin\hooklat_mode0.log` / `bin\hooklat_mode1.log`。**改动钩子相关代码后请重跑，`mode 1` 的 `dur=` 必须保持在 0.1 ms 量级。**

## 目录结构

```
src/           C++ 源码（main / hook / wheel / render / glass / glass_gpu / actions / config / settings / tray / util；lucide_icons.cpp 为生成文件）
third_party/   nanosvg（SVG 渲染）、nlohmann/json（配置读写），均为单头文件
assets/        程序图标生成脚本与 .ico
installer/     WiX 打包定义
tools/         冒烟测试辅助脚本（截屏 / 点击 / 内存测量）
bin/           构建产物（exe / msi / portable.zip）
dist/          便携版打包目录
```

## 配置文件（JSON，可手改）

```jsonc
{
  "appearance": {
    "diameter": 300, "sectorCount": 8, "bgColor": "#1e2430", "...": "...",
    "colorMode": 0,                // 0 深色 / 1 浅色 / 2 自动 / 3 跟随系统
    "bgColorLight": "#eef1f5", "textColorLight": "#1b2029",
    "hoverColorLight": "#cdd9ea", "borderColorLight": "#a9b4c4",
    "hoverAccent": false,          // 高亮颜色跟随系统主题色
    "backdropScale": 1.0,          // 实时背景截取分辨率比例 0.25–1.0（0=自动：大轮盘半分辨率）
    "backdropFps": 60              // 实时背景截取频率上限 10–60 Hz
  },
  "behavior":   { "triggerButton": 1, "thresholdPx": 12, "exclusions": ["game.exe"] },
  "pages": [
    { "name": "常用", "slots": [
      { "type": 0, "name": "粘贴", "keys": "Ctrl+V" },
      { "type": 1, "name": "下载", "path": "C:\\Users\\you\\Downloads" },
      { "type": 2, "name": "GitHub", "url": "https://github.com" }
    ]}
  ]
}
```
`type`：0=快捷键，1=文件/文件夹，2=网址；`icon` 可填图片路径、`builtin:lucide:<名称>`（内置 Lucide 图标库）或 `builtin:<名称>`（少量传统内置图标），留空自动提取。

## 已知限制

- 目标窗口以**管理员权限**运行时，本程序（非管理员）收不到其鼠标事件、也无法注入按键 → 在设置中改用管理员身份运行即可
- 含 **Win 键**的组合键已支持发送（设置槽位编辑中勾选 Win 修饰键后按主键即可）；**Win+L** 为系统保留组合、无法注入按键，执行时自动改调系统锁定 API（`LockWorkStation`）直接锁屏；其余保留组合（如 Ctrl+Alt+Del）无法发送
- 全屏独占（DirectX 3D）游戏中轮盘不会显示，可用排除名单避免手势冲突
- 亚克力模糊背景为实验特性，如无效会自动退化为半透明背景

## 许可

自研代码无限制使用；第三方内容遵循其原始许可（nanosvg — zlib/MIT 风格，nlohmann/json — MIT，[Lucide 图标](https://lucide.dev) — ISC，许可全文见 `src/lucide_icons.cpp` 头部）。

液态玻璃（bgMode 2）的折射/色散/鲜活度算法移植自 [Kyant0/AndroidLiquidGlass](https://github.com/Kyant0/AndroidLiquidGlass)（Apache License 2.0，Copyright 2025 Kyant），实现见 `src/glass.cpp` 头部说明。
