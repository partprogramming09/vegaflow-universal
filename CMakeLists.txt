cmake_minimum_required(VERSION 3.20)
project(vegaflow VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra)
    add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Core static library
set(VEGAFLOW_CORE_SOURCES
    src/core/privileges.cpp
    src/core/hardware_profiler.cpp
    src/core/process_detector.cpp
    src/core/system_info.cpp
    src/optimizers/graphics_optimizer.cpp
    src/optimizers/dxvk_manager.cpp
    src/optimizers/steam_engine_optimizer.cpp
    src/optimizers/fsr_scaler.cpp
    src/optimizers/cpu_orchestrator.cpp
    src/optimizers/memory_manager.cpp
    src/security/sandbox_guard.cpp
    src/security/frame_telemetry.cpp
    src/gui/gui_components.cpp
    src/gui/gui_tabs.cpp
    src/gui/gui_window.cpp
)

find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)

if(CMAKE_CROSSCOMPILING)
    set(RUST_TARGET "x86_64-pc-windows-gnu")
else()
    set(RUST_TARGET "x86_64-pc-windows-gnu")
endif()

set(RUST_STATIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/rust/vegaflow-core/target/${RUST_TARGET}/release/libvegaflow_core.a")

add_custom_target(vegaflow_rust_core ALL
    COMMAND ${CARGO_EXECUTABLE} build --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/rust/vegaflow-core/Cargo.toml" --target ${RUST_TARGET} --release
    BYPRODUCTS "${RUST_STATIC_LIB}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building Rust static library vegaflow-core (${RUST_TARGET})"
)

add_library(vegaflow_core STATIC ${VEGAFLOW_CORE_SOURCES})
add_dependencies(vegaflow_core vegaflow_rust_core)

target_include_directories(vegaflow_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/core
    ${CMAKE_CURRENT_SOURCE_DIR}/include/optimizers
    ${CMAKE_CURRENT_SOURCE_DIR}/include/security
    ${CMAKE_CURRENT_SOURCE_DIR}/include/gui
)

target_link_libraries(vegaflow_core PUBLIC
    "${RUST_STATIC_LIB}"
    ntdll
    dxgi
    advapi32
    psapi
    user32
    ole32
    gdi32
    shell32
    dwmapi
    tdh
)

# Daemon executable (vegaflow_daemon)
add_executable(vegaflow_daemon src/main.cpp)
target_link_libraries(vegaflow_daemon PRIVATE vegaflow_core)

# Test suite executable (vegaflow_tests)
add_executable(vegaflow_tests tests/test_main.cpp)
target_link_libraries(vegaflow_tests PRIVATE vegaflow_core)
