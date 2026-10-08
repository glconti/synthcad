find_package(raylib 4.0 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

include(${PROJECT_SOURCE_DIR}/cmake/embed-agent-guides.cmake)
synthcad_embed_agent_guides(${CMAKE_CURRENT_BINARY_DIR}/agent_guides.generated.h)
add_library(synthcad_agent STATIC agent_cli.cpp project_contract.cpp agent_transport.cpp agent_bridge.cpp agent_entry.cpp agent_knowledge.cpp guided_pick.cpp printer_profile.cpp project_overview.cpp export_history.cpp)
target_include_directories(synthcad_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(synthcad_agent PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(synthcad_agent PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
if(WIN32)
  target_link_libraries(synthcad_agent PUBLIC advapi32 shell32)
endif()
if(MSVC)
  target_compile_options(synthcad_agent PRIVATE /utf-8)
  target_compile_definitions(synthcad_agent PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_executable(synthcad cli_main.cpp)
target_link_libraries(synthcad PRIVATE synthcad_agent)

add_library(dingcad_quickjs STATIC
  ${PROJECT_SOURCE_DIR}/vendor/quickjs/quickjs.c
  ${PROJECT_SOURCE_DIR}/vendor/quickjs/quickjs-libc.c
  ${PROJECT_SOURCE_DIR}/vendor/quickjs/cutils.c
  ${PROJECT_SOURCE_DIR}/vendor/quickjs/libunicode.c
  ${PROJECT_SOURCE_DIR}/vendor/quickjs/libregexp.c
  ${PROJECT_SOURCE_DIR}/vendor/quickjs/dtoa.c
)
target_include_directories(dingcad_quickjs PUBLIC ${PROJECT_SOURCE_DIR}/vendor/quickjs)
set_target_properties(dingcad_quickjs PROPERTIES LINKER_LANGUAGE C)
if(UNIX)
  target_link_libraries(dingcad_quickjs PUBLIC ${CMAKE_DL_LIBS} Threads::Threads m)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(dingcad_quickjs PRIVATE _GNU_SOURCE)
  endif()
endif()
if(WIN32)
  # cutils.c includes windows.h before cutils.h, which includes winsock2.h.
  # Prevent windows.h from pulling in legacy winsock.h first.
  target_compile_definitions(dingcad_quickjs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MSVC)
    # QuickJS uses <stdatomic.h>, which MSVC enables behind this opt-in.
    target_compile_options(dingcad_quickjs PRIVATE /experimental:c11atomics)
  endif()
endif()

add_library(synthcad_design_graph STATIC design_graph.cpp)
target_include_directories(synthcad_design_graph PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_design_graph PUBLIC manifold raylib dingcad_quickjs synthcad_agent)
if(MSVC)
  target_compile_options(synthcad_design_graph PRIVATE /utf-8)
endif()
add_executable(synthcad_design_graph_tests EXCLUDE_FROM_ALL tests/design_graph_test.cpp js_bindings.cpp)
target_link_libraries(synthcad_design_graph_tests PRIVATE synthcad_design_graph)
if(MSVC)
  target_compile_options(synthcad_design_graph_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_design_graph_tests PRIVATE -UNDEBUG)
endif()

add_executable(dingcad_viewer
  main.cpp
  geometry_picker.cpp
  feature_topology.cpp
  selection_reference.cpp
  selection_ui.cpp
  display_scale.cpp
  guided_pick_ui.cpp
  project_overview_ui.cpp
  plate_review_ui.cpp
  manufacturing_checks.cpp
  js_bindings.cpp
  dimensions.cpp
  appearance.cpp
  part_tree.cpp
  parts_panel.cpp
  brand.cpp
  stl_export.cpp
  three_mf_export.cpp
  export_service.cpp
  agent_scene.cpp
)

target_include_directories(dingcad_viewer
  PRIVATE
    ${PROJECT_SOURCE_DIR}/vendor/manifold/include
    ${PROJECT_SOURCE_DIR}/vendor/quickjs
)

target_link_libraries(dingcad_viewer
  PRIVATE
    manifold
    raylib
    dingcad_quickjs
    synthcad_agent
    synthcad_design_graph
)

# Must match the linked raylib build. The pinned Windows vcpkg dependency uses
# custom frame control; system/default Linux raylib normally presents itself.
if(WIN32)
  set(synthcad_custom_frame_default ON)
else()
  set(synthcad_custom_frame_default OFF)
endif()
option(SYNTHCAD_CUSTOM_FRAME_CONTROL "raylib was built with SUPPORT_CUSTOM_FRAME_CONTROL" ${synthcad_custom_frame_default})
if(SYNTHCAD_CUSTOM_FRAME_CONTROL)
  target_compile_definitions(dingcad_viewer PRIVATE SYNTHCAD_CUSTOM_FRAME_CONTROL)
endif()

foreach(test project_contract agent_cli agent_transport agent_bridge agent_knowledge guided_pick printer_profile project_overview export_history)
  add_executable(synthcad_${test}_tests EXCLUDE_FROM_ALL tests/${test}_test.cpp)
  target_link_libraries(synthcad_${test}_tests PRIVATE synthcad_agent)
  target_include_directories(synthcad_${test}_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  if(MSVC)
    target_compile_options(synthcad_${test}_tests PRIVATE /utf-8 /UNDEBUG)
  else()
    target_compile_options(synthcad_${test}_tests PRIVATE -UNDEBUG)
  endif()
endforeach()
add_executable(synthcad_feature_topology_tests EXCLUDE_FROM_ALL tests/feature_topology_test.cpp feature_topology.cpp)
target_include_directories(synthcad_feature_topology_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_feature_topology_tests PRIVATE manifold)
if(MSVC)
  target_compile_options(synthcad_feature_topology_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_feature_topology_tests PRIVATE -UNDEBUG)
endif()
add_executable(synthcad_agent_scene_tests EXCLUDE_FROM_ALL tests/agent_scene_test.cpp agent_scene.cpp part_tree.cpp)
target_link_libraries(synthcad_agent_scene_tests PRIVATE manifold raylib dingcad_quickjs synthcad_agent)
if(MSVC)
  target_compile_options(synthcad_agent_scene_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_agent_scene_tests PRIVATE -UNDEBUG)
endif()

foreach(test geometry_picker selection_reference)
  add_executable(synthcad_${test}_tests EXCLUDE_FROM_ALL tests/${test}_test.cpp ${test}.cpp feature_topology.cpp part_tree.cpp)
  target_link_libraries(synthcad_${test}_tests PRIVATE manifold raylib dingcad_quickjs synthcad_agent)
  if(MSVC)
    target_compile_options(synthcad_${test}_tests PRIVATE /utf-8 /UNDEBUG)
  else()
    target_compile_options(synthcad_${test}_tests PRIVATE -UNDEBUG)
  endif()
endforeach()
add_executable(synthcad_selection_ui_tests EXCLUDE_FROM_ALL tests/selection_ui_test.cpp selection_ui.cpp parts_panel.cpp part_tree.cpp dimensions.cpp brand.cpp)
target_include_directories(synthcad_selection_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_selection_ui_tests PRIVATE manifold raylib dingcad_quickjs)
if(MSVC)
  target_compile_options(synthcad_selection_ui_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_selection_ui_tests PRIVATE -UNDEBUG)
endif()

add_executable(synthcad_guided_pick_ui_tests EXCLUDE_FROM_ALL tests/guided_pick_ui_test.cpp guided_pick_ui.cpp parts_panel.cpp part_tree.cpp dimensions.cpp brand.cpp)
add_executable(synthcad_manufacturing_checks_tests EXCLUDE_FROM_ALL tests/manufacturing_checks_test.cpp manufacturing_checks.cpp)
target_include_directories(synthcad_manufacturing_checks_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_manufacturing_checks_tests PRIVATE manifold raylib dingcad_quickjs synthcad_agent)
add_executable(synthcad_plate_review_ui_tests EXCLUDE_FROM_ALL tests/plate_review_ui_test.cpp plate_review_ui.cpp guided_pick_ui.cpp parts_panel.cpp part_tree.cpp dimensions.cpp brand.cpp)
target_include_directories(synthcad_plate_review_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_plate_review_ui_tests PRIVATE manifold raylib dingcad_quickjs nlohmann_json::nlohmann_json)
foreach(target synthcad_manufacturing_checks_tests synthcad_plate_review_ui_tests)
  if(MSVC)
    target_compile_options(${target} PRIVATE /utf-8 /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endforeach()
add_executable(synthcad_project_overview_ui_tests EXCLUDE_FROM_ALL tests/project_overview_ui_test.cpp project_overview_ui.cpp guided_pick_ui.cpp parts_panel.cpp part_tree.cpp dimensions.cpp brand.cpp)
target_include_directories(synthcad_project_overview_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_project_overview_ui_tests PRIVATE manifold raylib dingcad_quickjs nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(synthcad_project_overview_ui_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_project_overview_ui_tests PRIVATE -UNDEBUG)
endif()
target_include_directories(synthcad_guided_pick_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_guided_pick_ui_tests PRIVATE manifold raylib dingcad_quickjs)
if(MSVC)
  target_compile_options(synthcad_guided_pick_ui_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_guided_pick_ui_tests PRIVATE -UNDEBUG)
endif()

add_executable(synthcad_display_scale_tests EXCLUDE_FROM_ALL tests/display_scale_test.cpp display_scale.cpp)
target_include_directories(synthcad_display_scale_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_display_scale_tests PRIVATE raylib)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(X11 QUIET)
  if(X11_FOUND)
    foreach(target dingcad_viewer synthcad_display_scale_tests)
      target_compile_definitions(${target} PRIVATE SYNTHCAD_X11_DPI)
      target_link_libraries(${target} PRIVATE X11::X11)
    endforeach()
  endif()
endif()

# Run directly; the optional --render argument saves overlay QA images.
add_executable(dingcad_dimension_tests EXCLUDE_FROM_ALL
  tests/dimensions_test.cpp
  dimensions.cpp
)
target_include_directories(dingcad_dimension_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dingcad_dimension_tests PRIVATE raylib dingcad_quickjs)

add_executable(dingcad_camera_tests EXCLUDE_FROM_ALL tests/camera_test.cpp)
target_include_directories(dingcad_camera_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dingcad_camera_tests PRIVATE raylib)

add_executable(dingcad_appearance_tests EXCLUDE_FROM_ALL
  tests/appearance_test.cpp appearance.cpp js_bindings.cpp)
target_include_directories(dingcad_appearance_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/vendor/manifold/include ${PROJECT_SOURCE_DIR}/vendor/quickjs)
target_link_libraries(dingcad_appearance_tests PRIVATE manifold raylib dingcad_quickjs)

add_executable(dingcad_parts_tests EXCLUDE_FROM_ALL
  tests/parts_test.cpp part_tree.cpp parts_panel.cpp brand.cpp stl_export.cpp dimensions.cpp)
target_include_directories(dingcad_parts_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/vendor/manifold/include ${PROJECT_SOURCE_DIR}/vendor/quickjs)
target_link_libraries(dingcad_parts_tests PRIVATE manifold raylib dingcad_quickjs)

add_executable(synthcad_three_mf_export_tests EXCLUDE_FROM_ALL
  tests/three_mf_export_test.cpp three_mf_export.cpp part_tree.cpp)
target_include_directories(synthcad_three_mf_export_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/vendor/manifold/include ${PROJECT_SOURCE_DIR}/vendor/quickjs)
target_link_libraries(synthcad_three_mf_export_tests PRIVATE manifold raylib dingcad_quickjs)
if(MSVC)
  target_compile_options(synthcad_three_mf_export_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_three_mf_export_tests PRIVATE -UNDEBUG)
endif()

add_executable(synthcad_export_service_tests EXCLUDE_FROM_ALL
  tests/export_service_test.cpp export_service.cpp stl_export.cpp three_mf_export.cpp part_tree.cpp)
target_include_directories(synthcad_export_service_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(synthcad_export_service_tests PRIVATE manifold raylib dingcad_quickjs synthcad_agent)
if(MSVC)
  target_compile_options(synthcad_export_service_tests PRIVATE /utf-8 /UNDEBUG)
else()
  target_compile_options(synthcad_export_service_tests PRIVATE -UNDEBUG)
endif()

if(WIN32)
  # Keep the viewer self-contained: Windows only probes the executable's
  # directory for dependent DLLs by default.
  set(dingcad_vcpkg_runtime_dir
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(APPEND dingcad_vcpkg_runtime_dir "/debug")
  endif()
  add_custom_command(TARGET dingcad_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:dingcad_viewer>
      $<TARGET_RUNTIME_DLLS:dingcad_viewer>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${dingcad_vcpkg_runtime_dir}/bin" $<TARGET_FILE_DIR:dingcad_viewer>
    COMMAND_EXPAND_LISTS
  )
endif()

if(WIN32)
  enable_language(RC)
  configure_file(resources/synthcad.rc.in ${CMAKE_CURRENT_BINARY_DIR}/synthcad.rc @ONLY)
  target_sources(dingcad_viewer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/synthcad.rc)
endif()
if(MSVC)
  foreach(target dingcad_viewer dingcad_parts_tests dingcad_appearance_tests)
    target_compile_options(${target} PRIVATE /utf-8)
  endforeach()
endif()
