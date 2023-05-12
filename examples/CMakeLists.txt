cmake_minimum_required(VERSION 3.0.0)
project(driver_examples VERSION 0.1.0)

set(EXE "ftd3xx_test")
include_directories(${PROJECT_SOURCE_DIR}/../FTD3XXLibrary/win/include)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # 64-bit build
    link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib64)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib64/FTD3XX.dll DESTINATION ${PROJECT_BINARY_DIR})
else()
    # 32-bit build
    link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib)
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib/FTD3XX.dll DESTINATION ${PROJECT_BINARY_DIR})
endif()
add_executable(${EXE} ftd3xx_test.cpp)
target_link_libraries(${EXE} PRIVATE FTD3XX)



