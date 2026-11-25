cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  endif()
endif()
project(neoscav_vita CXX C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wl,-q -fno-lto")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wl,-q -fno-lto -fno-exceptions-dummy")
string(REPLACE "-fno-exceptions-dummy" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

set(TITLE_ID "NEOS00001")
set(VERSION "00.02")

add_executable(neoscavvita src/main_vita.cpp)
target_link_libraries(neoscavvita
  vitaGL vitashark mathneon SceShaccCgExt SceKernelDmacMgr_stub taihen_stub
  SceLibKernel_stub SceKernelThreadMgr_stub SceSysmem_stub
  SceCtrl_stub SceTouch_stub SceDisplay_stub SceGxm_stub SceRtc_stub SceSysmodule_stub
  SceAppMgr_stub SceCommonDialog_stub SceAudio_stub SceFios2_stub SceShaccCg_stub
  ScePower_stub SceKernelModulemgr_stub ScePgf_stub SceAppUtil_stub SceNet_stub SceNetCtl_stub
  SceIofilemgr_stub SceProcessmgr_stub
  pthread m c
)

vita_create_self(neoscavvita.self neoscavvita UNSAFE)
vita_create_vpk(neoscavvita.vpk ${TITLE_ID} neoscavvita.self
  VERSION ${VERSION}
  NAME "NEO Scavenger Vita"
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/pic0.png sce_sys/pic0.png
  FILE sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
