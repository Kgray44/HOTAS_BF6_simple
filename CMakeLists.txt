cmake_minimum_required(VERSION 3.21)

# VERSION is the single release version authority.  Packaging and CI read the
# same file rather than maintaining independent application/installer values.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" HOTAS_BF6_VERSION_RAW)
string(STRIP "${HOTAS_BF6_VERSION_RAW}" HOTAS_BF6_VERSION)
if (NOT HOTAS_BF6_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be a semantic major.minor.patch value")
endif()
string(REPLACE "." "," HOTAS_BF6_VERSION_COMMAS "${HOTAS_BF6_VERSION}")

project(HOTASMapper VERSION ${HOTAS_BF6_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)

qt_standard_project_setup(REQUIRES 6.5)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(cmake/build_version.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hotas_build_version.h" @ONLY)
configure_file(cmake/hotas_version.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hotas_version.rc" @ONLY)
configure_file(cmake/launcher_version.rc.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/launcher_version.rc" @ONLY)

set(HOTAS_MAPPER_SOURCES
    src/main.cpp
    src/mapping_types.h
    src/axis_transform.h
    src/axis_transform.cpp
    src/response_curve.h
    src/response_curve.cpp
    src/button_mapping.h
    src/button_mapping.cpp
    src/physical_input_monitor.h
    src/physical_input_monitor.cpp
    src/profile_model.h
    src/profile_model.cpp
    src/profile_trigger_runtime.h
    src/profile_trigger_runtime.cpp
    src/config_store.h
    src/config_store.cpp
    src/mapping_worker.h
    src/mapping_worker.cpp
    src/app_backend.h
    src/app_backend.cpp
)

qt_add_executable(HOTASMapper
    ${HOTAS_MAPPER_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hotas_version.rc"
)

qt_add_qml_module(HOTASMapper
    URI HOTASMapper
    VERSION 1.0
    QML_FILES
        qml/AviationPanel.qml
        qml/Main.qml
        qml/CurveEditor.qml
)

target_link_libraries(HOTASMapper PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
target_include_directories(HOTASMapper PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
set_target_properties(HOTASMapper PROPERTIES
    OUTPUT_NAME "HOTAS BF6"
    WIN32_EXECUTABLE TRUE
    VERSION "${HOTAS_BF6_VERSION}"
)

if (WIN32)
    target_link_libraries(HOTASMapper PRIVATE dinput8 dxguid advapi32)
    target_compile_definitions(HOTASMapper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    message(FATAL_ERROR "HOTAS Mapper v1.5 targets Windows (DirectInput and vJoy).")
endif()

# The launcher is deliberately independent of Qt and the mapper's real-time
# worker.  It runs only before the mapper process starts.
add_library(hotas_launcher_core STATIC
    launcher/launcher_core.cpp
    launcher/launcher_core.h
)
target_compile_features(hotas_launcher_core PUBLIC cxx_std_20)
target_include_directories(hotas_launcher_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/launcher")
target_link_libraries(hotas_launcher_core PUBLIC bcrypt)
target_compile_definitions(hotas_launcher_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_executable(HOTASBF6Launcher WIN32
    launcher/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/launcher_version.rc"
)
target_compile_features(HOTASBF6Launcher PRIVATE cxx_std_20)
target_include_directories(HOTASBF6Launcher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(HOTASBF6Launcher PRIVATE hotas_launcher_core winhttp shell32 comctl32)
target_compile_definitions(HOTASBF6Launcher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if (MSVC)
    # The tiny native launcher has no Qt dependency and can use a static CRT.
    set_property(TARGET hotas_launcher_core PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET HOTASBF6Launcher PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set_target_properties(HOTASBF6Launcher PROPERTIES
    OUTPUT_NAME "HOTAS BF6 Launcher"
    VERSION "${HOTAS_BF6_VERSION}"
)

include(CTest)
if (BUILD_TESTING)
    qt_add_executable(mapping_core_tests
        tests/mapping_core_tests.cpp
        src/mapping_types.h
        src/axis_transform.h
        src/axis_transform.cpp
        src/response_curve.h
        src/response_curve.cpp
        src/button_mapping.h
        src/button_mapping.cpp
        src/physical_input_monitor.h
        src/physical_input_monitor.cpp
        src/profile_model.h
        src/profile_model.cpp
        src/profile_trigger_runtime.h
        src/profile_trigger_runtime.cpp
        src/config_store.h
        src/config_store.cpp
    )
    target_link_libraries(mapping_core_tests PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(mapping_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME mapping_core_tests COMMAND mapping_core_tests)

if (WIN32)
    set_tests_properties(mapping_core_tests PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
endif()

    add_executable(launcher_core_tests tests/launcher_core_tests.cpp)
    target_link_libraries(launcher_core_tests PRIVATE hotas_launcher_core)
    target_compile_features(launcher_core_tests PRIVATE cxx_std_20)
    if (MSVC)
        set_property(TARGET launcher_core_tests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME launcher_core_tests COMMAND launcher_core_tests)

option(HOTAS_BUILD_PERFORMANCE_BENCHMARK "Build the synthetic mapping hot-path benchmark" OFF)
if (HOTAS_BUILD_PERFORMANCE_BENCHMARK)
    qt_add_executable(mapping_hot_path_benchmark
        tests/mapping_hot_path_benchmark.cpp
        src/mapping_types.h
        src/axis_transform.h
        src/axis_transform.cpp
        src/response_curve.h
        src/response_curve.cpp
        src/button_mapping.h
        src/button_mapping.cpp
        src/physical_input_monitor.h
        src/physical_input_monitor.cpp
        src/profile_model.h
        src/profile_model.cpp
        src/profile_trigger_runtime.h
        src/profile_trigger_runtime.cpp
    )
    target_link_libraries(mapping_hot_path_benchmark PRIVATE Qt6::Core)
    target_include_directories(mapping_hot_path_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME mapping_hot_path_benchmark COMMAND mapping_hot_path_benchmark)
endif()
endif()

option(HOTAS_BUILD_CURVE_RESEARCH_REPORT
    "Build the deterministic advanced-curve research metrics generator" OFF)
if (HOTAS_BUILD_CURVE_RESEARCH_REPORT)
    qt_add_executable(curve_research_report
        tests/curve_research_report.cpp
        src/mapping_types.h
        src/response_curve.h
        src/response_curve.cpp
    )
    target_link_libraries(curve_research_report PRIVATE Qt6::Core)
    target_include_directories(curve_research_report PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

install(TARGETS HOTASMapper HOTASBF6Launcher
    BUNDLE DESTINATION .
    RUNTIME DESTINATION .
)
