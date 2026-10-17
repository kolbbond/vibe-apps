## Project setup
cmake_minimum_required(VERSION 3.10)
project(main VERSION 0.1.0 LANGUAGES C CXX)

message(STATUS "starting cmake generation: " ${PROJECT_NAME})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# export for lsp
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# policies boost deprecation
#cmake_policy(SET CMP0167 NEW)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the build type from Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()
# check system
message(STATUS "check system architecture")
if(WIN32)
    message(STATUS "this is a windows machine")
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

endif(WIN32)
if(UNIX)
    message(STATUS "this is a linux machine")
endif(UNIX)

# ensure out of core build
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed.\nPlease make a new directory (called a build directory) and run CMake from there.\n")
endif()

message(STATUS "find packages")

# directory to search for modules
list(INSERT CMAKE_MODULE_PATH 0 ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# find libraries
#find_package(VibeAudio REQUIRED)
#find_package(VibeGraphics REQUIRED)
# tclap command line input parser
find_package(TCLAP REQUIRED)
find_package(OpenGL REQUIRED)

##################################################
# Create libraries

# add source list
# ls *.cxx >> CMakeLists.txt
set(source_list
    vvavviz.cxx
    AudioPlayer.cxx
    AudioPipeline.cxx
    FileBrowser.cxx
    GuiViews.cxx
    PlasmaView.cxx
)

SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH FALSE)
SET(CMAKE_INSTALL_RPATH "$ORIGIN/../lib/vibe")
add_executable(vvavviz ${source_list})

# add icon
set(icon_conf "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.rc")
set(icon_file "${CMAKE_CURRENT_SOURCE_DIR}/assets/vibe.ico")

if(WIN32 AND EXISTS ${icon_conf} AND EXISTS ${icon_file})
    #  set_property(TARGET vvavviz PROPERTY WIN32_EXECUTABLE TRUE)
    target_sources(vvavviz PRIVATE ${icon_conf})
    set_property(TARGET vvavviz PROPERTY WIN32_EXECUTABLE_ICON ${icon_file})
endif()

# enable warnings
message(STATUS "compiler id: " ${CXX_COMPILER_ID})
target_compile_options(vvavviz PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>:
        -Wpedantic -Wall -Wextra>
    )

# link to library
#link_directories(${LIB_DIR})
target_link_libraries(vvavviz PUBLIC
    Vibe::Audio
    Vibe::Graphics
    Vibe::Fluid
    Vibe::FluidPresets
    Vibe::GameCore
    Boost::filesystem
 )
target_compile_definitions(vvavviz PRIVATE VVAVVIZ_PRESETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/presets")

# On Windows, SDL2 redefines main to SDL_main, requiring SDL2main for the entry point
if(WIN32)
    find_package(SDL2 CONFIG REQUIRED)
    target_link_libraries(vvavviz PRIVATE SDL2::SDL2main)
endif()


 ## install
 install (TARGETS vvavviz RUNTIME DESTINATION bin)

