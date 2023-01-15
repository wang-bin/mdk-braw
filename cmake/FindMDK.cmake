# - Try to find MDK SDK and Abi
#
# MDK_FOUND - system has MDK
# MDK_INCLUDE_DIRS - the MDK include directory
# MDK_LIBRARIES - The MDK libraries
# MDK_VERSION_STRING -the version of MDK SDK found
#
# target_link_libraries(tgt PRIVATE mdk) will add all flags

if(POLICY CMP0063) # visibility. since 3.3
  cmake_policy(SET CMP0063 NEW)
endif()

if(APPLE)
  set(CMAKE_SHARED_MODULE_SUFFIX ".dylib") # default is so
endif()

if(CMAKE_PROJECT_NAME STREQUAL mdk) # build in source tree
  # https://crascit.com/2015/03/28/enabling-cxx11-in-cmake/ (global and specified target)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  #set(CMAKE_CXX_EXTENSIONS OFF)
endif()


function(setup_mdk_plugin tgt)
    target_compile_definitions(${tgt} PRIVATE BUILD_MDK_LIB)
    target_link_libraries(${tgt} PRIVATE LibmdkAbi)
    if(APPLE)
      set_target_properties(${tgt} PROPERTIES
        MACOSX_RPATH ON
        FRAMEWORK OFF
      )
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

# Compute the installation prefix relative to this file.
get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
if(_IMPORT_PREFIX STREQUAL "/")
  set(_IMPORT_PREFIX "")
endif()


if(ANDROID_ABI)
  set(_IMPORT_ARCH ${ANDROID_ABI})
elseif(CMAKE_ANDROID_ARCH_ABI)
  set(_IMPORT_ARCH ${CMAKE_ANDROID_ARCH_ABI})
elseif(CMAKE_C_COMPILER_ARCHITECTURE_ID) # msvc
  set(_IMPORT_ARCH ${CMAKE_C_COMPILER_ARCHITECTURE_ID}) # ARMV7 ARM64 X86 x64
elseif(WIN32)
  set(_IMPORT_ARCH ${CMAKE_SYSTEM_PROCESSOR})
elseif(CMAKE_SYSTEM_NAME STREQUAL Linux)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "ar.*64")
    set(_IMPORT_ARCH arm64)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    set(_IMPORT_ARCH armhf)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "64")
    set(_IMPORT_ARCH amd64)
  endif()
endif()
string(TOLOWER "${_IMPORT_ARCH}" _IMPORT_ARCH)
if(WIN32)
  if(_IMPORT_ARCH MATCHES armv7) #msvc
    set(_IMPORT_ARCH arm)
  elseif(_IMPORT_ARCH MATCHES amd64) #msvc
    set(_IMPORT_ARCH x64)
  endif()
endif()


if(CMAKE_PROJECT_NAME STREQUAL mdk)
  set(_IMPORT_PREFIX ${MDK_SOURCE_DIR}) #
endif()

#list(APPEND CMAKE_FIND_ROOT_PATH ${_IMPORT_PREFIX})
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH) # for cross build, find paths out sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH) # for cross build, find paths out sysroot
if(CMAKE_PROJECT_NAME STREQUAL mdk)
  set(MDK_INCLUDE_DIR ${_IMPORT_PREFIX}/include)
  set(MDK_ABI_INCLUDE_DIR ${_IMPORT_PREFIX}/include)
else()
  find_path(MDK_INCLUDE_DIR mdk/global.h PATHS ${_IMPORT_PREFIX}/include)
  find_path(MDK_ABI_INCLUDE_DIR mdk/global.h PATHS ${_IMPORT_PREFIX}/include/abi)
endif()

find_library(MDK_LIBRARY NAMES mdk libmdk PATHS ${_IMPORT_PREFIX}/lib/${_IMPORT_ARCH}) # FIXME: may select host library
if(MDK_LIBRARY)
  if(APPLE)
    set(MDK_LIBRARY ${MDK_LIBRARY}/mdk) # was .framework, IMPORTED_LOCATION is file path
  endif()
else()
  if(APPLE)
    set(MDK_XCFWK ${_IMPORT_PREFIX}/lib/mdk.xcframework)
    if(EXISTS ${MDK_XCFWK})
      if(IOS)
        if(${CMAKE_OSX_SYSROOT} MATCHES Simulator)
          file(GLOB MDK_FWK LIST_DIRECTORIES true ${MDK_XCFWK}/ios-*-simulator)
        else()
          file(GLOB MDK_FWK LIST_DIRECTORIES true ${MDK_XCFWK}/ios-arm*)
        endif()
      else()
        file(GLOB MDK_FWK LIST_DIRECTORIES true ${MDK_XCFWK}/macos-*)
      endif()
      if(EXISTS ${MDK_FWK})
        set(MDK_LIBRARY ${MDK_FWK}/mdk.framework/mdk)
      endif()
    endif()
  endif()
endif()


set(MDK_INCLUDE_DIRS ${MDK_INCLUDE_DIR})
set(MDK_ABI_INCLUDE_DIRS ${MDK_ABI_INCLUDE_DIR})
set(MDK_LIBRARIES ${MDK_LIBRARY})
mark_as_advanced(MDK_INCLUDE_DIRS MDK_ABI_INCLUDE_DIRS MDK_LIBRARIES)

if(MDK_INCLUDE_DIR AND EXISTS "${MDK_INCLUDE_DIR}/mdk/c/global.h")
  file(STRINGS "${MDK_INCLUDE_DIR}/mdk/c/global.h" mdk_version_str
       REGEX "^#[\t ]*define[\t ]+MDK_(MAJOR|MINOR|MICRO)[\t ]+[0-9]+$")

  unset(MDK_VERSION_STRING)
  foreach(VPART MAJOR MINOR MICRO)
    foreach(VLINE ${mdk_version_str})
      if(VLINE MATCHES "^#[\t ]*define[\t ]+MDK_${VPART}[\t ]+([0-9]+)$")
        set(MDK_VERSION_PART "${CMAKE_MATCH_1}")
        if(DEFINED MDK_VERSION_STRING)
          string(APPEND MDK_VERSION_STRING ".${MDK_VERSION_PART}")
        else()
          set(MDK_VERSION_STRING "${MDK_VERSION_PART}")
        endif()
        unset(MDK_VERSION_PART)
      endif()
    endforeach()
  endforeach()
endif()

include(FindPackageHandleStandardArgs)

if(CMAKE_PROJECT_NAME STREQUAL mdk)
  if(NOT TARGET LibmdkAbi)
    add_library(LibmdkAbi INTERFACE)
    target_include_directories(LibmdkAbi INTERFACE ${MDK_ABI_INCLUDE_DIRS} ${MDK_SOURCE_DIR}/external/include)
    target_link_libraries(LibmdkAbi INTERFACE mdk)
  endif()
  return()
endif()

add_library(LibmdkAbi SHARED IMPORTED)
set_target_properties(LibmdkAbi PROPERTIES
  IMPORTED_LOCATION "${MDK_LIBRARIES}"
  IMPORTED_IMPLIB "${MDK_LIBRARY}" # for win32, .lib import library
  INTERFACE_INCLUDE_DIRECTORIES "${MDK_ABI_INCLUDE_DIRS}"
  #IMPORTED_SONAME "@rpath/mdk.framework/mdk"
  #IMPORTED_NO_SONAME 1 # -lmdk instead of full path
  )
if(APPLE)
  set_property(TARGET LibmdkAbi PROPERTY FRAMEWORK 1)
endif()

find_package_handle_standard_args(MDK
                                  REQUIRED_VARS MDK_LIBRARY MDK_INCLUDE_DIR
                                  VERSION_VAR MDK_VERSION_STRING)
add_library(mdk SHARED IMPORTED)
set_target_properties(mdk PROPERTIES
  IMPORTED_LOCATION "${MDK_LIBRARIES}"
  IMPORTED_IMPLIB "${MDK_LIBRARY}" # for win32, .lib import library
  INTERFACE_INCLUDE_DIRECTORIES "${MDK_INCLUDE_DIRS}"
  #IMPORTED_SONAME "@rpath/mdk.framework/mdk"
  #IMPORTED_NO_SONAME 1 # -lmdk instead of full path
  )

if(APPLE)
  set_property(TARGET mdk PROPERTY FRAMEWORK 1)
else()
  if(ANDROID)
    add_library(mdk-ffmpeg SHARED IMPORTED)
    set_target_properties(mdk-ffmpeg PROPERTIES
            IMPORTED_LOCATION ${_IMPORT_PREFIX}/lib/${_IMPORT_ARCH}/libffmpeg.so
            )
    #add_dependencies(mdk mdk-ffmpeg)
    target_link_libraries(mdk INTERFACE mdk-ffmpeg)
  endif()
endif()
