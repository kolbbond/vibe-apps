# vibe-launcher: launches the games in build/games.json here or on the TV (Hyprland)
find_package(Corrade REQUIRED TestSuite)
find_package(jsoncpp CONFIG REQUIRED)

# headless, tested: command builders, hyprctl parsing, input repeat, process running
add_library(launcher_core STATIC
    core/LaunchCommands.cxx
    core/Process.cxx
)
target_include_directories(launcher_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(launcher_core PRIVATE JsonCpp::JsonCpp)
target_compile_options(launcher_core PRIVATE "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-Wshadow>")

if(ENABLE_TESTING)
    corrade_add_test(LauncherCommandsTest tests/LaunchCommandsTest.cxx LIBRARIES launcher_core)
    corrade_add_test(LauncherNavTest tests/NavRepeatTest.cxx LIBRARIES launcher_core)
    corrade_add_test(LauncherProcessTest tests/ProcessTest.cxx LIBRARIES launcher_core)
    corrade_add_test(LauncherWatchTest tests/WatchTest.cxx LIBRARIES launcher_core)
    corrade_add_test(LauncherGuardsTest tests/GuardsTest.cxx LIBRARIES launcher_core)
endif()

add_executable(vibe-launcher
    app/main.cxx
    app/LauncherApp.cxx
)
target_link_libraries(vibe-launcher PRIVATE launcher_core Vibe::Game)
target_compile_options(vibe-launcher PRIVATE "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-Wshadow>")
