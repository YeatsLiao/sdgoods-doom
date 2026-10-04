# 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
# 谷仓次元屏（谷仓电子徽章）固件 —— ESP32-S3 + ST77916 QSPI + LVGL
# Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
#
# 工程名决定固件产物名：编译后得到 build*/SDGOODS_DOOM.bin（烧到 0x10000）。
# EBADGE = Electronic Badge（电子徽章）—— 与产品名「谷仓次元屏（谷仓电子徽章）」一致。
cmake_minimum_required(VERSION 3.16)
# GBADoom 引擎源码树（分支 esp32-ai-passport）。默认同级 ../GBADoom；
# CI / 特殊路径可用 -DGBADOOM_PATH=<dir> 覆盖。
if(NOT DEFINED GBADOOM_PATH)
  set(GBADOOM_PATH "${CMAKE_CURRENT_LIST_DIR}/../GBADoom" CACHE PATH "GBADoom source tree")
endif()
if(NOT EXISTS "${GBADOOM_PATH}/source/d_main.c")
  message(FATAL_ERROR "GBADOOM_PATH=${GBADOOM_PATH} 无效。请先放置 GBADoom(esp32-ai-passport) 源码树")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SDGOODS_DOOM)

# 构建期把 GBADoom z_zone.c 的大缓冲(overflow_buffer)从内部 .bss 迁到 PSRAM。
# S3 内部 DRAM 要留给 LVGL 绘制缓冲(必须在 SRAM)+WiFi/BLE/音频，否则 dram0 链接溢出。
# 幂等；随源码提交，保证 CI 干净 checkout GBADoom 后也得一致固件。
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/components/doom/patch_gbadoom.py
            ${GBADOOM_PATH}/source/z_zone.c
    RESULT_VARIABLE _doom_patch_rc
    OUTPUT_VARIABLE _doom_patch_out
    ERROR_VARIABLE  _doom_patch_err
)
if(_doom_patch_out)
    message(STATUS "gbadoom-patch: ${_doom_patch_out}")
endif()
if(NOT _doom_patch_rc EQUAL 0)
    message(FATAL_ERROR "GBADoom z_zone.c 补丁失败: ${_doom_patch_err}")
endif()
