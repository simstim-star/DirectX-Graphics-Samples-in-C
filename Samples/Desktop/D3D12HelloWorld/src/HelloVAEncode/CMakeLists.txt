# run with:
# `cmake -B build-msvc -S . -G "Visual Studio 17 2022"`
# `cmake --build build-msvc`

cmake_minimum_required(VERSION 3.23.2)
project(HelloVAEncode LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(SOURCE_FILES main.c sample.c sample_commons.c window.c)
set(HEADER_FILES sample.h sample_commons.h window.h)
set(ALL_PROJECT_FILES ${SOURCE_FILES} ${HEADER_FILES})

source_group("Sources" FILES ${SOURCE_FILES})
source_group("Headers" FILES ${HEADER_FILES})

# You need to add all files on the executable for them to be visible on 
# the source_group in Visual Studio
# https://stackoverflow.com/a/31653555/14815076
add_executable(${PROJECT_NAME} WIN32 ${ALL_PROJECT_FILES})

add_custom_command(
    TARGET ${PROJECT_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/packages.config ${CMAKE_BINARY_DIR}
    COMMENT "Copying NuGet configs" VERBATIM
)

target_compile_options(${PROJECT_NAME} PRIVATE /WX)
target_link_libraries(${PROJECT_NAME} d3d12.lib dxguid.lib dxgi.lib va.lib va_win32.lib) 