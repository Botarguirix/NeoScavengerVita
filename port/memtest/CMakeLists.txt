cmake_minimum_required(VERSION 3.16)
# VPK de pruebas de memoria, carga de imagenes y dibujo (NEOS00002). No contiene datos del juego.
# Uso: cmake -S port/memtest -B build_memtest -G Ninja && ninja -C build_memtest
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  endif()
endif()
project(neos_memtest CXX C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(CMAKE_CXX_STANDARD 17)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -Wl,-q")

add_executable(memtest main_memtest.cpp)
target_include_directories(memtest PRIVATE ${ROOT}/runtime)
target_link_libraries(memtest
  vitaGL vitashark mathneon SceShaccCgExt SceKernelDmacMgr_stub taihen_stub
  SceLibKernel_stub SceKernelThreadMgr_stub SceSysmem_stub
  SceCtrl_stub SceTouch_stub SceDisplay_stub SceGxm_stub SceRtc_stub SceSysmodule_stub
  SceAppMgr_stub SceCommonDialog_stub SceAudio_stub SceFios2_stub SceShaccCg_stub
  ScePower_stub SceKernelModulemgr_stub ScePgf_stub SceAppUtil_stub SceIofilemgr_stub SceProcessmgr_stub
  z pthread m c
)
vita_create_self(eboot.bin memtest UNSAFE)
vita_create_vpk(memtest.vpk NEOS00002 eboot.bin
  VERSION 00.01
  NAME "NEOS Prueba memoria"
  FILE ${ROOT}/port/sce_sys/icon0.png sce_sys/icon0.png
)
