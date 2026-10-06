# 在圆形触摸屏上玩 DOOM · sdgoods-doom

把经典 **DOOM** 移植到 **SDGOODS 谷仓次元屏**（ESP32-S3 圆形触摸屏，只有一个电源键、没有方向键），
用屏幕上的 **GBA 风格虚拟按键** 操作。这是一份**独立的单应用固件**——开机直接进游戏，不依赖任何启动器或多应用框架。

| 标题画面 | 实机游玩（E1M1） |
|:---:|:---:|
| ![标题](screenshot/sdgoods-doom-title.jpg) | ![E1M1](screenshot/sdgoods-doom-e1m1.jpg) |

> 约 **26 fps**、彩色 HUD、带音效，纯触屏单指可玩。

---

## 目录

- [它是什么 / 亮点](#它是什么--亮点)
- [快速上手（拿到就能玩）](#快速上手拿到就能玩)
- [操作说明](#操作说明)
- [从零编译](#从零编译)
- [烧录详解](#烧录详解)
- [重新生成音效库](#重新生成音效库)
- [抓取真机画面（截屏）](#抓取真机画面截屏)
- [常见问题 FAQ](#常见问题-faq)
- [给开发者：架构与目录](#给开发者架构与目录)
- [后续方向](#后续方向)
- [引用与致谢](#引用与致谢)
- [许可与合规](#许可与合规)

---

## 它是什么 / 亮点

- **真·DOOM，不是模拟器**：跑的是 DOOM 引擎（GBADoom，PrBoom 血统）+ 标准 DOOM WAD，不是"在 GBA 模拟器里放 DOOM"。
- **为圆形触摸屏重做的操作**：单点触摸无法多指同按，所以按键全部改成"按住式"，方向/开火分置画面两侧，一根拇指就能打。
- **开机即玩**：单应用直启，上电约 1 秒进标题画面，没有桌面、没有应用列表。
- **带音效**：枪声、开门、怪物叫声都有（外置音效库，见下）。
- **可远程验机**：设备能把当前画面经 USB 串口回传成图片，不用拿手机对着屏幕拍。

### 硬件（谷仓电子徽章 / 次元屏）

| 部件 | 型号 / 规格 |
|---|---|
| 主控 | ESP32-S3-R8（双核 LX7 @240MHz，**8MB Octal PSRAM**） |
| 存储 | **32MB Flash**（QSPI） |
| 屏幕 | 圆形 **360×360**，ST77916，QSPI，RGB565 |
| 触摸 | **CST816** 单点电容触摸（I2C，支持手势） |
| 按键 | 仅 1 个电源键（**无物理方向 / 动作键**，全靠触屏虚拟键） |

---

## 快速上手（拿到就能玩）

如果你只想**尽快玩上**，不必自己编译——直接用发布好的成品，三步搞定：

**① 下载**（都在本仓库的 [Releases](https://github.com/YeatsLiao/sdgoods-doom/releases) 页面）

| 文件 | 说明 |
|---|---|
| `SDGOODS_DOOM.bin` | 游戏固件本体 |
| `DOOM1_PROCESSED.WAD` | **必须用这个版本**的关卡数据（见 FAQ） |
| `DOOM_SFX.bin` | 音效库（可选，不烧则静音运行） |

> ⚠️ WAD 与音效库因含 id Software 版权素材，**不放进 git 仓库**，只作为 Release 资产提供——
> 所以 `git clone` 下来是看不到这两个文件的，请从 Releases 下载。

**② 烧录**（USB-C 接上设备，认准串口号，Windows 如 `COM6`）

```bash
# 固件（含自编引导层，见"烧录详解"为什么要自编）
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash \
  0x0  bootloader.bin  0x8000 partition-table.bin  0x10000 SDGOODS_DOOM.bin
# 擦 otadata → 让设备回落 factory 槽、开机直进 DOOM
esptool.py --chip esp32s3 -p COM6 erase_region 0x310000 0x2000
# 关卡数据 → appdata 头部
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x1000000 DOOM1_PROCESSED.WAD
# 音效库 → appdata+0x480000（可选）
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x1480000 DOOM_SFX.bin
```

**③ 上电开玩**：按电源键开机 → 标题画面 → 用屏幕虚拟键操作。

> 自己编译的话，`idf.py -B build_pub -p COM6 flash` 会一次性把固件+引导层+分区表都烧好，
> 之后只需再手动烧 WAD（和可选的 SFX）+ 擦 otadata。详见 [从零编译](#从零编译) 与 [烧录详解](#烧录详解)。

---

## 操作说明

CST816 是**单点触摸**（同一时刻只能感应一根手指），所以按键全按"单指、按住生效"设计：

| 虚拟键 | 作用 | 怎么按 |
|---|---|---|
| ▲ ▼ | 前进 / 后退 | **按住**才动，松手即停 |
| ◀ ▶ | 左转 / 右转 | 按住 |
| **A** | 开火 | 按住 |
| **B** | 使用 / 开门 | 按住 |
| **L / R** | 切换武器 | 按住 |
| **ST / SE** | 菜单 / 确认 | 按住 |

- 按键排布仿 GBA：顶部窄行 `L SE ST R`，下方十字方向 + 两颗大圆键 `A / B`，半透明白浮在画面下沿。
- **画面区不挂手势**——转向用 ◀ ▶、开火用 A，单指下更直观、不误触。
- 顶部**向下滑**可唤出系统浮层（控制中心），调节音量 / 亮度等。
- 电源键：游戏内短按用于唤出/收起浮层，无操作时熄屏。

---

## 从零编译

依赖 **ESP-IDF v5.5** 和 **GBADoom 源码树**（`esp32-sdgoods` 分支）。

```bash
# 1) 取引擎源码：与本工程放在同级目录即可（也可用 -DGBADOOM_PATH 指定别的路径）
git clone -b esp32-sdgoods https://github.com/YeatsLiao/GBADoom ../GBADoom

# 2) 编译
. $IDF_PATH/export.sh          # Windows PowerShell: . $env:IDF_PATH\export.ps1
idf.py -B build_pub build
#   引擎在别处：idf.py -B build_pub build -DGBADOOM_PATH=/path/to/GBADoom
```

产物：`build_pub/SDGOODS_DOOM.bin`（约 1.3MB，须 ≤ 3MB 应用槽）。

> 构建期会自动做两件事，无需手动干预：
> - 对 GBADoom 的 `z_zone.c` 打一处**幂等补丁**（`components/doom_engine/patch_gbadoom.py`），
>   把引擎 128KB overflow 缓冲从内部 `.bss` 迁到 **PSRAM**——内部 SRAM 要留给 LVGL 绘制缓冲
>   （必须在 SRAM）+ 音频，否则 `dram0` 链接溢出。补丁随源码提交，CI 干净 checkout 同样生效。

---

## 烧录详解

```bash
idf.py -B build_pub -p COM6 flash                       # 固件 + 自编引导层 + 分区表
esptool.py --chip esp32s3 -p COM6 erase_region 0x310000 0x2000   # 擦 otadata → 直启 factory
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x1000000 DOOM1_PROCESSED.WAD
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x1480000 DOOM_SFX.bin   # 可选
```

或一键（Linux/macOS）：`python tools/flash_local.sh -p COM6 -B 921600`（已串好 固件 + 擦 otadata + 烧 WAD）。

**两个必须理解的点：**

1. **必须烧本工程自编的 bootloader**，不能用平台 `platform/prebuilt/` 那份。
   WAD 裸烧在 `appdata@0x1000000`（16MB 处），`esp_partition_mmap` 读它需要 flash cache 开
   **32 位地址映射**（`CONFIG_IDF_EXPERIMENTAL_FEATURES=y` + `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH=y`，
   已写进 `sdkconfig.defaults`）。这个映射由**第二级 bootloader** 启动时启用；平台 prebuilt 没开，
   会退回 24 位寻址 → WAD 读不进 → **黑屏**。`idf.py flash` 默认就烧 `build_pub/bootloader/`（自编），别手动改回去。

2. **分区表沿用平台托管版本，不改动**：`factory@0x10000`（3MB 应用槽）、`otadata@0x310000`、
   4 个 OTA 槽、`appdata` 16MB FAT @`0x1000000`。本工程只占 `factory` 一个槽，其余空着。
   WAD 烧在 appdata 头部，音效库烧在 appdata+`0x480000`。

---

## 重新生成音效库

DOOM 的 `PROCESSED`/GBA 版 WAD 都**不含音效 lump**（当年 GBA 移植为省 ROM 把音频全删了），
所以音效来自一份外置 soundbank，由 `tools/gen_soundbank.py` 从 GBADoom 的 `music/DS*.wav` 生成：

```bash
# 需先 clone GBADoom（含 music/ 素材），在本工程根目录执行：
python tools/gen_soundbank.py
# 产出：DOOM_SFX.bin（拼接 PCM）+ components/doom_engine/doom_sfx_index.h（按 sfx_id 对齐的索引表）
# 再把 DOOM_SFX.bin 烧到 appdata+0x480000（见"烧录详解"）
```

> 音乐（BGM）尚未实现，属后续里程碑。

---

## 抓取真机画面（截屏）

设备把当前屏 JPEG 编码后经 USB 串口回传，PC 落盘成图片——远程验机不用拍屏。
工具 `tools/screenshot_recv.py`（固件侧收到串口字符 `'s'` 触发）。

> ⚠️ 串口独占：截屏前**必须先关掉 `idf.py monitor` / 串口助手**，否则打不开端口。

```bash
python tools/screenshot_recv.py -p COM6 -t -o shot.jpg          # 触发并接收一张
python tools/screenshot_recv.py -p COM6 --caps                  # 查固件是否支持截屏
python tools/screenshot_recv.py -p COM6 -t -n 3 -o shot.jpg     # 连拍 3 张
python tools/screenshot_recv.py -p COM6 --pre c --wait 1.5 -t -o cc.jpg   # 先切控制中心再截
python tools/screenshot_recv.py --selftest                      # 无设备自检管线
```

---

## 常见问题 FAQ

**Q：能显示标题画面，但按键没反应、一进关卡就卡死/崩溃？**
几乎一定是**烧错了 WAD**。必须用 `DOOM1_PROCESSED.WAD`（1176 lumps，3,904,360 字节）——它把关卡
`SEGS`/`NODES` 转回了标准 DOOM 格式，并保留引擎必需的 GBA UI 补丁 lump（`STGANUM0-9`/`M_ARUN`/`M_GAMMA`）。
- ❌ **不能用 `DOOM1_GBA.WAD`**：关卡是 GBA 专有格式，PrBoom 的 `P_GroupLines` 会读到错位数据
  （日志刷 `sidedef N has out-of-range sector num…`），一进关卡就 `I_Error: P_GroupLines: Subsector a part of no sector!` 崩溃。
- ❌ 纯净 id 原版 `DOOM1.WAD` 也跑不了：引擎硬依赖上面那几个补丁 lump。

**Q：开机黑屏、串口日志说 WAD 读不进 / mmap 失败？**
八成是烧了平台 `prebuilt` 的 bootloader（没开 32 位 cache 映射）。改用本工程自编 bootloader
（`idf.py flash` 默认行为），见[烧录详解](#烧录详解)第 1 点。

**Q：完全没有声音？**
- 确认烧了 `DOOM_SFX.bin` 到 `0x1480000`（不烧也能玩，只是静音）；
- 确认音量不为 0（下滑控制中心看音量、或按电源键唤醒后调）；
- 串口日志应出现 `doom_snd: soundbank loaded: … from appdata+0x480000` 与 `I_InitSound: DOOM SFX ready`。

**Q：开火 / 使用两个键反了？**
对调 `components/doom_engine/i_system_sdgoods.c` 里 `s_keymap` 的对应两行即可。

**Q：这是 GBA 模拟器吗？能玩 GBA 游戏吗？**
不是。`GBADoom` 是 **PrBoom 血统的 DOOM 引擎**（BOOM → id DOOM 1993），当年被编译到 GBA、如今重编译到 ESP32。
它跑的是 **DOOM 的 WAD**，不是 GBA ROM。想玩 GBA 游戏是另一个项目（mGBa / RetroGBa 等）的事。

---

## 给开发者：架构与目录

### 分层

本工程剥离官方"平台应用框架"后只余四层，依赖自上而下单向（上层依赖下层，BSP 不反向硬依赖）：

```
┌─────────────────────────────────────────────┐
│  game —— main/game/ui_doom.c                  │  LVGL 外壳：canvas + GBA 虚拟键 + 帧提交
│  main/main.c：boot-direct 首屏直进 DOOM       │
├──────────────┬──────────────────────────────┤
│ doom_engine  │  control_center               │  引擎胶水（契约/音频/WAD）      系统浮层（顶部下滑）
├──────────────┴──────────────────────────────┤
│  bsp —— components/bsp（伞形头 bsp.h）          │  硬件驱动：LCD QSPI / CST816 / I2S / LVGL 移植 / JPEG / 电源 / 手势
└─────────────────────────────────────────────┘
```

- **bsp**：板级支持包，只保留硬件驱动。通过弱符号（`sdgoods_cc_open` / `sdgoods_cc_is_open` …）与上层解耦，不含 control_center 时仍可编译。
- **control_center**：唯一系统 UI（顶部下滑唤出），以强符号覆盖 bsp 的弱默认。
- **doom_engine**：GBADoom 引擎胶水层 + `doom_host.h` 契约（`btn_mask` / `frame_ready`）。
- **game**：单应用外壳，`main.c` 直接接线轮询与电源键钩子（无多应用注册表）。

### 画面链路

引擎任务（core 0）以 **240×160 / 8bpp 调色板**渲染 → 置 `frame_ready` → LVGL 线程转 RGB565、最近邻放大写
PSRAM canvas（**336×200**，居中 @ (12,58)，仅四角被圆形外框裁掉一点）→ `lv_obj_invalidate` → 平台 SRAM 条带 flush 上屏。
**不直刷 SPI**：绕开平台架构红线（QSPI DMA 缓冲不能取 PSRAM，否则黑条/红线）。

### 目录

```
sdgoods-doom/
├── CMakeLists.txt              # 定义 GBADOOM_PATH + 构建期打 GBADoom 补丁
├── components/
│   ├── bsp/                    #  平台 BSP（官方 sdgoods_board 改名）：屏/触摸/音频/LVGL/字体/电源/手势
│   │   └── include/bsp.h       #    伞形头（内部子系统头仍按 sdgoods_*.h 命名）
│   ├── control_center/         #  控制中心（由 sdgoods_launcher 改名）：顶部下滑系统浮层
│   ├── doom_engine/            #  引擎组件（GBADoom 源码 + 平台层）
│   │   ├── patch_gbadoom.py    #    构建期把 overflow 缓冲迁 PSRAM（幂等）
│   │   ├── doom_host.h         #    引擎 ⇄ UI 唯一契约（btn_mask / frame_ready）
│   │   ├── i_system_sdgoods.c  #    backbuffer(PSRAM)/调色板/键边沿检测
│   │   ├── i_sound_esp32.c     #    DOOM 音频后端：soundbank→PSRAM + 8 通道混音 + I2S
│   │   └── esp32_wad.c         #    从 appdata mmap WAD
│   └── jpegenc/                #    截屏 JPEG 编码
├── main/
│   ├── main.c                  #   boot-direct 首屏直进 DOOM
│   └── game/ui_doom.[ch]       #   LVGL 外壳：canvas + GBA 虚拟键 + 帧提交
├── platform/                   #   分区表 + 预编译引导层（平台托管，勿改）
├── screenshot/                 #   本文档配图（真机截图 + GBA 致敬图）
├── tools/
│   ├── gen_soundbank.py        #   音效库生成（music/*.wav → DOOM_SFX.bin + 索引头）
│   ├── screenshot_recv.py      #   真机截屏接收端
│   └── flash_local.sh          #   本地一键烧录
└── .github/workflows/          #   CI：clone GBADoom + 构建 + 挂 Release 资产
```

> `DOOM1_PROCESSED.WAD` 与 `DOOM_SFX.bin` **不在仓库**（id 版权素材，见 FAQ），从 Releases 下载。

---

## 后续方向

- **通用 DOOM 播放器**：撑大 mmap 窗口 + lump 注入工具，让它能刷不同 IWAD / 加载 PWAD
  （引擎硬依赖 `STGANUM`/`M_ARUN`/`M_GAMMA` 补丁 lump，纯净 IWAD 需脚本注入或回退）。
- **音乐（BGM）**：DOOM 音乐是 DMXMusic(OPL)，ESP32 播放需模块播放器或 OPL 软合成，较重，暂缓。
- 游戏内退出到桌面、BLE 手柄、上架开放平台商店。

---

## 引用与致谢

本固件是站在许多前人作品肩膀上做出来的，特此致谢与标注来源：

- **DOOM / id Software**：原版游戏与关卡数据（1993）。本项目的 WAD 基于 id 放出的 **shareware（DOOM1）** 版本。
- **GBADoom**（YeatsLiao fork 的 `esp32-sdgoods` 分支）：把 DOOM 引擎带到掌机/嵌入式平台的移植工程，本项目的引擎基座。
- **PrBoom / BOOM**：GBADoom 的引擎血统（id DOOM → BOOM → PrBoom），提供兼容层与特性。
- **SDGOODS 谷仓开放平台**：ESP32-S3 次元屏的板级支持（LCD QSPI / CST816 触摸 / I2S 音频 / LVGL 移植 / JPEG / 电源 / 手势），
  即 `components/bsp`、`components/control_center`、`components/jpegenc`，**Apache-2.0**，版权归深圳希德创新网络有限公司。
- **LVGL 8.3.11**（MIT）：图形与触摸交互。
- **ESP-IDF 5.5 / Espressif**（Apache-2.0）：SoC 固件框架。
- 字体、图标等素材见各组件文件头声明。

> 本项目与 SDGOODS / 谷仓官方**无隶属关系**，仅使用该硬件平台并保留其代码的 Apache-2.0 版权与许可声明。
> 项目名、产品名与 SDGOODS 标识不在代码许可授权范围内。

### 引擎血统致敬

`GBADoom` 当年把 DOOM 塞进了 Game Boy Advance；本项目把同一条引擎链路搬到了圆形触摸屏上。
下面这张是 DOOM 在 GBA 掌机上运行的实拍，作为"从 GBA 到次元屏"的血统注脚：

![GBA 上的 DOOM（致敬）](screenshot/gbadoom-hardware.jpg)

---

## 许可与合规

本工程是"多许可混合"，按目录区分：

| 部分 | 许可 | 版权 |
|---|---|---|
| `components/doom_engine/` + 链接的 GBADoom | **GPL-2.0-or-later** | GBADoom / PrBoom / BOOM / id Software |
| `components/bsp`、`components/control_center`、`components/jpegenc` | **Apache-2.0** | 深圳希德创新网络有限公司（SDGOODS） |
| `main/*`（应用 / 游戏外壳） | Apache-2.0 声明见文件头；随 GPL 引擎合并后**整体产物按 GPL-2.0 分发** | — |
| `DOOM1_PROCESSED.WAD` / `DOOM_SFX.bin` | 游戏数据，**版权归 id Software**，仅作合法试用分发 | id Software |

- 合并后的固件产物按 **GPL-2.0-or-later** 条款分发，完整文本见 [`LICENSE`](LICENSE)。
- 第三方组件的原始许可与版权头**均予保留**，详见各文件头与 [`NOTICE`](NOTICE)。
- WAD 与音效库含 id Software 版权素材，**不纳入 git 仓库**，仅以 shareware 版本经 Releases 提供，请自行通过官方渠道获取，勿用于商业分发。
