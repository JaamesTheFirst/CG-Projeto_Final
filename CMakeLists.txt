cmake_minimum_required(VERSION 3.18)

project("CG_Projeto_Final" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT_SRC_DIR ${CMAKE_SOURCE_DIR}/src)
file(GLOB PROJECT_SOURCES "${PROJECT_SRC_DIR}/*.cpp")

# cgltf (header-only API + single C file implementation)
set(CGLTF_DIR ${CMAKE_SOURCE_DIR}/external/cgltf)
list(APPEND PROJECT_SOURCES
  ${CGLTF_DIR}/cgltf.c
)
# cgltf is C; build it as C++ since the project only enables CXX language.
set_source_files_properties(${CGLTF_DIR}/cgltf.c PROPERTIES LANGUAGE CXX)

add_executable(super_mario_proto
  ${PROJECT_SOURCES}
)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(glm REQUIRED)
find_package(PNG REQUIRED)
find_package(assimp REQUIRED)

target_include_directories(super_mario_proto PRIVATE
  ${PROJECT_SRC_DIR}
  ${CGLTF_DIR}
  ${CMAKE_SOURCE_DIR}/external/stb
)

target_link_libraries(super_mario_proto PRIVATE
  OpenGL::GL
  GLEW::GLEW
  glfw
  glm::glm
  PNG::PNG
  assimp::assimp
)

if (MSVC)
  target_compile_options(super_mario_proto PRIVATE /W4 /permissive-)
else()
  target_compile_options(super_mario_proto PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_definitions(super_mario_proto PRIVATE
  PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Copy runtime resources (assets folder) next to the executable for out-of-tree builds.
set(RESOURCE_OUTPUT_DIR "$<TARGET_FILE_DIR:super_mario_proto>")
add_custom_command(TARGET super_mario_proto POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "${RESOURCE_OUTPUT_DIR}/assets"
  COMMENT "Copying assets directory"
)

