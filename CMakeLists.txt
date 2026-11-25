cmake_minimum_required(VERSION 3.16)
project(VirtualVehicleLab VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
option(VEHICLE_LAB_SANITIZERS "Build native targets with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(VEHICLE_LAB_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "VEHICLE_LAB_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Native sanitizers enabled: AddressSanitizer + UndefinedBehaviorSanitizer")
endif()
add_library(cantp_lib src/Can.cpp src/CanBus.cpp src/IsoTp.cpp src/CanTp.cpp src/Sender.cpp src/Receiver.cpp src/Sha256.cpp)
target_include_directories(cantp_lib PUBLIC headers)
target_link_libraries(cantp_lib PUBLIC Threads::Threads)
target_compile_options(cantp_lib PRIVATE -Wall -Wextra -Wpedantic)
add_library(vehicle_runtime src/SimulationManager.cpp)
target_link_libraries(vehicle_runtime PUBLIC cantp_lib)
target_compile_options(vehicle_runtime PRIVATE -Wall -Wextra -Wpedantic)
add_executable(vehicle_sim app/main.cpp)
target_link_libraries(vehicle_sim PRIVATE vehicle_runtime)
add_executable(ecu_kernel app/ecu_main.cpp)
target_link_libraries(ecu_kernel PRIVATE vehicle_runtime)
include(CTest)
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    add_executable(run_tests tests/test_can.cpp tests/test_canBus.cpp tests/test_canTp.cpp tests/test_isotp.cpp tests/test_sender.cpp tests/test_receiver.cpp tests/test_runtime.cpp tests/test_kernel.cpp tests/test_sha256.cpp)
    target_link_libraries(run_tests PRIVATE vehicle_runtime GTest::gtest_main)
    include(GoogleTest)
    # These suites use plain TEST cases. Register them at configure time so
    # discovery never writes metadata into the source working directory.
    gtest_add_tests(TARGET run_tests
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TEST_LIST native_tests)
    set_tests_properties(${native_tests} PROPERTIES TIMEOUT 90 LABELS native FAIL_REGULAR_EXPRESSION "Running 0 tests")

    option(VEHICLE_LAB_P0_TESTS "Run Python P0 regressions against the CMake binaries (requires jsonschema)" OFF)
    if(VEHICLE_LAB_P0_TESTS)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        foreach(suite IN ITEMS test_protocols test_envelopes test_adas_fusion test_isotp_vectors test_project_ab.ProjectAB test_performance)
            add_test(NAME P0.${suite}
                COMMAND "${CMAKE_COMMAND}" -E env
                    "SIM_BINARY=$<TARGET_FILE:vehicle_sim>"
                    "ECU_BINARY=$<TARGET_FILE:ecu_kernel>"
                    "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/run_regressions.py" "${suite}")
            set_tests_properties(P0.${suite} PROPERTIES
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 120 LABELS p0)
        endforeach()
    else()
        message(STATUS "Python P0 regressions disabled; enable VEHICLE_LAB_P0_TESTS for the full baseline (native CI requires it)")
    endif()
endif()
install(TARGETS vehicle_sim RUNTIME DESTINATION bin)

# Native C++ core facades (CORE-00)
add_library(vclab_core
    native/core/ApplicationService.cpp
    native/core/RunRecorder.cpp
)
target_include_directories(vclab_core PUBLIC headers native ${CMAKE_SOURCE_DIR}/headers)
target_link_libraries(vclab_core PUBLIC cantp_lib)
target_compile_options(vclab_core PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(run_tests_native tests/native/test_core_facades.cpp)
    target_link_libraries(run_tests_native PRIVATE vclab_core GTest::gtest_main)
    add_test(NAME CoreFacadesTests COMMAND run_tests_native)
    set_tests_properties(CoreFacadesTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
endif()
target_include_directories(vclab_core PUBLIC ${CMAKE_SOURCE_DIR}/headers)
if(BUILD_TESTING)
    target_include_directories(run_tests_native PUBLIC ${CMAKE_SOURCE_DIR}/headers)
    target_include_directories(run_tests_native PUBLIC ${CMAKE_SOURCE_DIR}/native/core ${CMAKE_SOURCE_DIR}/native)
endif()

# Native UI shell (UI-00) - Qt isolated/optional
add_library(vclab_ui
    native/ui/NativeShell.cpp
)
target_include_directories(vclab_ui PUBLIC headers native)
target_link_libraries(vclab_ui PUBLIC vclab_core)
target_compile_options(vclab_ui PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(run_tests_ui tests/native/ui/test_native_shell.cpp)
    target_link_libraries(run_tests_ui PRIVATE vclab_ui GTest::gtest_main)
    target_include_directories(run_tests_ui PUBLIC ${CMAKE_SOURCE_DIR}/native/core ${CMAKE_SOURCE_DIR}/native ${CMAKE_SOURCE_DIR}/headers)
    add_test(NAME NativeUIShellTests COMMAND run_tests_ui)
    set_tests_properties(NativeUIShellTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
endif()

# Native transport (CORE-01)
add_library(vclab_transport
    native/transport/HttpClient.cpp
    native/transport/ProcessClient.cpp
    native/transport/WorkerRegistry.cpp
    native/transport/Barrier.cpp
)
target_include_directories(vclab_transport PUBLIC headers native)
target_link_libraries(vclab_transport PUBLIC vclab_core)
target_compile_options(vclab_transport PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(run_tests_transport tests/native/transport/test_transport.cpp)
    target_link_libraries(run_tests_transport PRIVATE vclab_transport GTest::gtest_main)
    target_include_directories(run_tests_transport PUBLIC ${CMAKE_SOURCE_DIR}/native/core ${CMAKE_SOURCE_DIR}/native ${CMAKE_SOURCE_DIR}/headers)
    add_test(NAME TransportTests COMMAND run_tests_transport)
    set_tests_properties(TransportTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
endif()

# Linux native process/HTTP runtime. Optional UI/world dependencies stay isolated.
add_library(vclab_runtime native/runtime/Worker.cpp native/runtime/Controller.cpp)
target_include_directories(vclab_runtime PUBLIC headers native)
target_link_libraries(vclab_runtime PUBLIC vehicle_runtime vclab_transport)
target_compile_options(vclab_runtime PRIVATE -Wall -Wextra -Wpedantic)
add_executable(vclab app/native_main.cpp)
target_link_libraries(vclab PRIVATE vclab_runtime)
add_executable(vclab_worker app/native_worker.cpp)
target_link_libraries(vclab_worker PRIVATE vclab_runtime)
install(TARGETS vclab vclab_worker RUNTIME DESTINATION bin)

option(VEHICLE_LAB_DESKTOP "Build the native Qt 6 desktop with live ECU workers" OFF)
if(VEHICLE_LAB_DESKTOP)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    # A local package prefix is supported without changing system installations.
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/build/deps/usr")
    find_package(assimp REQUIRED)
    add_library(vclab_desktop_ui native/ui/desktop/DesktopWindow.cpp native/ui/desktop/SceneView.cpp
        native/ui/desktop/Workroom.cpp native/ui/desktop/EcuEditor.cpp native/ui/desktop/PhysicsPanel.cpp
        native/ui/desktop/TrafficPanel.cpp native/ui/desktop/HealthView.cpp native/ui/desktop/CarMesh.cpp)
    target_include_directories(vclab_desktop_ui PUBLIC native headers)
    target_link_libraries(vclab_desktop_ui PUBLIC Qt6::Widgets assimp::assimp)
    target_compile_definitions(vclab_desktop_ui PRIVATE VCLAB_RESOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    if(UNIX AND NOT APPLE AND EXISTS "${CMAKE_SOURCE_DIR}/build/deps/usr/lib/x86_64-linux-gnu")
        target_link_directories(vclab_desktop_ui PUBLIC "${CMAKE_SOURCE_DIR}/build/deps/usr/lib/x86_64-linux-gnu")
        target_link_options(vclab_desktop_ui INTERFACE "-Wl,--disable-new-dtags" "-Wl,-rpath,${CMAKE_SOURCE_DIR}/build/deps/usr/lib/x86_64-linux-gnu")
    endif()
    target_compile_options(vclab_desktop_ui PRIVATE -Wall -Wextra -Wpedantic)
    add_executable(vehicle_lab_desktop app/desktop_main.cpp)
    target_link_libraries(vehicle_lab_desktop PRIVATE vclab_desktop_ui)
    target_compile_definitions(vehicle_lab_desktop PRIVATE VCLAB_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
    target_compile_options(vehicle_lab_desktop PRIVATE -Wall -Wextra -Wpedantic)
    add_dependencies(vehicle_lab_desktop vclab vclab_worker)
    install(TARGETS vehicle_lab_desktop RUNTIME DESTINATION bin)
    if(BUILD_TESTING)
        find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
        add_executable(test_desktop_workspaces tests/desktop/test_workspaces.cpp)
        set_target_properties(test_desktop_workspaces PROPERTIES AUTOMOC ON)
        target_link_libraries(test_desktop_workspaces PRIVATE vclab_desktop_ui Qt6::Test)
        target_compile_definitions(test_desktop_workspaces PRIVATE
            VCLAB_TEST_RUNTIME="$<TARGET_FILE:vclab>" VCLAB_TEST_ROOT="${CMAKE_SOURCE_DIR}")
        add_dependencies(test_desktop_workspaces vclab vclab_worker)
        add_test(NAME NativeDesktop.WorkspacesAndProgramming COMMAND test_desktop_workspaces -platform offscreen)
        set_tests_properties(NativeDesktop.WorkspacesAndProgramming PROPERTIES TIMEOUT 120 LABELS native-desktop)
    endif()
    if(BUILD_TESTING AND VEHICLE_LAB_P0_TESTS)
        add_test(NAME NativeDesktop.LiveDrivingAndLifecycle
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/desktop/test_live_desktop.py"
                "$<TARGET_FILE:vehicle_lab_desktop>" "$<TARGET_FILE:vclab>" "${CMAKE_SOURCE_DIR}")
        set_tests_properties(NativeDesktop.LiveDrivingAndLifecycle PROPERTIES TIMEOUT 100 LABELS "native-desktop")
    endif()
endif()

if(BUILD_TESTING)
    add_executable(run_tests_native_runtime tests/native/test_native_runtime.cpp)
    target_link_libraries(run_tests_native_runtime PRIVATE vclab_runtime GTest::gtest_main)
    target_compile_definitions(run_tests_native_runtime PRIVATE VCLAB_WORKER_PATH="$<TARGET_FILE:vclab_worker>")
    add_dependencies(run_tests_native_runtime vclab_worker)
    gtest_add_tests(TARGET run_tests_native_runtime WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TEST_LIST runtime_tests)
    set_tests_properties(${runtime_tests} PROPERTIES TIMEOUT 60 LABELS native-runtime FAIL_REGULAR_EXPRESSION "Running 0 tests")
    if(VEHICLE_LAB_P0_TESTS)
        add_test(NAME NativeCLI.HttpAndFiles COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tests/native/test_native_cli.py" "$<TARGET_FILE:vclab>" "$<TARGET_FILE:vclab_worker>")
        set_tests_properties(NativeCLI.HttpAndFiles PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 90 LABELS native-runtime)
    endif()
endif()
