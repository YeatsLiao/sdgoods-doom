# 谷仓次元屏 · DOOM（sdgoods-doom）

> 把 **DOOM** 移植到 SDGOODS 谷仓次元屏（ESP32-S3 圆形触摸屏，无物理按键），
> 用屏幕上的 **GBA 布局虚拟按键** 操作。独立单应用固件，开机直进游戏。

这是从谷仓 SDGOODS 开放平台基础工程派生出的**纯 DOOM 工程**：只保留跑 DOOM 所需的
平台 BSP（`components/bsp`、`components/control_center`、`components/jpegenc`）+ 引擎胶水层（`components/doom_engine`）+
一个 LVGL 游戏外壳（`main/game`），官方模板示例（主页启动台 / 小鸟 / 扫描示例）、多应用注册表与发布链路脚本已全部剔除。

---

## 硬件（谷仓电子徽章）

| 部件 | 型号 / 规格 |
|---|---|
| 主控 SoC | ESP32-S3-R8（双核 Xtensa LX7 @240MHz，内置 **8MB Octal PSRAM**） |
| 存储 | **32MB Flash**（QSPI） |
| 显示屏 | 圆形 **360×360**，ST77916 驱动，QSPI，RGB565 |
| 触摸 | **CST816** 单点电容触摸（I2C，支持滑动手势） |
| 按键 | 仅 1 个电源键（无方向 / 动作键） |

## 引擎与画面

- 引擎：**GBADoom**（YeatsLiao fork 的 `esp32-sdgoods` 分支，PrBoom 血统，纯 C）
- 内部分辨率 **240×160**、8bpp 调色板索引，本工程经最近邻放大到 canvas **336×200** 撑满圆屏（居中 @ (12,58)，仅四角被圆形外框裁掉一点）
- 帧链路：引擎任务（core 0）渲染索引帧 → 置 `frame_ready` → LVGL 线程（apps_poll）转 RGB565 写 PSRAM canvas → `lv_obj_invalidate` → 平台 SRAM 条带 flush 上屏
  - **不直刷 SPI**：绕开平台架构红线（QSPI DMA 缓冲不能取 PSRAM，否则黑条/红线）
- WAD：**必须用 `DOOM1_PROCESSED.WAD`**（1176 lumps，3,904,360 字节）——它把关卡 `SEGS`/`NODES` 转回了**标准 DOOM 格式**（PrBoom 能解析），并保留引擎必需的 GBA UI 补丁 lump（`STGANUM0-9` HUD 数字 / `M_ARUN` / `M_GAMMA` 菜单项）。裸烧进 `appdata` 分区头部 `0x1000000`，`esp_partition_mmap` 固定 4.25MB 窗口直接读取，**不挂 FAT**。
  - ⚠️ **不能用 `DOOM1_GBA.WAD`**：它的关卡数据是 GBA 专有格式，PrBoom 的 `P_LoadSideDefs2`/`P_GroupLines` 会读到错位数据（日志刷 `sidedef N has out-of-range sector num …`），一进关卡就 `I_Error: P_GroupLines: Subsector a part of no sector!` 崩溃——表现为标题画面能显示、但按键无反应、画面卡死。
  - 音频：PROCESSED 与 GBA 版都**不含 `DS*`/`D_*` 音效/音乐 lump**（GBA 移植时删了）。音效由外部 `DOOM_SFX.bin` 提供（见“编译/烧录”），音乐尚未实现。
  - 纯净 id 原版 `DOOM1.WAD` 也跑不了——引擎硬依赖 `STGANUM`/`M_ARUN`/`M_GAMMA` 这几个补丁 lump。

## 输入：单指可玩的 GBA 虚拟键

CST816 是单点触摸，无法同时按多键，故为"单指"重新设计：

| 虚拟键 | 语义 | 触发方式 |
|---|---|---|
| ▲ ▼（十字键） | 前进 / 后退 | **按住**：按住才动、松手即停 |
| ◀ ▶（十字键） | 左转 / 右转 | 按住 |
| **A** | 开火（→ KEYD_B） | 按住 |
| **B** | 使用 / 开门（→ KEYD_A） | 按住 |
| L / R | 切换武器 | 按住 |
| ST / SE | 菜单 / SELECT | 按住 |

画面区**不挂手势**（转向用 ◀ ▶、开火用 A，单点触摸下更直观、避免误触）；全部按键为 `latch=false` 按住式。
按键 ↔ 引擎键码经共享位掩码 `g_doom_host.btn_mask` 传递，引擎侧 `I_ProcessKeyEvents` 做边沿检测后 `D_PostEvent`。

---

## 编译

依赖 ESP-IDF **v5.5** + GBADoom 源码树。

```bash
# 1) 放置 GBADoom（esp32-sdgoods 分支），与本工程同级即可
git clone -b esp32-sdgoods https://github.com/YeatsLiao/GBADoom ../GBADoom

# 2) 编译（默认找同级 ../GBADoom，也可用 -DGBADOOM_PATH 指定）
. $IDF_PATH/export.sh
idf.py -B build_pub build
#   路径不同：idf.py -B build_pub build -DGBADOOM_PATH=/path/to/GBADoom
```

> 构建期会自动对 GBADoom 的 `z_zone.c` 打一处**幂等补丁**（`components/doom_engine/patch_gbadoom.py`），
> 把引擎的 128KB overflow 缓冲从内部 `.bss` 迁到 **PSRAM**——S3 内部 DRAM 要留给 LVGL 绘制缓冲
> （必须在 SRAM）+ WiFi/BLE/音频，否则 `dram0` 链接溢出。补丁随源码提交，CI 干净 checkout 后同样生效。

产物：`build_pub/SDGOODS_DOOM.bin`（约 2.1MB，须 ≤ 3MB 应用槽）。

> ⚠️ **32 位 flash cache 是硬要求**：WAD 裸烧在 `appdata@0x1000000`（16MB），`esp_partition_mmap`
> 读它需要 flash cache 开 32 位地址映射（`CONFIG_IDF_EXPERIMENTAL_FEATURES=y` +
> `CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH=y`，已写进 `sdkconfig.defaults`）。
> 该映射由**第二级 bootloader** 启动时启用，所以**必须烧本工程自编的 bootloader**（见下），
> 不能用平台 `prebuilt/`（它没开这个，会退回 24 位寻址 → WAD 读不进 → 黑屏）。

## 烧录（单应用直启 + WAD）

```bash
# 固件（idf.py flash 会烧本工程自编的 bootloader + 分区表 + app，含 32 位 cache 映射）
idf.py -B build_pub -p <PORT> flash
# 擦 otadata → 单应用直启（让 bootloader 回落 factory@0x10000）
esptool.py --chip esp32s3 -p <PORT> erase_region 0x310000 0x2000
# WAD → appdata 头部（必须 PROCESSED 版，非 GBA 版）
esptool.py --chip esp32s3 -p <PORT> -b 921600 write_flash 0x1000000 DOOM1_PROCESSED.WAD
# 音效库 → appdata+0x480000（DOOM_SFX.bin，由 tools/gen_soundbank.py 生成）
esptool.py --chip esp32s3 -p <PORT> -b 921600 write_flash 0x1480000 DOOM_SFX.bin
```

> `idf.py flash` 默认就烧 `build_pub/bootloader/`（自编、含 32 位映射），**不要**手动改回平台
> `prebuilt/bootloader.bin`。`tools/flash_local.sh` 也已改为使用 build 目录内的自编引导层。

或一键（Linux/macOS）：`python tools/flash_local.sh -p <PORT> -B 921600`
（该脚本已串好 固件 + 擦 otadata + 烧 WAD 三步）。

分区表沿用平台托管版本，**不改动**：4 个 OTA 槽各 3MB + `appdata` 16MB @`0x1000000` + `otadata` @`0x310000`。

---

## 截屏（抓取真机画面）

设备把当前屏用 JPEG 编码后经 USB 串口回传，PC 端落盘成图片——无需相机拍屏，可远程核验画面。
工具：`tools/screenshot_recv.py`（固件侧由 `components/bsp/src/sdgoods_screenshot.c` 响应，串口收到字符 `'s'` 即触发）。

> ⚠️ 串口独占：截屏前**必须先关掉 `idf.py monitor` / 串口助手**，否则打不开端口。

```bash
# 自动向串口发 's' 触发并接收一张（-t），存为 shot.jpg
python tools/screenshot_recv.py -p COM6 -t -o shot.jpg

# 查固件是否支持截屏能力（发 '?'，回 'SDGOODS-CAPS:SHOT,...'）
python tools/screenshot_recv.py -p COM6 --caps

# 连拍 3 张
python tools/screenshot_recv.py -p COM6 -t -n 3 -o shot.jpg

# 截「非首屏」界面：先发切换字符（如控制中心 'c'），等渲染好再截
python tools/screenshot_recv.py -p COM6 --pre c --wait 1.5 -t -o cc.jpg

# 无设备自检（只验证解析 + RGB565→PNG + JPEG 落盘管线）
python tools/screenshot_recv.py --selftest
```

说明：
- 传输协议为「文本头 `===SHOT-BEGIN ...===` + 定长原始二进制 + 文本尾 `===SHOT-END===`」，
  设备端优先 JPEG（典型 20~50KB，约 2~5 秒），编码失败退回原始 RGB565（259KB，约 25 秒），故 `--timeout` 别设太短。
- 只能截「当前显示的那一屏」；控制中心 / 二级页等要靠触摸才到的界面，用 `--pre <字符>` 先切过去再截。
- 脚本会保持 DTR/RTS 为高，避免打开串口瞬间复位设备。

---

## 分层架构

本工程剥离官方「平台应用框架」后只余四层，依赖自上而下单向（上层依赖下层，BSP 不反向硬依赖）：

```
┌─────────────────────────────────────────────┐
│  game  —— main/game/ui_doom.c                 │  LVGL 外壳：canvas + GBA 虚拟键 + 帧提交
│  main/main.c：boot-direct 首屏直进 DOOM       │
├──────────────┬──────────────────────────────┤
│ doom_engine  │  control_center               │  引擎胶水（GBADoom 契约/音频/WAD）  系统浮层（顶部下滑）
├──────────────┴──────────────────────────────┤
│  bsp  —— components/bsp（伞形头 bsp.h）          │  硬件驱动：LCD QSPI / CST816 / I2S / LVGL 移植 / JPEG / 电源 / 手势
└─────────────────────────────────────────────┘
```

- **bsp**：板级支持包，只保留硬件驱动。通过弱符号（`sdgoods_cc_open` / `sdgoods_cc_is_open` / `sdgoods_cc_close` …）与上层解耦，不含 control_center 时仍可编译。
- **control_center**：取代旧「主页启动台 + 应用内菜单」的唯一系统 UI（顶部下滑唤出），以强符号覆盖 bsp 的弱默认。
- **doom_engine**：GBADoom 引擎胶水层 + `doom_host.h` 契约（`btn_mask` / `frame_ready`）。
- **game**：单应用外壳，`main.c` 直接接线轮询与电源键钩子（无多应用注册表）。

## 目录结构

```
sdgoods-doom/
├── CMakeLists.txt              # 定义 GBADOOM_PATH + 构建期打 GBADoom 补丁（引用 components/doom_engine/）
├── DOOM1_PROCESSED.WAD         # 标准格式关卡 + GBA UI 补丁（引擎必需 lump；必须用此版，非 GBA 版）
├── DOOM_SFX.bin                # 音效库（tools/gen_soundbank.py 生成，烧 appdata+0x480000）
├── components/
│   ├── bsp/                    #  ★ 平台 BSP（由官方 sdgoods_board 改名）：屏/触摸/音频/LVGL/字体/电源/手势 — 勿动
│   │   └── include/bsp.h       #    伞形头（原 sdgoods_board.h）；内部子系统头仍按 sdgoods_*.h 命名
│   ├── control_center/         #  ★ 控制中心（取代旧 launcher，由 sdgoods_launcher 改名）：顶部下滑系统浮层
│   │   └── src/sdgoods_cc.c
│   ├── doom_engine/            #  ★ 引擎组件（由 doom 改名；GBADoom 源码 + SDGOODS 平台层）
│   │   ├── CMakeLists.txt      #    GLOB 引擎源 + 排除表 + --wrap=clock
│   │   ├── patch_gbadoom.py    #    构建期把 overflow 缓冲迁 PSRAM（幂等）
│   │   ├── doom_host.h         #    引擎 ⇄ UI 唯一契约（btn_mask / frame_ready）
│   │   ├── i_system_sdgoods.c  #    平台层：backbuffer(PSRAM)/调色板/键边沿检测
│   │   ├── i_sound_esp32.c     #    ★ DOOM 音频后端：soundbank→PSRAM + 8 通道混音 + I2S 推流
│   │   └── esp32_wad.c         #    从 appdata mmap WAD
│   └── jpegenc/
├── main/
│   ├── main.c                  #   boot-direct 首屏直进 DOOM（直接轮询/电源键钩子）
│   └── game/                   #   ★ 游戏外壳（原 main/apps，已去 apps_registry）
│       └── ui_doom.[ch]        #     LVGL 外壳：canvas + GBA 虚拟键 + 帧提交
├── platform/                   #   分区表 + 预编译引导层（平台托管，勿改）
├── tools/gen_soundbank.py      #   音效库生成（GBADoom/music/*.wav → DOOM_SFX.bin + doom_sfx_index.h）
├── tools/screenshot_recv.py    #   真机截屏接收端（见上文“截屏”）
├── tools/flash_local.sh        #   本地一键烧录
└── .github/workflows/          #   CI：clone GBADoom + -DGBADOOM_PATH 构建
```

## 真机验证结果（ESP32-S3 次元屏，COM6 实刷）

固件已刷入真机并跑通，串口日志 + 截图实测确认：

- ✅ **WAD mmap 成功**：`doom_wad: WAD mmap ok: addr=0x1000000 map=4352KB`（PROCESSED 版，开 32 位 cache 后）。
- ✅ **引擎启动**：`PrBoom / Playing: DOOM Shareware`，`W_Init`/`R_Init` 正常，`backbuffer 38400B @ PSRAM`。
- ✅ **帧率 ~26 fps**：`doom_ui: 26 fps`（远超 12fps 目标，无需再降 canvas）。
- ✅ **色彩正确**：截屏中 HUD 血量为红、护甲为绿——证明 `I_SetPallete_e32`
  已按 `LV_COLOR_16_SWAP=y` 做了一次 bswap（平台 `sdgoods_screenshot.c` 注释印证 LVGL 缓冲高字节在前）。
- ✅ **音效链路**：`doom_snd: soundbank loaded: 1216747 B from appdata+0x480000 (PSRAM)` +
  `I_InitSound: DOOM SFX ready (out=16000Hz)`；音量 100% 时 `audio: volume=100% -> PA ON`。
- ✅ **GBA 虚拟键布局**：顶部窄行 L/SE/ST/R + 6 颗等大圆键（十字方向 + A/B），半透明白浮于画面下沿。

待用户上手验收（截图/日志无法验证的部分）：

- **触摸手感**：按住方向键移动 + 按 A 开火是否顺手（单点触摸下无法多指同按）。
- **实际听感**：进游戏按 A 开火 / 开门 / 受击是否有声、音量是否合适（软件链路已通，喇叭出声需人耳确认）。
- **A/B 语义**：若开火/使用反了，对调 `i_system_sdgoods.c` 的 `s_keymap` 两行。
- **MVP 未做**：音乐（BGM）、游戏内退出到桌面、BLE 手柄、上架开放平台商店。

## 许可

- **引擎相关**（`components/doom_engine/*` + 链接的 GBADoom）：GBADoom/DOOM 引擎为 **GPL-2.0-or-later**，
  合并固件产物按 GPL 条款分发。
- **平台 BSP 组件**（`components/bsp`、`components/control_center`、`components/jpegenc`）：**Apache-2.0**，
  版权归深圳希德创新网络有限公司（SDGOODS）。
- **应用/游戏外壳**（`main/*`）：源自 SDGOODS 基础工程，Apache-2.0 声明见文件头；随 GPL 引擎合并后整体按 GPL 分发。
- **`DOOM1_PROCESSED.WAD`**：游戏数据，版权归 **id Software**。本仓库沿用 passport 做法，仅以
  **shareware（DOOM1）版本**随固件分发，供合法试用；请自行通过官方渠道获取，勿用于商业分发。

> 项目名、产品名与 SDGOODS 标识不在代码许可授权范围内。
