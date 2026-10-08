find_package(raylib 4.0 REQUIRED)

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
if(WIN32)
  # cutils.c includes windows.h before cutils.h, which includes winsock2.h.
  # Prevent windows.h from pulling in legacy winsock.h first.
  target_compile_definitions(dingcad_quickjs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  if(MSVC)
    # QuickJS uses <stdatomic.h>, which MSVC enables behind this opt-in.
    target_compile_options(dingcad_quickjs PRIVATE /experimental:c11atomics)
  endif()
endif()

add_executable(dingcad_viewer
  main.cpp
  js_bindings.cpp
  dimensions.cpp
  appearance.cpp
  part_tree.cpp
  parts_panel.cpp
  brand.cpp
  stl_export.cpp
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
)

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
