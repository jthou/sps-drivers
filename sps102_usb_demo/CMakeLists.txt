cmake_minimum_required(VERSION 3.15)

# project name
project(sps102_usb_demo)

if(NOT WIN32)
  message(SEND_ERROR "This project does not support non-Windows operating systems")
  return()
endif()

# set source
set(SOURSES main.cpp)

add_executable(${PROJECT_NAME} ${SOURSES})
# Copy the dynamic library to the build directory
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64-bit build
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib64/FTD3XX.dll DESTINATION ${PROJECT_BINARY_DIR})
else()
    # 32-bit build
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib/FTD3XX.dll DESTINATION ${PROJECT_BINARY_DIR})
endif()