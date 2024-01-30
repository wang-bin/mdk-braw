cmake_minimum_required(VERSION 3.15)

project(mdk-braw VERSION 0.16.0 DESCRIPTION "mdk braw plugin")
set(PROJECT_VERSION_TWEAK 0)

set(MDKSDK "${CMAKE_CURRENT_SOURCE_DIR}/mdk-sdk" CACHE STRING "libmdk SDK dir")

if(NOT CMAKE_PROJECT_NAME STREQUAL mdk) # not build in source tree
    list(APPEND CMAKE_MODULE_PATH ${MDKSDK}/lib/cmake)
endif()
find_package(MDK)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads)
link_libraries(Threads::Threads)

setup_mdk_plugin(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE
    BRawReader.cpp
    BRawAPILoader.cpp
    Variant.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    #VERSION ${PROJECT_VERSION} # -current_version can not be applied for MODULE
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME ${PROJECT_NAME}
    CLEAN_DIRECT_OUTPUT 1
  )

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
if(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    "-framework CoreFoundation" "-framework Metal")
  # set rpath
# TODO: iOS: weak link,  DecoderMetal.framework
elseif(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE OleAut32)
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif()

if(TARGET cppcompat) # requires https://github.com/wang-bin/cppcompat
  target_link_libraries(${PROJECT_NAME} PRIVATE cppcompat)
endif()