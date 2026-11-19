cmake_minimum_required(VERSION 3.27)
project(RecoilAndroidGraphicsProbe LANGUAGES C)

find_package(SDL2 CONFIG REQUIRED)
find_library(GLES_LIBRARY GLESv3 REQUIRED)
find_library(LOG_LIBRARY log REQUIRED)

add_library(main SHARED main.c)
target_link_libraries(main PRIVATE SDL2::SDL2 ${GLES_LIBRARY} ${LOG_LIBRARY})
# Use the Recoil icon vertex shader with a small model-preview fragment shader.
set(shader_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../cont/base/springcontent/shaders/GLSL")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
	"${shader_dir}/Icons3DVS.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/unit.frag.glsl")
file(READ "${shader_dir}/Icons3DVS.glsl" vertex_shader)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/unit.frag.glsl" fragment_shader)
string(REPLACE "#version 150 compatibility" "#version 310 es\nuniform mat4 viewProjMat;" vertex_shader "${vertex_shader}")
string(REPLACE "gl_ModelViewProjectionMatrix" "viewProjMat" vertex_shader "${vertex_shader}")

foreach(stage vertex fragment)
	string(REPLACE "\\" "\\\\" ${stage}_shader "${${stage}_shader}")
	string(REPLACE "\"" "\\\"" ${stage}_shader "${${stage}_shader}")
	string(REPLACE "\n" "\\n\"\n\"" ${stage}_shader "${${stage}_shader}")
endforeach()
file(CONFIGURE OUTPUT recoil_icon_shaders.h CONTENT
	"static const char vertexSource[] = \"@vertex_shader@\";\nstatic const char fragmentSource[] = \"@fragment_shader@\";\n" @ONLY)
target_include_directories(main PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(main PRIVATE m)
