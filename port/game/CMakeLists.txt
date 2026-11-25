cmake_minimum_required(VERSION 3.16)
# NEO Scavenger Vita: el juego traducido (as3cpp) + runtime + backend vitaGL.
# Uso: cmake -S port/game -B build_vita -G Ninja -DGEN_DIR=<work/gen17>
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  endif()
endif()
project(neoscav_game CXX C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
if(NOT GEN_DIR)
  set(GEN_DIR ${ROOT}/work/gen17)
endif()
# -O2 sin rastreo de depuracion (AS3_TRACE solo en PC). -w: el codigo generado produce muchos avisos inofensivos.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -w -fno-lto -Wl,-q")
# NEOS_TRACE=ON: rastreo de funciones AS3 + muestreador en la consola (para perfilar en la Vita; mas lento)
option(NEOS_TRACE "Rastreo de funciones AS3 en la Vita" OFF)
if(NEOS_TRACE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DAS3_TRACE")
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -Wl,-q")

set(TITLE_ID "NEOS00001")
set(VERSION "00.03")

file(GLOB GEN_SOURCES ${GEN_DIR}/*.cpp)
add_executable(neoscavgame ${CMAKE_CURRENT_SOURCE_DIR}/main_game_vita.cpp ${ROOT}/runtime/minimp3_impl.cpp ${ROOT}/runtime/fastalloc.cpp ${GEN_SOURCES})
set_source_files_properties(${ROOT}/runtime/minimp3_impl.cpp ${ROOT}/runtime/fastalloc.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_include_directories(neoscavgame PRIVATE ${ROOT}/runtime ${GEN_DIR})
target_precompile_headers(neoscavgame PRIVATE ${GEN_DIR}/all.h)
target_link_libraries(neoscavgame
  vitaGL vitashark mathneon SceShaccCgExt SceKernelDmacMgr_stub taihen_stub
  SceLibKernel_stub SceKernelThreadMgr_stub SceSysmem_stub
  SceCtrl_stub SceTouch_stub SceDisplay_stub SceGxm_stub SceRtc_stub SceSysmodule_stub
  SceAppMgr_stub SceCommonDialog_stub SceAudio_stub SceFios2_stub SceShaccCg_stub
  ScePower_stub SceKernelModulemgr_stub ScePgf_stub SceAppUtil_stub SceIofilemgr_stub SceProcessmgr_stub
  z pthread m c
)

vita_create_self(eboot.bin neoscavgame UNSAFE)
vita_create_vpk(neoscavgame.vpk ${TITLE_ID} eboot.bin
  VERSION ${VERSION}
  NAME "NEO Scavenger Vita"
  FILE ${ROOT}/port/sce_sys/icon0.png sce_sys/icon0.png
  FILE ${ROOT}/port/sce_sys/pic0.png sce_sys/pic0.png
  FILE ${ROOT}/port/sce_sys/livearea/contents/bg0.png sce_sys/livearea/contents/bg0.png
  FILE ${ROOT}/port/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE ${ROOT}/port/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
)
