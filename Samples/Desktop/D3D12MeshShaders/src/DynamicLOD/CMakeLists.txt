# run with:
# `cmake -B build-msvc -S . -G "Visual Studio 17 2022"`
# `cmake --build build-msvc`

cmake_minimum_required(VERSION 3.23.2)
project(DynamicLOD LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(SOURCE_FILES main.c sample.c sample_commons.c window.c simple_camera.c model.c)
set(HEADER_FILES sample.h sample_commons.h shared.h window.h span.h macros.h simple_camera.h step_timer.h model.h 
dxheaders/core_helpers.h dxheaders/d3dx12_pipeline_state_stream.h dxheaders/barrier_helpers.h)
set(SHADER_FILES shaders/MeshletAS.hlsl shaders/MeshletPS.hlsl shaders/MeshletMS.hlsl)
set(ALL_PROJECT_FILES ${SOURCE_FILES} ${HEADER_FILES} ${SHADER_FILES})
set_source_files_properties(${SHADER_FILES} PROPERTIES LANGUAGE HLSL)

source_group("Sources" FILES ${SOURCE_FILES})
source_group("Headers" FILES ${HEADER_FILES})
source_group("Shaders" FILES ${SHADER_FILES})

# You need to add all files on the executable for them to be visible on 
# the source_group in Visual Studio
# https://stackoverflow.com/a/31653555/14815076
add_executable(${PROJECT_NAME} WIN32 ${ALL_PROJECT_FILES})

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copying shaders" VERBATIM
)

add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
    COMMENT "Copying Assets" VERBATIM
)

target_compile_options(${PROJECT_NAME} PRIVATE /WX)

# not good to hardcode the path as below... it should be automatic once installed
find_package(XMathC REQUIRED PATHS "C:/xmathc")

target_link_libraries(${PROJECT_NAME} PUBLIC d3d12.lib dxguid.lib dxgi.lib D3DCompiler.lib XMathC) 

# Build HLSL shaders
add_custom_target(shaders)

add_custom_command(
    TARGET shaders PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copying shaders" VERBATIM
)

set(HLSL_SHADER_FILES
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/MeshletAS.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/MeshletMS.hlsl
  ${CMAKE_CURRENT_SOURCE_DIR}/shaders/MeshletPS.hlsl
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/shaders/MeshletAS.hlsl PROPERTIES ShaderType "as" ShaderModel "6_5")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/shaders/MeshletMS.hlsl PROPERTIES ShaderType "ms" ShaderModel "6_5")
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/shaders/MeshletPS.hlsl PROPERTIES ShaderType "ps" ShaderModel "6_3")
set_source_files_properties(${HLSL_SHADER_FILES})

foreach(FILE ${HLSL_SHADER_FILES})
  get_filename_component(FILE_WE ${FILE} NAME_WE)
  get_source_file_property(shadertype ${FILE} ShaderType)
  get_source_file_property(shadermodel ${FILE} ShaderModel)

  if(NOT shadertype)
    message(FATAL_ERROR "ShaderType not set for ${FILE}")
  endif()

  if(NOT shadermodel)
    message(FATAL_ERROR "ShaderModel not set for ${FILE}")
  endif()

  # DXC required! Add it to your path
  add_custom_command(TARGET shaders PRE_BUILD
    COMMAND dxc
      -T${shadertype}_${shadermodel}
      $<IF:$<CONFIG:DEBUG>,-Od,-O3>
      $<IF:$<CONFIG:DEBUG>,-Zi,>                    
      $<IF:$<CONFIG:DEBUG>,-Qembed_debug,>
      -D__HLSL__
      -I C:/xmathc          
      -Fo $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders/${FILE_WE}.cso
      ${FILE}
    MAIN_DEPENDENCY ${FILE}
    COMMENT "Compiling ${FILE}"
    VERBATIM
  )
endforeach()

add_dependencies(${PROJECT_NAME} shaders)
