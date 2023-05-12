cmake_minimum_required(VERSION 3.0.0)
project(sps102_usb_demo VERSION 0.1.0)

include(CTest)
enable_testing()

add_definitions(-D_AFXDLL) #enables MFC
set(CMAKE_MFC_FLAG 2)      #use shared MFC library

set(FTDemo_SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../FT600DataStreamerDemoApp_v1.3.0.4/source/DataStreamerDemoApp)
include_directories(${FTDemo_SRC_ROOT})
link_directories(${FTDemo_SRC_ROOT}/FTD3XXLibrary/x64)
add_executable(sps102_usb_demo  ${FTDemo_SRC_ROOT}/DRV_DriverInterface.cpp  
                                ${FTDemo_SRC_ROOT}/APP_Logger.cpp
                                main.cpp)


TARGET_LINK_LIBRARIES(sps102_usb_demo FTD3XX)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
