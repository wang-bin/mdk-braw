# defines var ffmpeg_dso: the ffmpeg single runtime file path
cmake_minimum_required(VERSION 3.1)
if(POLICY CMP0025)
# MUST call before project() and enable_language
  cmake_policy(SET CMP0025 NEW) # since 3.0, prefer AppleClang instead of Clang. FIXME: ios is still Clang
endif()
if(POLICY CMP0063) # visibility. since 3.3
  cmake_policy(SET CMP0063 NEW)
endif()

project(mdk-braw VERSION 0.11.0 DESCRIPTION "mdk braw plugin") # LANGUAGES C CXX CXXPCH) # VERSION: cmake 3.0+
set(PROJECT_VERSION_TWEAK 0)
if(CMAKE_PROJECT_NAME STREQUAL mdk) # build in source tree
    include_directories(${CMAKE_SOURCE_DIR}/include)
else()
  # TODO: FindMDKAbi
endif()
# https://crascit.com/2015/03/28/enabling-cxx11-in-cmake/ (global and specified target)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)

include(CheckLibraryExists)
include(CheckIncludeFiles)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
# TODO: tools.cmake

set(DEP_LIB_OS ${CMAKE_SOURCE_DIR}/external/lib/${OS})
set(DEP_LIB_TARGET ${CMAKE_SOURCE_DIR}/external/lib/${OS}/${ARCH})
set(DEP_INCLUDE ${CMAKE_SOURCE_DIR}/external/include)
message("DEP_LIB_TARGET: ${DEP_LIB_TARGET}")
include_directories(${DEP_INCLUDE})


if(APPLE)
  set(CMAKE_SHARED_MODULE_SUFFIX ".dylib") # default is so
endif()
add_library(${PROJECT_NAME} MODULE
    BRawReader.cpp
    BRawAPILoader.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_RPATH ON
    FRAMEWORK OFF
    #VERSION ${PROJECT_VERSION} # -current_version can not be applied for MODULE
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME ${PROJECT_NAME}
    CLEAN_DIRECT_OUTPUT 1
  )

target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_MDK_LIB)
target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_PROJECT_NAME})
if(APPLE)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    "-framework CoreFoundation"
    -F${CMAKE_CURRENT_SOURCE_DIR}/sdk/Frameworks "-weak_framework BlackmagicRawAPI")
  # set rpath
elseif(WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE comsuppw) # _bstr_t
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE dl)
endif()


function(setup_mdk_plugin tgt)
  if(APPLE)
    # https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPFrameworks/Concepts/FrameworkAnatomy.html
    list(APPEND RPATH_DIRS @loader_path/.. @loader_path/../.. @loader_path/../../.. @loader_path/../../../..) # plugin is in Plugins, Plugins/foo, mdk.framework/Version/A, mdk.framework/Version/A/Libraries
    foreach(p ${RPATH_DIRS})
      target_link_libraries(${tgt} PRIVATE -Wl,-rpath,\"${p}\")
    endforeach()
    # -install_name @rpath/... is set by cmake
    if(CMAKE_PROJECT_NAME STREQUAL mdk)
      # copy ${tgt} plugin to main dso dir, and modify the copy. original output is untouched for test
      add_custom_command(TARGET ${tgt} POST_BUILD
      # ${tgt} plugin is areadly in versioned dir, so TARGET_LINKER_FILE_NAME is enough, while TARGET_SONAME_FILE_NAME is for regular dso runtime instead of plugin
          COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${tgt}> $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_LINKER_FILE_NAME:${tgt}>
      # change id from absolute path to relative one. a module has no id?
          COMMAND install_name_tool -id @rpath/$<TARGET_LINKER_FILE_NAME:${tgt}> $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_LINKER_FILE_NAME:${tgt}>
      # if plugin is in the same dir as mdk, change absolute path to @rpath/mdk is enough if loader_path is included in rpath and ${tgt} is located in the same dir of dependency
      # TODO: framework Libraries dir, Plugins dir, change to @rpath/mdk.framework/Versions/A/mdk
      COMMAND install_name_tool -change $<TARGET_FILE:${CMAKE_PROJECT_NAME}> @rpath/$<TARGET_SONAME_FILE_NAME:${CMAKE_PROJECT_NAME}>.framework/Versions/A/$<TARGET_SONAME_FILE_NAME:${CMAKE_PROJECT_NAME}> $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_LINKER_FILE_NAME:${tgt}>
      #COMMAND install_name_tool -change $<TARGET_FILE:${CMAKE_PROJECT_NAME}> @rpath/$<TARGET_SONAME_FILE_NAME:${CMAKE_PROJECT_NAME}> $<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>/$<TARGET_LINKER_FILE_NAME:${tgt}>
      )
    endif()
  else()
    install(TARGETS ${tgt}
      RUNTIME DESTINATION bin
      LIBRARY DESTINATION $<IF:$<BOOL:${WIN32}>,bin,lib>
      ARCHIVE DESTINATION lib
      FRAMEWORK DESTINATION lib
      )
  endif()
endfunction(setup_mdk_plugin)

setup_mdk_plugin(${PROJECT_NAME})
