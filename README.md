# 谷仓次元屏 · DOOM（sdgoods-doom）

> 把 **DOOM** 移植到 SDGOODS 谷仓次元屏（ESP32-S3 圆形触摸屏，无物理按键），
> 用屏幕上的 **GBA 布局虚拟按键** 操作。独立单应用固件，开机直进游戏。

这是从谷仓 SDGOODS 开放平台基础工程派生出的**纯 DOOM 工程**：只保留跑 DOOM 所需的
平台 BSP（`components/sdgoods_board`、`sdgoods_launcher`、`jpegenc`）+ 引擎胶水层 +
一个 LVGL 应用外壳，官方模板示例（主页启动台 / 小鸟 / 扫描示例）与发布链路脚本已全部剔除。

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

- 引擎：**GBADoom**（YeatsLiao fork 的 `esp32-ai-passport` 分支，PrBoom 血统，纯 C）
- 内部分辨率 **240×160**、8bpp 调色板索引，本工程经最近邻放大到 canvas **304×168** 居中显示（角点距圆心 173 < 180，落在圆屏可视区）
- 帧链路：引擎任务（core 0）渲染索引帧 → 置 `frame_ready` → LVGL 线程（apps_poll）转 RGB565 写 PSRAM canvas → `lv_obj_invalidate` → 平台 SRAM 条带 flush 上屏
  - **不直刷 SPI**：绕开平台架构红线（QSPI DMA 缓冲不能取 PSRAM，否则黑条/红线）
- WAD：`DOOM1_GBA.WAD`（**完整原版内容** + 引擎必需的 GBA UI 补丁 lump：`STGANUM0-9`/`M_ARUN`/`M_GAMMA`，4,278,841 字节，未删任何关卡素材）裸烧进 `appdata` 分区头部 `0x1000000`，`esp_partition_mmap` 4.25MB 窗口直接读取，**不挂 FAT**
  - 说明：passport 早期为塞进 3.75MB 窗口做过削小的 `DOOM1_PROCESSED.WAD`（3.72MB，删了素材）；本工程已撑大窗口，改用未删减的 `DOOM1_GBA.WAD`。纯净 id 原版 `DOOM1.WAD` 跑不了——引擎硬依赖 `STGANUM`（HUD 数字）与 `M_ARUN`/`M_GAMMA`（菜单项）这几个补丁 lump。

## 输入：单指可玩的 GBA 虚拟键

CST816 是单点触摸，无法同时按多键，故为"单指"重新设计：

| 虚拟键 | 语义 | 触发方式 |
|---|---|---|
| ▲ ▼（十字键） | 前进 / 后退 | **锁存**：点一下持续走，再点取消；上下互斥 |
| ◀ ▶（十字键） | 左转 / 右转 | 锁存，左右互斥 |
| **A** | 开火（→ KEYD_B） | 点按 |
| **B** | 使用 / 开门（→ KEYD_A） | 点按 |
| L / R | 切换武器 | 点按 |
| ST / SE | 菜单 / SELECT | 点按 |
| 画面拖拽 | 瞬时转向（→ KEYD_LEFT/RIGHT） | 在 canvas 上左右拖，松手即停 |
| 画面单击 | 开火脉冲 | canvas 上近似原地点击 |

组合：**锁存方向键持续移动** + **另一时间拖拽/单击转向开火**，单指即可完成"边走边打"。
按键 ↔ 引擎键码经共享位掩码 `g_doom_host.btn_mask` 传递，引擎侧 `I_ProcessKeyEvents` 做边沿检测后 `D_PostEvent`。

---

## 编译

依赖 ESP-IDF **v5.5** + GBADoom 源码树。

```bash
# 1) 放置 GBADoom（esp32-ai-passport 分支），与本工程同级即可
git clone -b esp32-ai-passport https://github.com/YeatsLiao/GBADoom ../GBADoom

# 2) 编译（默认找同级 ../GBADoom，也可用 -DGBADOOM_PATH 指定）
. $IDF_PATH/export.sh
idf.py -B build_pub build
#   路径不同：idf.py -B build_pub build -DGBADOOM_PATH=/path/to/GBADoom
```

> 构建期会自动对 GBADoom 的 `z_zone.c` 打一处**幂等补丁**（`components/doom/patch_gbadoom.py`），
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
# WAD → appdata 头部
esptool.py --chip esp32s3 -p <PORT> -b 921600 write_flash 0x1000000 DOOM1_GBA.WAD
```

> `idf.py flash` 默认就烧 `build_pub/bootloader/`（自编、含 32 位映射），**不要**手动改回平台
> `prebuilt/bootloader.bin`。`tools/flash_local.sh` 也已改为使用 build 目录内的自编引导层。

或一键（Linux/macOS）：`python tools/flash_local.sh -p <PORT> -B 921600`
（该脚本已串好 固件 + 擦 otadata + 烧 WAD 三步）。

分区表沿用平台托管版本，**不改动**：4 个 OTA 槽各 3MB + `appdata` 16MB @`0x1000000` + `otadata` @`0x310000`。

---

## 目录结构

```
sdgoods-doom/
├── CMakeLists.txt              # 定义 GBADOOM_PATH + 构建期打 GBADoom 补丁
├── DOOM1_GBA.WAD               # 完整原版 + GBA UI 补丁（引擎必需 lump，未删减）
├── components/
│   ├── doom/                   # ★ 引擎组件（GBADoom 源码 + SDGOODS 平台层）
│   │   ├── CMakeLists.txt      #   GLOB 引擎源 + 排除表 + --wrap=clock
│   │   ├── patch_gbadoom.py    #   构建期把 overflow 缓冲迁 PSRAM（幂等）
│   │   ├── doom_host.h         #   引擎 ⇄ UI 唯一契约（btn_mask / frame_ready）
│   │   ├── i_system_sdgoods.c  #   平台层：backbuffer(PSRAM)/调色板/键边沿检测
│   │   └── esp32_wad.c         #   从 appdata mmap WAD
│   ├── sdgoods_board/          #   平台 BSP（屏/触摸/音频/LVGL/字体）— 勿动
│   ├── sdgoods_launcher/       #   平台应用框架 — 勿动
│   └── jpegenc/
├── main/
│   ├── main.c                  #   boot-direct 首屏直进 DOOM
│   └── apps/
│       ├── apps_registry.c     #   应用注册表（仅 DOOM 一条）
│       └── ui_doom.[ch]        #   ★ LVGL 外壳：canvas + GBA 虚拟键 + 帧提交
├── platform/                   #   分区表 + 预编译引导层（平台托管，勿改）
├── tools/flash_local.sh        #   本地一键烧录
└── .github/workflows/          #   CI：clone GBADoom + -DGBADOOM_PATH 构建
```

## 真机验证结果（ESP32-S3 次元屏，COM6 实刷）

固件已刷入真机并跑通，串口日志 + 截图实测确认：

- ✅ **WAD mmap 成功**：`doom_wad: WAD mmap ok: addr=0x1000000 map=3840KB`（开 32 位 cache 后）。
- ✅ **引擎启动**：`PrBoom / Playing: DOOM Shareware`，`W_Init`/`R_Init` 正常，`backbuffer 38400B @ PSRAM`。
- ✅ **帧率 ~32 fps**：`doom_ui: 32 fps`（远超 12fps 目标，无需再降 canvas）。
- ✅ **色彩正确**：截屏中 HUD 血量“50”为红、护甲“100%”为绿——证明 `I_SetPallete_e32`
  已按 `LV_COLOR_16_SWAP=y` 做了一次 bswap（平台 `sdgoods_screenshot.c` 注释印证 LVGL 缓冲高字节在前）。
- ✅ **GBA 虚拟键布局**：上排 L/ST/R + 下排十字键/A/B/SE 均在圆屏可视区内（经弦宽校验）。

待用户上手验收（截图无法验证触摸）：

- **触摸输入**：点 A 开火 / 拖画面转向 / 锁存方向键持续移动是否顺手。若 tap 开火脉冲漏按，
  把 `ui_doom.c` 的 `ui_doom_poll` 脉冲改为时间保持（`esp_timer` 持续 ≥ 一帧周期）。
- **A/B 语义**：若开火/使用反了，对调 `i_system_sdgoods.c` 的 `s_keymap` 两行。
- **canvas 顶角**：304 宽时顶角距圆心 ≈192 > 180，真机圆屏会裁掉画面顶部两角（该处为暗色天空，
  影响轻微）；如需完整可把 `CANVAS_W` 收到 ≤271。
- **MVP 未做**：音频、游戏内退出到桌面、BLE 手柄、上架开放平台商店。

## 许可

- **引擎相关**（`components/doom/*` + 链接的 GBADoom）：GBADoom/DOOM 引擎为 **GPL-2.0-or-later**，
  合并固件产物按 GPL 条款分发。
- **平台 BSP 组件**（`components/sdgoods_board`、`sdgoods_launcher`、`jpegenc`）：**Apache-2.0**，
  版权归深圳希德创新网络有限公司（SDGOODS）。
- **应用外壳**（`main/*`）：源自 SDGOODS 基础工程，Apache-2.0 声明见文件头；随 GPL 引擎合并后整体按 GPL 分发。
- **`DOOM1_GBA.WAD`**：游戏数据，版权归 **id Software**。本仓库沿用 passport 做法，仅以
  **shareware（DOOM1）版本**随固件分发，供合法试用；请自行通过官方渠道获取，勿用于商业分发。

> 项目名、产品名与 SDGOODS 标识不在代码许可授权范围内。
