cmake_minimum_required(VERSION 3.21)

project(HOTASMapper VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)

qt_standard_project_setup(REQUIRES 6.5)

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
    src/config_store.h
    src/config_store.cpp
    src/mapping_worker.h
    src/mapping_worker.cpp
    src/app_backend.h
    src/app_backend.cpp
)

qt_add_executable(HOTASMapper
    ${HOTAS_MAPPER_SOURCES}
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

if (WIN32)
    target_link_libraries(HOTASMapper PRIVATE dinput8 dxguid advapi32)
    target_compile_definitions(HOTASMapper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
    message(FATAL_ERROR "HOTAS Mapper v1.4 targets Windows (DirectInput and vJoy).")
endif()

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
    )
    target_link_libraries(mapping_hot_path_benchmark PRIVATE Qt6::Core)
    target_include_directories(mapping_hot_path_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
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

install(TARGETS HOTASMapper
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
