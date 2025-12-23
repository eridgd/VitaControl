cmake_minimum_required(VERSION 3.5)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(lite2_mapper)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 -std=c99 -Wall -Wextra")

include_directories(
  common
)

add_executable(${PROJECT_NAME}
  src/main.c
  common/debugScreen.c
)

target_link_libraries(${PROJECT_NAME}
  SceCtrl_stub
  SceDisplay_stub
  SceIofilemgr_stub
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk LITE20001 ${PROJECT_NAME}.self
  VERSION "01.00"
  NAME "Lite2 Mapper"
)


