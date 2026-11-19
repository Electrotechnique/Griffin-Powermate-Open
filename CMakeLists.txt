# For Visual Studio / MSVC:  cmake -B build && cmake --build build --config Release
cmake_minimum_required(VERSION 3.16)
project(PowerMate C RC)

add_executable(PowerMate WIN32
    src/main.c src/device.c src/actions.c src/config.c src/settings.c src/app.rc)
target_compile_definitions(PowerMate PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 _CRT_SECURE_NO_WARNINGS)
target_link_libraries(PowerMate PRIVATE hid setupapi ole32 uuid shell32 user32 advapi32 comctl32 comdlg32)
if(MSVC)
    target_link_options(PowerMate PRIVATE /ENTRY:wWinMainCRTStartup /MANIFEST:NO)
    set_property(TARGET PowerMate PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
else()
    target_link_options(PowerMate PRIVATE -municode -static)
endif()
