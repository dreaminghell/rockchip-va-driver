cmake_minimum_required(VERSION 2.6)
find_package(PkgConfig)

PROJECT(rockchip_drv_video C)
INCLUDE(GNUInstallDirs)

pkg_search_module(LIBVA libva)
string(REPLACE "." ";" LIBVA_VERSION_LIST ${LIBVA_VERSION})
list(GET LIBVA_VERSION_LIST 0 VA_MAJOR_VERSION)
list(GET LIBVA_VERSION_LIST 1 VA_MINOR_VERSION)

ADD_DEFINITIONS(-Os -Wall --std=gnu99 -g3 -Wmissing-declarations)

set(VA_DRIVER_INIT_FUNC "__vaDriverInit_${VA_MAJOR_VERSION}_${VA_MINOR_VERSION}")
CONFIGURE_FILE("${CMAKE_CURRENT_SOURCE_DIR}/src/include/config.h.cmake"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/include/config.h")

ADD_SUBDIRECTORY (librkvpu)

ADD_LIBRARY(rockchip_drv_video SHARED 
src/rockchip_drv_video.c src/object_heap.c
src/rockchip_buffer.c src/rockchip_encoder.c
src/rockchip_image.c src/rockchip_picture.c
src/rockchip_surface.c src/v4l2_enc_utils.c
)

TARGET_LINK_LIBRARIES(rockchip_drv_video ${LIBVA_LIBRARIES} rkvpu pthread)
TARGET_INCLUDE_DIRECTORIES(rockchip_drv_video PUBLIC 
"${LIBVA_INCLUDE_DIRS}"
"${CMAKE_CURRENT_SOURCE_DIR}/src/include"
"${CMAKE_CURRENT_SOURCE_DIR}/librkvpu/inc"
)

TARGET_COMPILE_OPTIONS(rockchip_drv_video PUBLIC ${LIBVA_CFLAGS})
SET_TARGET_PROPERTIES(rockchip_drv_video PROPERTIES PREFIX "")

INSTALL(TARGETS rockchip_drv_video LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/dri")
