# sdgoods-doom 架构整理与功能演进路线图

> 独立单应用游戏固件。方向已定：**不必完全对齐官方多应用平台**，只保留下拉控制中心（CC）
> 需要的设置即可；官方另提供恢复固件兜底，故允许较激进地裁剪。本文记录整体计划，
> 当前执行焦点 = **阶段 B：DOOM 音效**。

---

## 阶段总览

| 阶段 | 内容 | 风险 | 状态 |
|---|---|---|---|
| A | 架构裁剪与整理（删 WiFi/BLE、清死注释、修 CI、重写 README、删 AI 草稿） | 低 | 待做 |
| **B** | **DOOM 音效（SFX 优先，音乐后置）** | 中 | **进行中** |
| C | 通用 DOOM 播放器（撑 mmap 窗口 + lump 注入，玩不同 IWAD/PWAD） | 中高 | 待做 |

---

## 0. 概念澄清：这不是"GBA 模拟器"

`GBADoom` 是 **PrBoom 血统的 DOOM 引擎**（based on BOOM → id DOOM 1993），当年被编译到 GBA，
现重编译到 ESP32。它跑的是 **DOOM 的 WAD**，不是 GBA ROM。

- ❌ "提炼成能玩 GBA 游戏的模拟器"：那是另一个项目（mGBA / RetroGBa 等），与这套引擎无关，
  ESP32 全速模拟 GBA 也很吃力。
- ✅ 真正可得且有价值的方向（阶段 C）：**通用 DOOM 播放器**——刷不同 DOOM IWAD / 加载 PWAD。

---

## 1. 阶段 A：架构裁剪（已验证安全性）

**保留**（CC 与运行必需）：`sdgoods_board` 核心（lcd / lvgl / input-touch / power / hw_info /
nvs / audio / app_shell / swipe_up / swipe_back / tap / i18n / fonts）+ `sdgoods_launcher`（CC
+ 应用枚举）+ `doom` + `jpegenc`。

**可删 / 可关**：
- 🟢 **WiFi + BLE**：全工程仅 `main.c` 两行 `sdgoods_wifi_init()` / `sdgoods_ble_init()`，
  CC 与 DOOM 都不依赖。删除可省一大块内部 RAM（`sdgoods_lcd.h` 注释亦指出 WiFi RX 静态缓冲
  吃内部 DMA，删了对屏 flush 黑条问题也有益）。
  - 动作：去 2 行 init；board CMakeLists 去 `sdgoods_wifi.c`/`sdgoods_ble.c` + `esp_wifi`/`bt`
    REQUIRES；`sdkconfig*` 去 `CONFIG_BT_*`；main/CMakeLists 去 `esp_wifi`/`esp_netif`/`bt`。
- 🟢 **截屏** `sdgoods_screenshot`：Kconfig 开关（`CONFIG_SDGOODS_SCREENSHOT`），关。
- 🟡 **launcher 多槽**（slot_manifest / slot_cover / device_mode）：CC 的"应用枚举"磁贴仍在用，
  删它要连 CC 磁贴一起改，风险中 → **阶段 A 先不动**。
- 🧹 应用层**模板死注释**（引用不存在的 `ui_home.c`/`app_template.c`/`new_app_project.py`）清理。
- 🧹 **CI 修正**：`.github/workflows/build.yml` + `release.yml` 里引擎分支 `esp32-ai-passport`
  → 应为 `esp32-sdgoods`；`release.yml` 的 `DOOM1_PROCESSED.WAD` → 应为 `DOOM1_GBA.WAD`。
- 🧹 删 `docs/superpowers/plans/*.md`（AI 草稿）；重写 `README.md`（现状：canvas 336×200 而非
  304×168；按键为半透明白色等大圆键、按住式 latch=false、无拖拽点击层，README 描述已过时）。

**验证**：每步 `idf.py -B build_pub build` 编译通过 + 刷 COM6 冒烟（进 DOOM、CC 可下拉、音量滑条可用）。

---

## 2. 阶段 B：DOOM 音效（当前焦点）

### 2.1 现状与根因（已实测确认）
- 引擎 `I_Init()` → `I_InitSound()`，实现来自 `GBADoom/source/i_audio.c`，**整个文件的有效逻辑
  都在 `#ifdef GBA` 内**（maxmod）。ESP32 编译时这些块为空 → `I_StartSound`/`I_PlaySong` 是空壳 → **静音**。
- GBA 音频靠构建期 `mmutil music/*.xx -osoundbank.bin -hsoundbank.h`（Makefile:163）预烘焙的
  **maxmod soundbank**；该 soundbank **不在仓库**（是 GBA 构建产物），也无法在 ESP32 复用。
- ⚠️ **关键实测**：`DOOM1_GBA.WAD`（1282 lumps）**已删除全部音频 lump**——`DS*` 音效 lump = 0、
  `D_*` 音乐 lump = 0（当年为省 ROM，音频全转进 soundbank）。
  → **"从 WAD 读原始 DMX"这条路走不通**，音效只能来自仓库 `music/` 里的素材。

### 2.2 素材来源（唯一可用）
- `GBADoom/music/DS*.wav`：**107 个音效 WAV，共 1.17MB**，全单声道，格式混合：
  `{11025Hz/8bit}`（多数）、`{11025Hz/16bit}`、`{22050Hz/8bit}`。命名 `DSPISTOL.wav` 等。
- `GBADoom/music/*.it`（`Chiptune/`）：音乐 chiptune 模块（**阶段 B 后置，先不做**）。

### 2.3 引擎侧接口（已确认，决定后端怎么写）
- `source/sounds.c` 的 `S_sfx[]`：**name 不带 `DS` 前缀**（如 `"pistol"`、`"shotgn"`）；
  对应 lump/WAV 名 = `"DS" + uppercase(name)`（`DSPISTOL.wav`）。
- `source/s_sound.c`：`S_StartSound*` → `S_StartSoundAtVolume` → 算好音量 `vol`、相位 `sep` →
  调 `I_StartSound(sfx_id, cnum, vol, sep)`（`cnum` 0..7，`MAX_CHANNELS=8`），返回 handle。
  通道分配/生命周期由引擎侧 `s_sound.c` 管，后端对每次 `I_StartSound` 是 **fire-and-forget**
  （从头在该通道播一遍）。本 `i_sound.h` 无 `I_UpdateParams`/`I_StopSound`。

### 2.4 实施方案（SFX 优先）
1. **soundbank 转换器** `tools/gen_soundbank.py`：
   - 读 `music/DS*.wav` → 统一归一为 **8-bit 无符号 mono @ 11025Hz**（16-bit→截 8bit、
     22050→2:1 降采样）。
   - 按 `S_sfx[]` 顺序产出 `doom_sfx.bin`（拼接的 PCM 数据）+ 索引表
     `doom_sfx_index.h`（`{ data_offset, sample_count, rate }` 按 sfx_id 对齐，未命中的置空）。
2. **soundbank 存放**（待定，见 §2.6）——推荐**追加进 appdata 分区、WAD 之后**（与阶段 C 的
   "撑大 mmap 窗口"合并考虑），避开 3MB launcher 分区装不下（2.05MB app + 1.17MB = 3.22MB > 3MB）。
3. **新后端** `components/doom/i_sound_esp32.c`（替换 i_audio.c 在 ESP32 的角色；CMakeLists 里
   排除 `i_audio.c`）：
   - `I_InitSound()`：初始化混音（打开功放、置音量）。
   - `I_StartSound(id, ch, vol, sep)`：把通道 `ch` 指向 soundbank 条目，记 position=0/长度/vol/sep/active。
   - **混音任务**（FreeRTOS，建议 core 1，中优先级）：每个 I2S 周期把 8 通道的 8-bit 样本
     → 16-bit、按 `vol` 缩放、按 `sep` 做左右声道 pan、`11025→16000` 定点重采样 → 立体声 int16 缓冲
     → 写 I2S。**复用板载 `sdgoods_audio` 的 I2S0 TX + PA + 音量**（见下）。
   - `I_PlaySong`/`I_SetMusicVolume` 等：先空实现（音乐后置）。
4. **I2S 接入**（推荐）：给 `sdgoods_board` 的 `sdgoods_audio` 增加一个通用 PCM 写入口
   （如 `sdgoods_audio_write_pcm(const int16_t *stereo, size_t frames)`，内部走已建好的 `s_tx`
   + PA 逻辑），DOOM 混音任务调它。好处：功放时序/防爆音/音量缩放都复用平台成熟代码。
5. **音量 / 静音联动 CC**：混音缩放读取 `sdgoods_audio_get_volume()`（CC 滑条的 `s_vol_pct`）。
6. **音乐（OPL/IT）**：独立里程碑。DOOM 音乐是 DMXMusic(OPL) / 仓库 .it chiptune；ESP32 播放需
   模块播放器（libxmp/mikmod）或 OPL 软合成（Nuked-OPL3/DMXOPL），较重，**先不做**。

### 2.5 风险 / 注意
- **CPU 预算**：混音 + 重采样 + 8 通道叠加要控开销；DOOM 主循环 + LVGL flush 已占两核，混音任务
  优先级要低于 LVGL/输入，避免掉帧。可先 4 通道验证。
- **I2S 通道所有权**：`sdgoods_audio` 的 BGM 与 DOOM 混音都用 I2S0 TX，互斥——DOOM 期绝不启 BGM。
- **爆音**：功放开/关、静音淡入淡出，沿用 `sdgoods_audio` 现成的 fade/PA settle 处理。
- **PSRAM 带宽**：backbuffer 已在 PSRAM；soundbank 若放 flash-mmap 区，读取走 cache，注意与 WAD
  同窗口时的映射上限。
- **循环音效**：部分怪声（如 boss sight）原版要 loop；本 `i_sound.h` 无 loop 接口，MVP 先忽略
  （播一遍），后续再补。

### 2.6 起步前需拍板（3 个决策）
1. **soundbank 存放**：A) 追加进 appdata WAD 之后（推荐，配 mmap 窗口撑大，顺带为阶段 C 铺路）／
   B) 新增独立数据分区（要改 partitions.csv 重刷分区表）／C) 塞进 app bin（**已排除**，3MB 装不下）。
2. **I2S 接入**：扩展 `sdgoods_audio` 加通用 PCM 写入口（推荐）／DOOM 组件独占一路 I2S（要自己管 PA/GPIO）。
3. **首里程碑范围**：只做 SFX（推荐，先听见枪声/开门/怪物）／直接连音乐一起做。

---

## 3. 阶段 C：通用 DOOM 播放器（多 WAD）
- 现状 `components/doom/esp32_wad.c` mmap appdata 头 4.25MB 并校验 `"IWAD"`。
- 要做：① 撑 mmap 窗口到分区上限（DOOM2/Ultimate ~11MB，需 32-bit flash mmap，最大到 appdata 16MB）；
  ② **lump 注入工具**——引擎硬依赖 GBA UI 补丁 lump（`STGANUM0-9`/`M_ARUN`/`M_GAMMA`），
  纯净 IWAD 缺这些会崩，须脚本注入（或对缺失做回退）；③ 支持 `-file` 加 PWAD（多段 mmap）；
  ④ 若阶段 B 选了"soundbank 追加 appdata"，此处统一规划 appdata 布局（WAD + SFX bank）。
- 注意：阶段 B 已证 `DOOM1_GBA.WAD` **无音频 lump**，其它 IWAD 若含 DMX 音效，后端需同时支持
  "WAD 内 DMX" 与 "外置 soundbank" 两条取声路径（或统一都走外置 bank）。

---

## 4. 关键事实速查（本会话实测）
- `DOOM1_GBA.WAD`：ID `IWAD`，1282 lumps，目录偏移 4258329，**DS\* 音效 0 个、D_\* 音乐 0 个**。
- `music/DS*.wav`：107 个 / 1.17MB / mono / {11025-8, 11025-16, 22050-8}。
- 板载扬声器：I2S_NUM_0，16-bit **立体声**，默认 16000Hz，PA 功放 GPIO + 全局音量 `s_vol_pct`。
- 引擎接口：`I_StartSound(sfx_id, cnum, vol, sep)`，`MAX_CHANNELS 8`，`S_sfx[].name` 无 `DS` 前缀。
