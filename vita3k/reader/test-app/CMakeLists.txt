# SPDX-FileCopyrightText: Vita3K Reader fork contributors
# SPDX-License-Identifier: GPL-2.0-or-later

# Core check (RDRV00090): a small Vita app for trying this fork's core, which needs no firmware
# (Vita3K runs it with its own system functions). Built with VitaSDK (source tools/vita/env.sh at
# the top of the repository), not by the core's build: vita3k/AGENTS.md, "Trying it".
cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "")
endif()

project(reader_core_check C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Core check")
set(VITA_TITLEID  "RDRV00090")
set(VITA_VERSION  "01.00")

add_executable(core_check main.c)
target_compile_options(core_check PRIVATE -O2 -mthumb)
target_link_libraries(core_check
  vita2d SceAppMgr_stub SceAudio_stub SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
  SceTouch_stub ScePgf_stub SceCommonDialog_stub SceAppUtil_stub ScePower_stub
  freetype png jpeg z m c)

vita_create_self(eboot.bin core_check UNSAFE)
vita_create_vpk(core_check.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME})
