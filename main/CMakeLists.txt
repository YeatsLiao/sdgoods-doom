# main/ —— 应用层（二次开发主要在这里）
#
# 硬件抽象层（屏 / 触摸 / 音频 / LVGL 移植 / 应用框架 / 字体）是独立组件：
#   components/bsp/
# 应用层通过 REQUIRES bsp 使用它，一行 #include "bsp.h" 即可。

idf_component_register(
    SRCS
        "main.c"

        # --- 应用层小工具（不注册成「应用」，只被 main.c / 页面调用） ---
        # （纯 DOOM 工程：app_data_store 已删——appdata 分区头部被 WAD 裸数据占用，不可挂 FAT）
        # --- 游戏接线（应用注册表 + DOOM 首屏） ---
        "game/apps_registry.c"
        "game/ui_doom.c"

    INCLUDE_DIRS
        "game"                  # 让 #include "ui_doom.h" 直接可用
        "."                     # 让 #include "app_data_store.h" 直接可用

    REQUIRES
        doom_engine
        bsp
        control_center
        driver
        esp_event
        esp_netif
        esp_timer
        freertos
        hal
        heap
        log
        lvgl
        nvs_flash
)

# ---- 自动编译版本号 (MMDDHHMM)，每次编译刷新 ----
# 生成 build_version.h 到组件构建目录，并让组件库在编译前依赖它，
# 同时把构建目录加入头文件搜索路径，使 game/ 下的源文件能 #include "build_version.h"。
set(_bv_dir ${CMAKE_CURRENT_BINARY_DIR})
add_custom_target(gen_build_version ALL
    COMMAND ${CMAKE_COMMAND}
        -D BIN_DIR=${_bv_dir}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_build_version.cmake
    BYPRODUCTS ${_bv_dir}/build_version.h
    COMMENT "Generating build version stamp (MMDDHHMM)"
)
add_dependencies(${COMPONENT_LIB} gen_build_version)
target_include_directories(${COMPONENT_LIB} PRIVATE ${_bv_dir})

# ---- 应用第三方组件补丁（LVGL 的 GIF canvas 走 PSRAM 等）----
# managed_components/ 不入库、由组件管理器下载，所以补丁放在 main/patches/ 随源码提交，
# 每次 configure 幂等地覆盖过去 —— 保证「clone 后直接编译」得到与发布版一致的固件。
# 详见 main/patches/README.md。该步失败会让 configure 直接报错（有意为之）。
idf_build_get_property(_patch_python PYTHON)
execute_process(
    COMMAND ${_patch_python} ${CMAKE_CURRENT_SOURCE_DIR}/patches/apply_lvgl_patches.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    RESULT_VARIABLE _lvgl_patch_rc
    OUTPUT_VARIABLE _lvgl_patch_out
    ERROR_VARIABLE  _lvgl_patch_err
)
if(_lvgl_patch_out)
    message(STATUS "${_lvgl_patch_out}")
endif()
if(NOT _lvgl_patch_rc EQUAL 0)
    message(FATAL_ERROR "LVGL 补丁应用失败: ${_lvgl_patch_err}")
endif()
