cmake_minimum_required(VERSION 3.25)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
project(FTFSurface VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

add_library(miniz STATIC third_party/miniz/miniz.c third_party/miniz/miniz_tdef.c third_party/miniz/miniz_tinfl.c third_party/miniz/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party/miniz)
target_compile_definitions(miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

add_library(volk STATIC third_party/volk/volk.c)
target_include_directories(volk PUBLIC third_party/volk third_party/vulkan-headers/include)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui PUBLIC volk glfw)

find_program(GLSLANG_EXECUTABLE NAMES glslang.exe glslangValidator.exe
    PATHS "${CMAKE_SOURCE_DIR}/.tools/glslang/bin" REQUIRED)
set(SHADER_OUTPUTS)
foreach(SHADER terrain.comp terrain.vert terrain.frag filter.comp mountain_cache.comp mountain_segments.comp world_map.comp ocean_floor.comp island_surface.comp climate.comp world_filter.comp rivers.comp biomes.comp atmosphere.vert atmosphere.frag)
    set(OUTPUT "${CMAKE_BINARY_DIR}/shaders/${SHADER}.spv")
    add_custom_command(OUTPUT "${OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND "${GLSLANG_EXECUTABLE}" -V --target-env vulkan1.1 -I"${CMAKE_SOURCE_DIR}/shaders"
            "${CMAKE_SOURCE_DIR}/shaders/${SHADER}" -o "${OUTPUT}"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/${SHADER}" "${CMAKE_SOURCE_DIR}/shaders/shared.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/ftf_terrain.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_regions.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_cells.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_mix.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/ftf_mountains.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_sin.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/ftf_land.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/ftf_world.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_water_steps.glsl"
            "${CMAKE_SOURCE_DIR}/shaders/ftf_islands.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_climate.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_region_cells.glsl" "${CMAKE_SOURCE_DIR}/shaders/cell_state.glsl" "${CMAKE_SOURCE_DIR}/shaders/materials.glsl" "${CMAKE_SOURCE_DIR}/shaders/material_rules_generated.glsl" "${CMAKE_SOURCE_DIR}/shaders/mod_material_rules_generated.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_filters.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_erosion.glsl" "${CMAKE_SOURCE_DIR}/shaders/ftf_rivers.glsl"
        VERBATIM)
    list(APPEND SHADER_OUTPUTS "${OUTPUT}")
endforeach()
add_custom_target(shaders ALL DEPENDS ${SHADER_OUTPUTS})

add_executable(FTFSurface src/main.cpp src/gpu.cpp src/surface.cpp src/surface_generation.cpp src/preset.cpp src/datapack.cpp src/ftf_packgen.cpp src/library.cpp src/preview.cpp src/mix.cpp src/world.cpp src/cpu_world.cpp src/rivers.cpp)
target_link_libraries(FTFSurface PRIVATE imgui comdlg32 miniz shell32 ole32 dbghelp)
if(MSVC)
    target_link_options(FTFSurface PRIVATE "/MAP:${CMAKE_BINARY_DIR}/FTFSurface.map")
    # GraphSeeds carries the per-seed vanilla noise and strata tables (47 KB); the frame loop keeps several copies on the stack.
    target_link_options(FTFSurface PRIVATE "/STACK:16777216")
endif()
target_include_directories(FTFSurface PRIVATE third_party/glm third_party/json/single_include)
target_compile_definitions(FTFSurface PRIVATE
    GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(FTFSurface PRIVATE /W4 /EHsc /utf-8 /permissive-)
endif()
add_dependencies(FTFSurface shaders)
add_custom_target(content ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:FTFSurface>/assets"
    DEPENDS FTFSurface)

