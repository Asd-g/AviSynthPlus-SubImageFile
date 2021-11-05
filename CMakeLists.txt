cmake_minimum_required(VERSION 3.16)

project(libsubimagefile LANGUAGES CXX)

find_package (Git)

if (GIT_FOUND)
    execute_process (COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
        OUTPUT_VARIABLE ver
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else ()
    message (STATUS "GIT not found")
endif ()

add_library(subimagefile SHARED src/SubImageFile.cpp)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

message(STATUS "Build type - ${CMAKE_BUILD_TYPE}")

target_include_directories(subimagefile PRIVATE /usr/local/include/avisynth)

set_target_properties(subimagefile PROPERTIES OUTPUT_NAME "subimagefile.${ver}")

target_compile_features(subimagefile PRIVATE cxx_std_14)

find_library(libavcodec NAMES avcodec)
find_library(libavformat NAMES avformat)
find_library(libavutil NAMES avutil)
find_library(libswresample NAMES swresample)
find_library(zlib NAMES z)
message(STATUS "find_library returned ${libavcodec}")
message(STATUS "find_library returned ${libavformat}")
message(STATUS "find_library returned ${libavutil}")
message(STATUS "find_library returned ${libswresample}")
message(STATUS "find_library returned ${zlib}")
target_link_libraries(subimagefile PRIVATE
    ${libavcodec}
    ${libavformat}
    ${libavutil}
    ${libswresample}
    ${zlib}
)

include(GNUInstallDirs)

INSTALL(TARGETS subimagefile LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/avisynth")
        
# uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()
