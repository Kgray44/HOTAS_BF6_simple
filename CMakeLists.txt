cmake_minimum_required(VERSION 3.21)

# HOTAS_VERSION is the single release version authority.  Its non-generic name
# avoids shadowing the C++20 <version> header on case-insensitive Windows paths.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/HOTAS_VERSION" HOTAS_BF6_VERSION_RAW)
string(STRIP "${HOTAS_BF6_VERSION_RAW}" HOTAS_BF6_VERSION)
if (NOT HOTAS_BF6_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "HOTAS_VERSION must be a semantic major.minor.patch value")
endif()
string(REPLACE "." "," HOTAS_BF6_VERSION_COMMAS "${HOTAS_BF6_VERSION}")

project(HOTASMapper VERSION ${HOTAS_BF6_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Test)

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
    src/automation_engine.h
    src/automation_engine.cpp
    src/config_store.h
    src/config_store.cpp
    src/controller_readiness.h
    src/controller_readiness.cpp
    src/controller_diagnostics.h
    src/controller_diagnostics.cpp
    src/setup_repair_helper.h
    src/setup_repair_helper.cpp
    src/theme_manager.h
    src/theme_manager.cpp
    src/event_log.h
    src/mapping_worker.h
    src/mapping_worker.cpp
    src/app_backend.h
    src/app_backend.cpp
)

qt_add_executable(HOTASMapper
    ${HOTAS_MAPPER_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hotas_version.rc"
    resources/app.rc
)

qt_add_qml_module(HOTASMapper
    URI HOTASMapper
    VERSION 1.0
    QML_FILES
        qml/AviationPanel.qml
        qml/AutomationPage.qml
        qml/LegacyAviationPanel.qml
        qml/LegacyCurveEditor.qml
        qml/Legacy.qml
        qml/Theme.qml
        qml/Main.qml
        qml/Standard.qml
        qml/CurveEditor.qml
        qml/ControllerReadinessPanel.qml
)

qt_add_resources(HOTASMapper hotas_icon_resources
    PREFIX "/"
    FILES
        assets/icons/png/hotas-bf6-256.png
        assets/themes/topgun/fighter-silhouette.svg
        assets/themes/topgun/wing-badge.svg
        assets/themes/topgun/slash-stripes.svg
)

target_link_libraries(HOTASMapper PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
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
    target_link_libraries(HOTASMapper PRIVATE dinput8 dxguid advapi32 shell32)
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

# The mapper also consumes the same manifest/parser source, but links Qt's
# dynamic MSVC runtime. Keep the launcher core's intentionally static CRT for
# its standalone deployment and build a runtime-compatible sibling for Qt.
add_library(hotas_update_core STATIC
    launcher/launcher_core.cpp
    launcher/launcher_core.h
)
target_compile_features(hotas_update_core PUBLIC cxx_std_20)
target_include_directories(hotas_update_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/launcher")
target_link_libraries(hotas_update_core PUBLIC bcrypt)
target_compile_definitions(hotas_update_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_executable(HOTASBF6Launcher WIN32
    launcher/main.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/launcher_version.rc"
    resources/launcher.rc
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

# The GUI performs only an asynchronous manifest availability check. Parsing,
# version ordering, and manifest validation remain the launcher's shared core.
target_link_libraries(HOTASMapper PRIVATE hotas_update_core)
target_include_directories(HOTASMapper PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/launcher")

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
        src/automation_engine.h
        src/automation_engine.cpp
        src/config_store.h
        src/config_store.cpp
        src/event_log.h
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

    qt_add_executable(automation_engine_tests
        tests/automation_engine_tests.cpp
        src/mapping_types.h
        src/axis_transform.h
        src/axis_transform.cpp
        src/response_curve.h
        src/response_curve.cpp
        src/button_mapping.h
        src/button_mapping.cpp
        src/profile_trigger_runtime.h
        src/profile_trigger_runtime.cpp
        src/automation_engine.h
        src/automation_engine.cpp
        src/config_store.h
        src/config_store.cpp
    )
    target_link_libraries(automation_engine_tests PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(automation_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME automation_engine_tests COMMAND automation_engine_tests)
    if (WIN32)
        set_tests_properties(automation_engine_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()
    add_test(NAME launcher_core_tests COMMAND launcher_core_tests)

    qt_add_executable(controller_readiness_tests
        tests/controller_readiness_tests.cpp
        src/controller_readiness.h
        src/controller_readiness.cpp
        src/controller_diagnostics.h
        src/controller_diagnostics.cpp
        src/mapping_types.h
    )
    target_link_libraries(controller_readiness_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test shell32 advapi32)
    target_include_directories(controller_readiness_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME controller_readiness_tests COMMAND controller_readiness_tests)
    if (WIN32)
        set_tests_properties(controller_readiness_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()

    qt_add_executable(theme_manager_tests
        tests/theme_manager_tests.cpp
        src/theme_manager.h
        src/theme_manager.cpp
    )
    target_link_libraries(theme_manager_tests PRIVATE Qt6::Core Qt6::Test)
    target_include_directories(theme_manager_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME theme_manager_tests COMMAND theme_manager_tests)
    if (WIN32)
        set_tests_properties(theme_manager_tests PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()

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
        src/automation_engine.h
        src/automation_engine.cpp
    )
    target_link_libraries(mapping_hot_path_benchmark PRIVATE Qt6::Core)
    target_include_directories(mapping_hot_path_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # This is a report-producing CI/local diagnostic, not a GUI executable.
    # Keep stdout attached so its latency and throughput evidence is visible.
    set_target_properties(mapping_hot_path_benchmark PROPERTIES WIN32_EXECUTABLE FALSE)
    add_test(NAME mapping_hot_path_benchmark COMMAND mapping_hot_path_benchmark)
    if (WIN32)
        set_tests_properties(mapping_hot_path_benchmark PROPERTIES
            ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
    endif()
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
