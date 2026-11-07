## Project setup
cmake_minimum_required(VERSION 3.24)  # CMAKE_FIND_PACKAGE_TARGETS_GLOBAL

# Default build type: RelWithDebInfo (optimized + debug symbols)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(vibe-apps VERSION 0.1.0 LANGUAGES C CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Make a build directory and run CMake from there.")
endif()

if(PROJECT_IS_TOP_LEVEL)
    # Keep asserts enabled even in Release/RelWithDebInfo builds (as the engine does)
    string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "-DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    string(REPLACE "/DNDEBUG" "" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE PATH "" FORCE)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    if(WIN32)
        set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()

    option(ENABLE_TESTING "build tests" ON)
    if(ENABLE_TESTING)
        enable_testing()
    endif()

    # the engine: a vibe checkout next to this repo (side by side), else an installed Vibe package
    set(VIBE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../vibe" CACHE PATH "vibe engine checkout (empty or missing: use an installed Vibe)")
    if(EXISTS "${VIBE_ROOT}/cmake/VibeDeps.cmake")
        message(STATUS "vibe engine: ${VIBE_ROOT}")
        include(${VIBE_ROOT}/cmake/VibeDeps.cmake)
        add_subdirectory(${VIBE_ROOT} vibe)
    else()
        # installed engine; third-party packages from the engine's deps/ superbuild
        set(VIBE_DEPS_PREFIX "" CACHE PATH "the vibe engine's .deps/usr")
        if(VIBE_DEPS_PREFIX)
            list(APPEND CMAKE_PREFIX_PATH "${VIBE_DEPS_PREFIX}")
            list(APPEND CMAKE_MODULE_PATH
                "${VIBE_DEPS_PREFIX}/share/cmake/Corrade"
                "${VIBE_DEPS_PREFIX}/share/cmake/Magnum"
                "${VIBE_DEPS_PREFIX}/share/cmake/MagnumIntegration")
            set(CMAKE_BUILD_RPATH "${VIBE_DEPS_PREFIX}/lib;${VIBE_DEPS_PREFIX}/lib/smg")
        endif()
        set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
        set(OpenGL_GL_PREFERENCE "GLVND")
        find_package(Vibe CONFIG REQUIRED)
        find_package(Boost 1.80.0 REQUIRED COMPONENTS filesystem)
        message(STATUS "vibe engine: installed package ${Vibe_DIR}")
    endif()

    # game.json manifests -> build/games.json (the launcher's list)
    include(VibeManifest)
else()
    # inside a parent project that already provides the engine and writes the manifests
    set(ENABLE_TESTING OFF)
endif()

add_subdirectory(vvavviz)
vibe_add_manifest(vvavviz ${CMAKE_CURRENT_SOURCE_DIR}/vvavviz/game.json)

# desk + couch launcher over build/games.json (Hyprland, POSIX)
if(UNIX)
    add_subdirectory(launcher)
endif()

if(NOT PROJECT_IS_TOP_LEVEL)
    return()
endif()

vibe_write_manifests()

# Windows: copy engine, deps and vcpkg DLLs next to vvavviz.exe so it runs from the build tree
if(WIN32 AND TARGET vibecommon)
    if(DEFINED ENV{VCPKG_ROOT})
        file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" _VCPKG_ROOT_PATH)
    elseif(DEFINED ENV{VCPKG_INSTALLATION_ROOT})
        file(TO_CMAKE_PATH "$ENV{VCPKG_INSTALLATION_ROOT}" _VCPKG_ROOT_PATH)
    endif()
    add_custom_command(TARGET vvavviz POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:vibeaudio>"
            "$<TARGET_FILE:vibecommon>"
            "$<TARGET_FILE:vibegraphics>"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${DEPS_PREFIX}/lib/soundio.dll"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${DEPS_PREFIX}/bin"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_VCPKG_ROOT_PATH}/installed/x64-windows/bin"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
        COMMENT "Copying DLLs to output directory..."
    )
endif()

# license + third-party notices travel with every install and package
install(FILES LICENSE THIRD-PARTY-NOTICES.md DESTINATION share/doc/vvavviz)
install(DIRECTORY licenses/ DESTINATION share/doc/vvavviz/licenses)

# CPack: vvavviz installer/zip (Windows) or tarball (Linux)
set(CPACK_PACKAGE_NAME "vvavviz")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Vibe")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "vvavviz - music visualizer and fluid plasma")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")

    # NSIS specific settings
    set(CPACK_NSIS_DISPLAY_NAME "vvavviz ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "vvavviz")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "vvavviz")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "vvavviz")

    # Start menu shortcut
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\vvavviz.lnk' '$INSTDIR\\\\bin\\\\vvavviz.exe'")
    set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\vvavviz.lnk'")

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vvavviz/assets/vibe.ico")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}/vvavviz/assets/vibe.ico")
        set(CPACK_NSIS_MUI_UNIICON "${CMAKE_CURRENT_SOURCE_DIR}/vvavviz/assets/vibe.ico")
    endif()
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
