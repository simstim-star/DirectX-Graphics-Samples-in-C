# run with:
# `cmake -B build-msvc -S . -G "Visual Studio 17 2022"`
# `cmake --build build-msvc`

cmake_minimum_required(VERSION 3.23.2)
project(D3D12Bundles LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(SOURCE_FILES main.c sample.c sample_commons.c window.c simple_camera.c occcity.c frame_resource.c)
set(HEADER_FILES sample.h sample_commons.h window.h simple_camera.h step_timer.h occcity.h frame_resource.h)
set(SHADER_FILES shaders/shader_mesh_alt_pixel.hlsl shaders/shader_mesh_simple_pixel.hlsl shaders/shader_mesh_simple_vert.hlsl)
set(ALL_PROJECT_FILES ${SOURCE_FILES} ${HEADER_FILES} ${SHADER_FILES})
set_source_files_properties(${SHADER_FILES} PROPERTIES LANGUAGE HLSL)

source_group("Sources" FILES ${SOURCE_FILES})
source_group("Headers" FILES ${HEADER_FILES})
source_group("Shaders" FILES ${SHADER_FILES})

# You need to add all files on the executable for them to be visible on 
# the source_group in Visual Studio
# https://stackoverflow.com/a/31653555/14815076
add_executable(${PROJECT_NAME} WIN32 ${ALL_PROJECT_FILES})

target_compile_options(${PROJECT_NAME} PRIVATE /WX)

# not good to hardcode the path as below... it should be automatic once installed
find_package(XMathC REQUIRED PATHS "C:/xmathc")

target_link_libraries(${PROJECT_NAME} PUBLIC d3d12.lib dxguid.lib dxgi.lib D3DCompiler.lib XMathC) 

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/occcity.bin $<TARGET_FILE_DIR:${PROJECT_NAME}>/occcity.bin
    COMMENT "Copying occcity.bin" VERBATIM
)

# Build HLSL shaders
add_custom_target(shaders)

add_custom_command(
    TARGET shaders PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copying shaders" VERBATIM
)

set(HLSL_SHADER_FILES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_mesh_alt_pixel.hlsl 
 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_mesh_simple_pixel.hlsl 
 ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_mesh_simple_vert.hlsl)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_mesh_simple_vert.hlsl PROPERTIES ShaderType "vs")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_mesh_alt_pixel.hlsl PROPERTIES ShaderType "ps")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_mesh_simple_pixel.hlsl PROPERTIES ShaderType "ps")
set_source_files_properties(${HLSL_SHADER_FILES} PROPERTIES ShaderModel 4_0)

foreach(FILE ${HLSL_SHADER_FILES})
  get_filename_component(FILE_WE ${FILE} NAME_WE)
  get_source_file_property(shadertype ${FILE} ShaderType)
  get_source_file_property(shadermodel ${FILE} ShaderModel)
  add_custom_command(TARGET shaders
                     COMMAND fxc.exe /nologo /EMain /T${shadertype}_${shadermodel} $<IF:$<CONFIG:DEBUG>,/Od,/O1> /Zi /Fo $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/${FILE_WE}.cso /Fd $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/${FILE_WE}.pdb ${FILE}
                     MAIN_DEPENDENCY ${FILE}
                     COMMENT "HLSL ${FILE}"
                     WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                     VERBATIM)
endforeach(FILE)

add_dependencies(${PROJECT_NAME} shaders)