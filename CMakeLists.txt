cmake_minimum_required(VERSION 3.21)

project(HOTASMapper VERSION 1.2.0 LANGUAGES CXX)

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
        qml/Main.qml
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
    message(FATAL_ERROR "HOTAS Mapper v1.1 targets Windows (DirectInput and vJoy).")
endif()

include(CTest)
if (BUILD_TESTING)
    qt_add_executable(mapping_core_tests
        tests/mapping_core_tests.cpp
        src/mapping_types.h
        src/axis_transform.h
        src/axis_transform.cpp
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
endif()

install(TARGETS HOTASMapper
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
