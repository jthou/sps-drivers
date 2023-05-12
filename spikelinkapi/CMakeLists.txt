cmake_minimum_required(VERSION 3.0.0)
project(spikelinkapi VERSION 0.1.0)

#message( STATUS "compiling " ${PROJECT_NAME})
#if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
#    message( STATUS "the system is Linux ... O.K.")
#else()
#    message( STATUS "the system is: ${CMAKE_SYSTEM_NAME} ... NOT O.K. !!! " )
#endif()

set(CMAKE_CXX_STANDARD 17)

include(CTest)
enable_testing()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SHARED_LIB_SOURCES
    spikelinkapi.cpp 
    spikelinkinput.cpp 
    DRV_DriverInterface.cpp
    SpikeLinkusb3.cpp
    refcount.cpp)

set( EXE_SOURCES
    main.cpp refcount.cpp)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
add_definitions(-D_HAS_STD_BYTE=0)
add_definitions(-DSV_EXPORTS)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

#set(ROOT_DIR ${CMAKE_SOURCE_DIR})
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/../JR6101_v2/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/../FTD3XXLibrary/win/include)
INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})
link_directories(${PROJECT_SOURCE_DIR}/../FTD3XXLibrary/win/lib64)

add_library(spikelinkapi SHARED ${SHARED_LIB_SOURCES})
TARGET_LINK_LIBRARIES(spikelinkapi FTD3XX)
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    TARGET_LINK_LIBRARIES(spikelinkapi pthread stdc++fs dl)
endif()

add_executable(spikelinkapitest ${EXE_SOURCES})
TARGET_LINK_LIBRARIES(spikelinkapitest spikelinkapi)
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    TARGET_LINK_LIBRARIES(spikelinkapitest pthread stdc++fs dl) 
endif()


# Copy the dynamic library to the build directory
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../JR6101_v2/lib/libhda100.so DESTINATION ${PROJECT_BINARY_DIR})
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib64/FTD3XX.dll DESTINATION ${PROJECT_BINARY_DIR})
    else()
        # 32-bit build
        file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../FTD3XXLibrary/win/lib/FTD3XX.dll DESTINATION ${PROJECT_BINARY_DIR})
    endif()
endif()