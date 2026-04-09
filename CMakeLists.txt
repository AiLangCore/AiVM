cmake_minimum_required(VERSION 3.16)
project(aivm_c_core C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
option(AIVM_BUILD_SHARED "Build shared AiVM.C library for host bridge experiments" OFF)

set(AIVM_CORE_SOURCES
    aivm_types.c
    aivm_vm.c
    aivm_program.c
    sys/aivm_syscall.c
    sys/aivm_syscall_contracts.c
    aivm_parity.c
    aivm_runtime.c
    aivm_c_api.c
    remote/aivm_remote_channel.c
    remote/aivm_remote_session.c
    remote/aivm_remote_transport.c
    remote/aivm_remote_ws_frame.c
)

add_library(aivm_core STATIC ${AIVM_CORE_SOURCES})

target_include_directories(aivm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(aivm_core PRIVATE /W4)
else()
    target_compile_options(aivm_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if (AIVM_BUILD_SHARED)
    add_library(aivm_core_shared SHARED ${AIVM_CORE_SOURCES})
    target_include_directories(aivm_core_shared PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(aivm_core_shared PUBLIC AIVM_BUILD_SHARED_LIB PRIVATE AIVM_CORE_SHARED_IMPL)
    if (MSVC)
        target_compile_options(aivm_core_shared PRIVATE /W4)
    else()
        target_compile_options(aivm_core_shared PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

add_executable(hello_vm
    examples/hello_vm.c
)

target_link_libraries(hello_vm PRIVATE aivm_core)

if (MSVC)
    target_compile_options(hello_vm PRIVATE /W4)
else()
    target_compile_options(hello_vm PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(aivm_parity_cli
    examples/parity_cli.c
)

target_link_libraries(aivm_parity_cli PRIVATE aivm_core)

if (MSVC)
    target_compile_options(aivm_parity_cli PRIVATE /W4)
else()
    target_compile_options(aivm_parity_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(aivm_remote_stdio_bridge
    examples/remote_stdio_bridge.c
)

target_link_libraries(aivm_remote_stdio_bridge PRIVATE aivm_core)

if (MSVC)
    target_compile_options(aivm_remote_stdio_bridge PRIVATE /W4)
else()
    target_compile_options(aivm_remote_stdio_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(aivm_remote_ws_bridge
    examples/remote_ws_bridge.c
)

target_link_libraries(aivm_remote_ws_bridge PRIVATE aivm_core)
if (WIN32)
    target_link_libraries(aivm_remote_ws_bridge PRIVATE ws2_32)
endif()

if (MSVC)
    target_compile_options(aivm_remote_ws_bridge PRIVATE /W4)
else()
    target_compile_options(aivm_remote_ws_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(aivm_test_program_loader
        tests/test_program_loader.c
    )
    target_link_libraries(aivm_test_program_loader PRIVATE aivm_core)

    add_executable(aivm_test_vm_core
        tests/test_vm_core.c
    )
    target_link_libraries(aivm_test_vm_core PRIVATE aivm_core)

    add_executable(aivm_test_values
        tests/test_values.c
    )
    target_link_libraries(aivm_test_values PRIVATE aivm_core)

    add_executable(aivm_test_syscall
        tests/test_syscall.c
    )
    target_link_libraries(aivm_test_syscall PRIVATE aivm_core)

    add_executable(aivm_test_vm_diagnostics
        tests/test_vm_diagnostics.c
    )
    target_link_libraries(aivm_test_vm_diagnostics PRIVATE aivm_core)

    add_executable(aivm_test_vm_determinism
        tests/test_vm_determinism.c
    )
    target_link_libraries(aivm_test_vm_determinism PRIVATE aivm_core)

    add_executable(aivm_test_vm_stack
        tests/test_vm_stack.c
    )
    target_link_libraries(aivm_test_vm_stack PRIVATE aivm_core)

    add_executable(aivm_test_vm_frames_locals
        tests/test_vm_frames_locals.c
    )
    target_link_libraries(aivm_test_vm_frames_locals PRIVATE aivm_core)

    add_executable(aivm_test_vm_ops
        tests/test_vm_ops.c
    )
    target_link_libraries(aivm_test_vm_ops PRIVATE aivm_core)

    add_executable(aivm_test_parity
        tests/test_parity.c
    )
    target_link_libraries(aivm_test_parity PRIVATE aivm_core)

    add_executable(aivm_test_runtime
        tests/test_runtime.c
    )
    target_link_libraries(aivm_test_runtime PRIVATE aivm_core)

    add_executable(aivm_test_syscall_contracts
        tests/test_syscall_contracts.c
    )
    target_link_libraries(aivm_test_syscall_contracts PRIVATE aivm_core)

    add_executable(aivm_test_c_api
        tests/test_c_api.c
    )
    target_link_libraries(aivm_test_c_api PRIVATE aivm_core)

    add_executable(aivm_test_memory_rc
        tests/test_memory_rc.c
    )
    target_link_libraries(aivm_test_memory_rc PRIVATE aivm_core)

    add_executable(aivm_test_memory_cycle
        tests/test_memory_cycle.c
    )
    target_link_libraries(aivm_test_memory_cycle PRIVATE aivm_core)

    add_executable(aivm_test_bytes_host
        tests/test_bytes_host.c
    )
    target_link_libraries(aivm_test_bytes_host PRIVATE aivm_core)
    if (WIN32)
        target_link_libraries(aivm_test_bytes_host PRIVATE ws2_32 psapi)
    endif()

    add_executable(aivm_test_ui_image_host
        tests/test_ui_image_host.c
    )
    target_link_libraries(aivm_test_ui_image_host PRIVATE aivm_core)
    if (WIN32)
        target_link_libraries(aivm_test_ui_image_host PRIVATE ws2_32 psapi)
    endif()

    add_executable(aivm_test_net_async_host
        tests/test_net_async_host.c
    )
    target_link_libraries(aivm_test_net_async_host PRIVATE aivm_core)
    if (WIN32)
        target_link_libraries(aivm_test_net_async_host PRIVATE ws2_32 psapi)
    endif()

    add_executable(aivm_test_host_open_default
        tests/test_host_open_default.c
    )
    target_link_libraries(aivm_test_host_open_default PRIVATE aivm_core)
    if (WIN32)
        target_link_libraries(aivm_test_host_open_default PRIVATE ws2_32 psapi)
    endif()

    add_executable(aivm_test_remote_channel
        tests/test_remote_channel.c
    )
    target_link_libraries(aivm_test_remote_channel PRIVATE aivm_core)

    add_executable(aivm_test_remote_session
        tests/test_remote_session.c
    )
    target_link_libraries(aivm_test_remote_session PRIVATE aivm_core)

    add_executable(aivm_test_remote_transport
        tests/test_remote_transport.c
    )
    target_link_libraries(aivm_test_remote_transport PRIVATE aivm_core)

    add_executable(aivm_test_remote_ws_frame
        tests/test_remote_ws_frame.c
    )
    target_link_libraries(aivm_test_remote_ws_frame PRIVATE aivm_core)

    add_executable(aivm_test_process_lifecycle_stress
        tests/test_process_lifecycle_stress.c
    )
    target_link_libraries(aivm_test_process_lifecycle_stress PRIVATE aivm_core)
    if (WIN32)
        target_link_libraries(aivm_test_process_lifecycle_stress PRIVATE ws2_32 psapi)
    endif()
    if (APPLE)
        target_link_libraries(
            aivm_test_bytes_host PRIVATE
            "-framework AppKit"
            "-framework Foundation"
            "-framework Security"
            "-framework CoreFoundation"
        )
        target_link_libraries(
            aivm_test_ui_image_host PRIVATE
            "-framework AppKit"
            "-framework Foundation"
            "-framework Security"
            "-framework CoreFoundation"
        )
        target_link_libraries(
            aivm_test_net_async_host PRIVATE
            "-framework AppKit"
            "-framework Foundation"
            "-framework Security"
            "-framework CoreFoundation"
        )
        target_link_libraries(
            aivm_test_host_open_default PRIVATE
            "-framework AppKit"
            "-framework Foundation"
            "-framework Security"
            "-framework CoreFoundation"
        )
        target_link_libraries(
            aivm_test_process_lifecycle_stress PRIVATE
            "-framework AppKit"
            "-framework Foundation"
            "-framework Security"
            "-framework CoreFoundation"
        )
    endif()

    if (AIVM_BUILD_SHARED)
        add_executable(aivm_test_shared_bridge_loader
            tests/test_shared_bridge_loader.c
        )
        target_include_directories(aivm_test_shared_bridge_loader PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
        if (MSVC)
            target_compile_options(aivm_test_shared_bridge_loader PRIVATE /W4)
        else()
            target_compile_options(aivm_test_shared_bridge_loader PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        add_test(NAME aivm_test_shared_bridge_loader COMMAND aivm_test_shared_bridge_loader)
    endif()

    if (MSVC)
        target_compile_options(aivm_test_program_loader PRIVATE /W4)
        target_compile_options(aivm_test_vm_core PRIVATE /W4)
        target_compile_options(aivm_test_values PRIVATE /W4)
        target_compile_options(aivm_test_syscall PRIVATE /W4)
        target_compile_options(aivm_test_vm_diagnostics PRIVATE /W4)
        target_compile_options(aivm_test_vm_determinism PRIVATE /W4)
        target_compile_options(aivm_test_vm_stack PRIVATE /W4)
        target_compile_options(aivm_test_vm_frames_locals PRIVATE /W4)
        target_compile_options(aivm_test_vm_ops PRIVATE /W4)
        target_compile_options(aivm_test_parity PRIVATE /W4)
        target_compile_options(aivm_test_runtime PRIVATE /W4)
        target_compile_options(aivm_test_syscall_contracts PRIVATE /W4)
        target_compile_options(aivm_test_c_api PRIVATE /W4)
        target_compile_options(aivm_test_memory_rc PRIVATE /W4)
        target_compile_options(aivm_test_memory_cycle PRIVATE /W4)
        target_compile_options(aivm_test_bytes_host PRIVATE /W4)
        target_compile_options(aivm_test_ui_image_host PRIVATE /W4)
        target_compile_options(aivm_test_net_async_host PRIVATE /W4)
        target_compile_options(aivm_test_host_open_default PRIVATE /W4)
        target_compile_options(aivm_test_remote_channel PRIVATE /W4)
        target_compile_options(aivm_test_remote_session PRIVATE /W4)
        target_compile_options(aivm_test_remote_transport PRIVATE /W4)
        target_compile_options(aivm_test_remote_ws_frame PRIVATE /W4)
        target_compile_options(aivm_test_process_lifecycle_stress PRIVATE /W4)
    else()
        target_compile_options(aivm_test_program_loader PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_values PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_syscall PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_diagnostics PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_determinism PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_stack PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_frames_locals PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_ops PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_parity PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_syscall_contracts PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_c_api PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_memory_rc PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_memory_cycle PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_bytes_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_ui_image_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_net_async_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_host_open_default PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_channel PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_session PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_ws_frame PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_process_lifecycle_stress PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    add_test(NAME aivm_test_program_loader COMMAND aivm_test_program_loader)
    add_test(NAME aivm_test_vm_core COMMAND aivm_test_vm_core)
    add_test(NAME aivm_test_values COMMAND aivm_test_values)
    add_test(NAME aivm_test_syscall COMMAND aivm_test_syscall)
    add_test(NAME aivm_test_vm_diagnostics COMMAND aivm_test_vm_diagnostics)
    add_test(NAME aivm_test_vm_determinism COMMAND aivm_test_vm_determinism)
    add_test(NAME aivm_test_vm_stack COMMAND aivm_test_vm_stack)
    add_test(NAME aivm_test_vm_frames_locals COMMAND aivm_test_vm_frames_locals)
    add_test(NAME aivm_test_vm_ops COMMAND aivm_test_vm_ops)
    add_test(NAME aivm_test_parity COMMAND aivm_test_parity)
    add_test(NAME aivm_test_runtime COMMAND aivm_test_runtime)
    add_test(NAME aivm_test_syscall_contracts COMMAND aivm_test_syscall_contracts)
    add_test(NAME aivm_test_c_api COMMAND aivm_test_c_api)
    add_test(NAME aivm_test_memory_rc COMMAND aivm_test_memory_rc)
    add_test(NAME aivm_test_memory_cycle COMMAND aivm_test_memory_cycle)
    add_test(NAME aivm_test_bytes_host COMMAND aivm_test_bytes_host)
    add_test(NAME aivm_test_ui_image_host COMMAND aivm_test_ui_image_host)
    add_test(NAME aivm_test_net_async_host COMMAND aivm_test_net_async_host)
    add_test(NAME aivm_test_host_open_default COMMAND aivm_test_host_open_default)
    add_test(NAME aivm_test_remote_channel COMMAND aivm_test_remote_channel)
    add_test(NAME aivm_test_remote_session COMMAND aivm_test_remote_session)
    add_test(NAME aivm_test_remote_transport COMMAND aivm_test_remote_transport)
    add_test(NAME aivm_test_remote_ws_frame COMMAND aivm_test_remote_ws_frame)
    add_test(NAME aivm_test_process_lifecycle_stress COMMAND aivm_test_process_lifecycle_stress)
    add_test(
        NAME aivm_test_parity_cli
        COMMAND aivm_parity_cli
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity_left.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity_right.txt
    )
    if (WIN32)
        add_test(
            NAME aivm_test_airun_smoke
            COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_airun_smoke.ps1
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        )
        add_test(
            NAME aivm_test_task_edge_parity
            COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_task_edge_parity.ps1
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
                $<TARGET_FILE:aivm_parity_cli>
        )
        add_test(
            NAME aivm_test_debug_memory_smoke
            COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_debug_memory_smoke.ps1
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        )
    else()
        add_test(
            NAME aivm_test_airun_smoke
            COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_airun_smoke.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        )
        add_test(
            NAME aivm_test_task_edge_parity
            COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_task_edge_parity.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
                $<TARGET_FILE:aivm_parity_cli>
        )
        add_test(
            NAME aivm_test_debug_memory_smoke
            COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_debug_memory_smoke.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        )
        add_test(
            NAME aivm_test_wasm_golden
            COMMAND bash
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_wasm_golden.sh
                ${CMAKE_CURRENT_SOURCE_DIR}/../../..
        )
        set_tests_properties(
            aivm_test_wasm_golden
            PROPERTIES SKIP_RETURN_CODE 77
        )
    endif()

    set_tests_properties(
        aivm_test_program_loader
        aivm_test_vm_core
        aivm_test_values
        aivm_test_vm_stack
        aivm_test_vm_frames_locals
        aivm_test_vm_ops
        aivm_test_runtime
        aivm_test_c_api
        PROPERTIES LABELS "unit;core"
    )
    set_tests_properties(
        aivm_test_syscall
        aivm_test_syscall_contracts
        PROPERTIES LABELS "unit;syscall"
    )
    set_tests_properties(
        aivm_test_vm_diagnostics
        aivm_test_vm_determinism
        PROPERTIES LABELS "unit;determinism"
    )
    set_tests_properties(
        aivm_test_memory_rc
        aivm_test_memory_cycle
        PROPERTIES LABELS "unit;memory"
    )
    set_tests_properties(
        aivm_test_remote_channel
        aivm_test_remote_session
        aivm_test_remote_transport
        aivm_test_remote_ws_frame
        PROPERTIES LABELS "unit;remote"
    )
    set_tests_properties(
        aivm_test_bytes_host
        aivm_test_ui_image_host
        aivm_test_net_async_host
        aivm_test_host_open_default
        aivm_test_process_lifecycle_stress
        aivm_test_airun_smoke
        PROPERTIES LABELS "integration;host"
    )
    set_tests_properties(
        aivm_test_debug_memory_smoke
        PROPERTIES LABELS "integration;memory;host"
    )
    set_tests_properties(
        aivm_test_parity
        aivm_test_parity_cli
        aivm_test_task_edge_parity
        PROPERTIES LABELS "integration;parity"
    )
    if (NOT WIN32)
        set_tests_properties(
            aivm_test_wasm_golden
            PROPERTIES LABELS "integration;parity;wasm"
        )
    endif()
endif()
