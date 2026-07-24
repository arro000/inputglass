cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" INPUTGLASS_VERSION LIMIT_COUNT 1)
if(NOT INPUTGLASS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must contain one semantic version such as 1.0.0")
endif()

project(InputGlass VERSION "${INPUTGLASS_VERSION}" LANGUAGES C RC)

if(NOT WIN32)
    message(FATAL_ERROR "InputGlass targets Windows. Use the supplied MinGW-w64 toolchain file when cross-compiling.")
endif()

set(INPUTGLASS_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(INPUTGLASS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/inputglass.ico")
set(INPUTGLASS_MANIFEST "${INPUTGLASS_GENERATED_DIR}/app.manifest")
set(INPUTGLASS_RESOURCE "${INPUTGLASS_GENERATED_DIR}/inputglass.rc")
file(MAKE_DIRECTORY "${INPUTGLASS_GENERATED_DIR}")
configure_file(VERSION "${INPUTGLASS_GENERATED_DIR}/VERSION" COPYONLY)
configure_file(resources/version.h.in "${INPUTGLASS_GENERATED_DIR}/version.h" @ONLY)
configure_file(resources/app.manifest.in "${INPUTGLASS_MANIFEST}" @ONLY)
configure_file(resources/inputglass.rc.in "${INPUTGLASS_RESOURCE}" @ONLY)

add_executable(InputGlass WIN32
    src/inputglass.c
    "${INPUTGLASS_RESOURCE}"
)

set_property(SOURCE "${INPUTGLASS_RESOURCE}" PROPERTY
    OBJECT_DEPENDS "${INPUTGLASS_ICON};${INPUTGLASS_MANIFEST}"
)

target_include_directories(InputGlass PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    "${INPUTGLASS_GENERATED_DIR}"
)
target_compile_definitions(InputGlass PRIVATE UNICODE _UNICODE)
target_link_libraries(InputGlass PRIVATE user32 gdi32 shell32)

if(MSVC)
    target_compile_options(InputGlass PRIVATE /W4 /WX /utf-8)
    target_link_options(InputGlass PRIVATE /MANIFEST:NO)
else()
    target_compile_options(InputGlass PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(MINGW)
        target_link_options(InputGlass PRIVATE -municode -static)
    endif()
endif()

set_target_properties(InputGlass PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "InputGlass"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS InputGlass RUNTIME DESTINATION .)
install(FILES LICENSE README.md VERSION DESTINATION .)
