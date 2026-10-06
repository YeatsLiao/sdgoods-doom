# 在圆形触摸屏上玩 DOOM 🔫

一块比硬币大不了多少的**圆形触摸屏小徽章**，开机直接进 DOOM，一根拇指就能突突突。
真·DOOM 引擎（不是模拟器），彩色 HUD、带音效、约 26 fps。

![实机游玩](screenshot/sdgoods-doom-handheld.jpg)

| 标题画面 | E1M1 截图 |
|:---:|:---:|
| ![标题](screenshot/sdgoods-doom-title.jpg) | ![E1M1](screenshot/sdgoods-doom-e1m1.jpg) |

> 硬件：SDGOODS 谷仓次元屏 —— ESP32-S3-R8、360×360 圆形屏、单点电容触摸、**只有一个电源键**，
> 所以方向 / 开火 / 开门全做成了屏幕上的 GBA 风虚拟键。

---

## 🎮 一键开玩（不用编译）

去 [Releases](https://github.com/YeatsLiao/sdgoods-doom/releases) 下载 **`sdgoods-doom-full.bin.zip`**，
解压得到 `sdgoods-doom-full.bin`（固件 + 关卡 + 音效全在里面），然后**一条命令刷完**：

```bash
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x0 sdgoods-doom-full.bin
```

按电源键开机 → 标题画面 → 开打。想退回官方固件，用平台安装通道重刷即可，不会刷坏。

> 串口卡在 `Connecting...`？按住 **BOOT** → 点一下 **RST** → 松 BOOT，再给上面命令加 `--before no_reset`。

### 按键怎么用

CST816 是单点触摸（同一时刻只认一根手指），所以按键都是**按住生效、松手停**：

| ▲ ▼ ◀ ▶ | 前进/后退、左转/右转 |
|---|---|
| **A** | 开火 |
| **B** | 使用 / 开门 |
| **L / R** | 换武器 |
| **ST / SE** | 菜单 / 确认 |

顶部向下滑能唤出控制中心调音量 / 亮度。

---

## 🛠 想自己动手编译

需要 **ESP-IDF v5.5** + **GBADoom 源码**（`esp32-sdgoods` 分支，与本工程放同级目录）：

```bash
git clone -b esp32-sdgoods https://github.com/YeatsLiao/GBADoom ../GBADoom
idf.py -B build_pub build                 # 产物 build_pub/SDGOODS_DOOM.bin
python tools/make_full_bin.py             # 拼出 sdgoods-doom-full.bin（需仓库根放好 WAD/SFX）
```

分步烧录（不想用 full.bin 时）——**紧凑单体分区表**，地址就这几个：

```bash
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash \
  0x0     build_pub/bootloader/bootloader.bin \
  0x8000  build_pub/partition_table/partition-table.bin \
  0x10000 build_pub/SDGOODS_DOOM.bin
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x210000 DOOM1_PROCESSED.WAD
esptool.py --chip esp32s3 -p COM6 -b 921600 write_flash 0x690000 DOOM_SFX.bin   # 可选，不烧则静音
```

> 分区表是本工程自带的紧凑布局：`factory@0x10000(2MB)` + `appdata@0x210000(16MB)`，
> 没有 otadata / OTA 槽——单应用从 factory 直启。WAD 烧在 appdata 头部，音效烧在 appdata+0x480000。
> 引导层用本工程自编的 `build_pub/bootloader/`，别用平台 prebuilt。

### 音效库怎么来的

DOOM 的 shareware WAD 不含音效，`DOOM_SFX.bin` 由 `tools/gen_soundbank.py` 从 GBADoom 的
`music/*.wav` 生成。（背景音乐 BGM 还没做，是后续里程碑。）

---

## ❓ 常见问题

**进关卡就崩 / 黑屏？** 几乎一定是 WAD 烧错了。必须用 **`DOOM1_PROCESSED.WAD`**
（3,904,360 字节，关卡已转回标准格式 + 含引擎必需的补丁 lump）。`DOOM1_GBA.WAD`、纯净原版
`DOOM1.WAD` 都会让引擎崩在 `P_GroupLines`。

**没声音？** 确认烧了 `DOOM_SFX.bin` 到 `0x690000`，且音量不为 0（下滑控制中心看）。

**WAD / 音效怎么没在仓库里？** 它们含 id Software 版权素材，**不进 git**，只在 Releases 提供。

---

## 📁 目录速览

```
sdgoods-doom/
├── main/            # boot-direct 首屏 + LVGL 游戏外壳（canvas + GBA 虚拟键）
├── components/
│   ├── bsp/         #   板级驱动：屏 / 触摸 / 音频 / LVGL / 电源 / 手势
│   ├── control_center/  # 顶部下滑系统浮层
│   ├── doom_engine/ #   GBADoom 引擎胶水：WAD mmap / 音频后端 / 键位
│   └── jpegenc/     #   截屏 JPEG 编码
├── platform/        #   自带紧凑分区表 partitions.csv
├── screenshot/      #   本文档配图
└── tools/           #   make_full_bin.py / gen_soundbank.py / flash_local.sh / screenshot_recv.py
```

引擎以 240×160 调色板渲染 → LVGL 放大写 PSRAM canvas → 上屏；绕开"QSPI DMA 缓冲不能取 PSRAM"的平台红线。

---

## 🙏 致谢与许可

- **DOOM / id Software**（1993）：游戏与关卡，本项目基于 shareware（DOOM1）。
- **GBADoom**（PrBoom / BOOM 血统）：把 DOOM 引擎搬上嵌入式平台的移植工程。
- **SDGOODS 谷仓开放平台**（Apache-2.0）：次元屏板级支持（`bsp` / `control_center` / `jpegenc`）。
- **LVGL 8.3**（MIT）、**ESP-IDF 5.5**（Apache-2.0）。

固件产物按 **GPL-2.0-or-later** 分发（详见 [`LICENSE`](LICENSE)、第三方声明见 [`NOTICE`](NOTICE)）；
WAD / 音效版权归 id Software，仅作合法试用分发，勿商用。本项目与 SDGOODS 官方无隶属关系。

<sub>下面这张是 DOOM 当年在 GBA 掌机上的实拍——同一条引擎链路，从 GBA 搬到了圆形触摸屏：</sub>

![GBA 上的 DOOM（致敬）](screenshot/gbadoom-hardware.jpg)
