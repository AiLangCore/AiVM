cmake_minimum_required(VERSION 3.16)
project(aivm_c_core VERSION 0.0.1 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/.." describe --tags --abbrev=0 --match "v[0-9]*"
    OUTPUT_VARIABLE AIVM_DETECTED_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if (AIVM_DETECTED_TAG)
    string(REGEX REPLACE "^v" "" AIVM_DETECTED_VERSION "${AIVM_DETECTED_TAG}")
else()
    set(AIVM_DETECTED_VERSION "${PROJECT_VERSION}")
endif()

set(AIVM_BUILD_VERSION_OVERRIDE "" CACHE STRING "Explicit AiVM CLI version string override")
if (AIVM_BUILD_VERSION_OVERRIDE)
    set(AIVM_EFFECTIVE_BUILD_VERSION "${AIVM_BUILD_VERSION_OVERRIDE}")
else()
    set(AIVM_EFFECTIVE_BUILD_VERSION "${AIVM_DETECTED_VERSION}")
endif()
set(AIVM_BUILD_CHANNEL "local" CACHE STRING "AiVM CLI release channel")
set(AIVM_BUILD_COMMIT "unknown" CACHE STRING "AiVM CLI source commit")
option(AIVM_BUILD_SHARED "Build shared AiVM.C library for host bridge experiments" OFF)
option(AIVM_BUILD_HOST_TESTS "Build AiLang host integration tests" OFF)
option(AIVM_ENABLE_ASAN "Enable AddressSanitizer for native builds" OFF)
option(AIVM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer for native builds" OFF)
option(AIVM_ENABLE_LSAN "Enable LeakSanitizer for native builds where supported" OFF)
option(AIVM_ENABLE_MSAN "Enable MemorySanitizer for native builds where supported" OFF)

if (AIVM_BUILD_COMMIT STREQUAL "unknown")
    execute_process(
        COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}/.." rev-parse --short=12 HEAD
        OUTPUT_VARIABLE AIVM_DETECTED_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (AIVM_DETECTED_COMMIT)
        set(AIVM_BUILD_COMMIT "${AIVM_DETECTED_COMMIT}" CACHE STRING "AiVM CLI source commit" FORCE)
    endif()
endif()

function(aivm_apply_build_metadata target_name)
    target_compile_definitions(${target_name} PRIVATE
        AIVM_BUILD_VERSION="${AIVM_EFFECTIVE_BUILD_VERSION}"
        AIVM_BUILD_CHANNEL="${AIVM_BUILD_CHANNEL}"
        AIVM_BUILD_COMMIT="${AIVM_BUILD_COMMIT}"
    )
endfunction()

find_package(Threads REQUIRED)

if (MSVC)
    # AivmVm currently owns fixed-size execution arenas. Keep Windows test and
    # CLI stacks large enough until the VM arenas move behind heap-backed storage.
    add_link_options(/STACK:16777216)
endif()

if (NOT MSVC)
    set(AIVM_SANITIZERS "")
    if (AIVM_ENABLE_ASAN)
        list(APPEND AIVM_SANITIZERS "address")
    endif()
    if (AIVM_ENABLE_UBSAN)
        list(APPEND AIVM_SANITIZERS "undefined")
    endif()
    if (AIVM_ENABLE_LSAN AND NOT APPLE)
        list(APPEND AIVM_SANITIZERS "leak")
    endif()
    if (AIVM_ENABLE_MSAN)
        list(APPEND AIVM_SANITIZERS "memory")
    endif()
    if (AIVM_SANITIZERS)
        list(JOIN AIVM_SANITIZERS "," AIVM_SANITIZER_FLAGS)
        add_compile_options(-fsanitize=${AIVM_SANITIZER_FLAGS} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${AIVM_SANITIZER_FLAGS})
    endif()
endif()

file(STRINGS
    "${CMAKE_CURRENT_SOURCE_DIR}/aivm_core_sources.txt"
    AIVM_CORE_SOURCES
)

add_library(aivm_core STATIC ${AIVM_CORE_SOURCES})
target_link_libraries(aivm_core PUBLIC Threads::Threads)

target_include_directories(aivm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(aivm_core PRIVATE /W4)
else()
    target_compile_options(aivm_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ailang_native_bridge STATIC
    ailang_native_bridge.c
)

target_include_directories(ailang_native_bridge PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(ailang_native_bridge PRIVATE /W4)
else()
    target_compile_options(ailang_native_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(ailang_package_manager STATIC
    ailang_package_manager.c
)

target_include_directories(ailang_package_manager PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ailang_package_manager PUBLIC ailang_native_bridge)

if (MSVC)
    target_compile_options(ailang_package_manager PRIVATE /W4)
else()
    target_compile_options(ailang_package_manager PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if (AIVM_BUILD_SHARED)
    add_library(aivm_core_shared SHARED ${AIVM_CORE_SOURCES})
    target_link_libraries(aivm_core_shared PUBLIC Threads::Threads)
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

add_library(aivm_core_debug STATIC ${AIVM_CORE_SOURCES})
target_link_libraries(aivm_core_debug PUBLIC Threads::Threads)

target_include_directories(aivm_core_debug PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(aivm_core_debug PUBLIC AIVM_DEBUG_RUNTIME=1)

if (MSVC)
    target_compile_options(aivm_core_debug PRIVATE /W4 /Zi /Od)
else()
    target_compile_options(aivm_core_debug PRIVATE -Wall -Wextra -Wpedantic -Werror -g -O0)
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

add_executable(aivm
    examples/aivm_cli.c
)

target_link_libraries(aivm PRIVATE aivm_core)
aivm_apply_build_metadata(aivm)

if (MSVC)
    target_compile_options(aivm PRIVATE /W4)
else()
    target_compile_options(aivm PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(aivm_debug
    examples/aivm_cli.c
)

set_target_properties(aivm_debug PROPERTIES OUTPUT_NAME "aivm-debug")

target_link_libraries(aivm_debug PRIVATE aivm_core_debug)
target_compile_definitions(aivm_debug PRIVATE AIVM_DEBUG_RUNTIME=1)
aivm_apply_build_metadata(aivm_debug)

if (MSVC)
    target_compile_options(aivm_debug PRIVATE /W4 /Zi /Od)
else()
    target_compile_options(aivm_debug PRIVATE -Wall -Wextra -Wpedantic -Werror -g -O0)
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
        ../tests/unit/bytecode/test_program_loader.c
    )
    target_link_libraries(aivm_test_program_loader PRIVATE aivm_core)

    add_executable(aivm_test_worker_catalog
        ../tests/unit/bytecode/test_worker_catalog.c
    )
    target_link_libraries(aivm_test_worker_catalog PRIVATE aivm_core)

    add_executable(aivm_test_dynamic_constants
        ../tests/unit/bytecode/test_dynamic_constants.c
    )
    target_link_libraries(aivm_test_dynamic_constants PRIVATE aivm_core)

    add_executable(aivm_test_dynamic_instructions
        ../tests/unit/bytecode/test_dynamic_instructions.c
    )
    target_link_libraries(aivm_test_dynamic_instructions PRIVATE aivm_core)

    add_executable(aivm_test_vm_core
        ../tests/unit/vm/test_vm_core.c
    )
    target_link_libraries(aivm_test_vm_core PRIVATE aivm_core)

    add_executable(aivm_test_worker_capacity
        ../tests/unit/vm/test_worker_capacity.c
    )
    target_link_libraries(aivm_test_worker_capacity PRIVATE aivm_core)

    add_executable(aivm_test_worker_scheduler
        ../tests/unit/vm/test_worker_scheduler.c
    )
    target_link_libraries(aivm_test_worker_scheduler PRIVATE aivm_core)

    add_executable(aivm_test_worker_runtime
        ../tests/unit/vm/test_worker_runtime.c
    )
    target_link_libraries(aivm_test_worker_runtime PRIVATE aivm_core)

    add_executable(aivm_test_vm_worker_tasks
        ../tests/unit/vm/test_vm_worker_tasks.c
    )
    target_link_libraries(aivm_test_vm_worker_tasks PRIVATE aivm_core)

    add_executable(aivm_test_storage_path
        ../tests/unit/host/test_storage_path.c
    )

    add_executable(aivm_test_values
        ../tests/unit/bytecode/test_values.c
    )
    target_link_libraries(aivm_test_values PRIVATE aivm_core)

    add_executable(aivm_test_module_cache
        ../tests/unit/bytecode/test_module_cache.c
    )
    target_link_libraries(aivm_test_module_cache PRIVATE aivm_core)

    add_executable(aivm_test_syscall
        ../tests/integration/syscalls/test_syscall.c
    )
    target_link_libraries(aivm_test_syscall PRIVATE aivm_core)

    add_executable(aivm_test_vm_diagnostics
        ../tests/unit/vm/test_vm_diagnostics.c
    )
    target_link_libraries(aivm_test_vm_diagnostics PRIVATE aivm_core)

    add_executable(aivm_test_vm_determinism
        ../tests/unit/vm/test_vm_determinism.c
    )
    target_link_libraries(aivm_test_vm_determinism PRIVATE aivm_core)

    add_executable(aivm_test_vm_stack
        ../tests/unit/vm/test_vm_stack.c
    )
    target_link_libraries(aivm_test_vm_stack PRIVATE aivm_core)

    add_executable(aivm_test_vm_frames_locals
        ../tests/unit/vm/test_vm_frames_locals.c
    )
    target_link_libraries(aivm_test_vm_frames_locals PRIVATE aivm_core)

    add_executable(aivm_test_vm_ops
        ../tests/unit/vm/test_vm_ops.c
    )
    target_link_libraries(aivm_test_vm_ops PRIVATE aivm_core)

    add_executable(aivm_test_debugger_protocol
        ../tests/unit/vm/test_debugger_protocol.c
    )
    target_link_libraries(aivm_test_debugger_protocol PRIVATE aivm_core)

    add_executable(aivm_test_parity
        ../tests/golden/test_parity.c
    )
    target_link_libraries(aivm_test_parity PRIVATE aivm_core)

    add_executable(aivm_test_runtime
        ../tests/unit/vm/test_runtime.c
    )
    target_link_libraries(aivm_test_runtime PRIVATE aivm_core)

    add_executable(aivm_test_syscall_contracts
        ../tests/unit/syscalls/test_syscall_contracts.c
    )
    target_link_libraries(aivm_test_syscall_contracts PRIVATE aivm_core)

    add_executable(aivm_test_c_api
        ../tests/unit/vm/test_c_api.c
    )
    target_link_libraries(aivm_test_c_api PRIVATE aivm_core)

    add_executable(aivm_test_host_abi
        ../tests/unit/vm/test_host_abi.c
    )
    target_link_libraries(aivm_test_host_abi PRIVATE aivm_core)

    add_executable(aivm_test_ailang_native_bridge
        ../tests/unit/stdlib/test_ailang_native_bridge.c
    )
    target_link_libraries(aivm_test_ailang_native_bridge PRIVATE ailang_native_bridge)

    add_executable(aivm_test_ailang_package_manager
        ../tests/unit/stdlib/test_ailang_package_manager.c
    )
    target_link_libraries(aivm_test_ailang_package_manager PRIVATE ailang_package_manager)

    add_executable(aivm_test_memory_rc
        ../tests/unit/memory/test_memory_rc.c
    )
    target_link_libraries(aivm_test_memory_rc PRIVATE aivm_core)

    add_executable(aivm_test_memory_cycle
        ../tests/unit/memory/test_memory_cycle.c
    )
    target_link_libraries(aivm_test_memory_cycle PRIVATE aivm_core)

    add_executable(aivm_test_debug_artifact_budget
        ../tests/unit/memory/test_debug_artifact_budget.c
    )
    target_link_libraries(aivm_test_debug_artifact_budget PRIVATE aivm_core)

    add_executable(aivm_test_blob_storage
        ../tests/unit/memory/test_blob_storage.c
    )
    target_link_libraries(aivm_test_blob_storage PRIVATE aivm_core)

    add_executable(aivm_test_map_storage
        ../tests/unit/collections/test_map_storage.c
    )
    target_link_libraries(aivm_test_map_storage PRIVATE aivm_core)

    add_executable(aivm_test_spec_coverage
        ../tests/spec/test_spec_coverage.c
    )

    add_executable(aivm_test_loader_fuzz
        ../tests/fuzz/test_loader_fuzz.c
    )
    target_link_libraries(aivm_test_loader_fuzz PRIVATE aivm_core)

    add_executable(aivm_test_security_malformed_inputs
        ../tests/security/test_security_malformed_inputs.c
    )
    target_link_libraries(aivm_test_security_malformed_inputs PRIVATE aivm_core)

    add_executable(aivm_test_runtime_stress
        ../tests/stress/test_runtime_stress.c
    )
    target_link_libraries(aivm_test_runtime_stress PRIVATE aivm_core)

    add_executable(aivm_perf_harness
        ../tests/perf/tools/perf_harness.c
    )
    target_link_libraries(aivm_perf_harness PRIVATE aivm_core)
    aivm_apply_build_metadata(aivm_perf_harness)

    add_executable(aivm_perf_compare
        ../tests/perf/tools/perf_compare.c
    )

    add_executable(aivm_perf_opcode_coverage
        ../tests/perf/tools/perf_opcode_coverage.c
    )
    target_link_libraries(aivm_perf_opcode_coverage PRIVATE aivm_core)

    if (AIVM_BUILD_HOST_TESTS)
        add_executable(aivm_test_bytes_host
            ../tests/integration/syscalls/test_bytes_host.c
        )
        target_link_libraries(aivm_test_bytes_host PRIVATE aivm_core ailang_package_manager)
        if (WIN32)
            target_link_libraries(aivm_test_bytes_host PRIVATE ws2_32 psapi)
        endif()

        add_executable(aivm_test_ui_image_host
            ../tests/integration/syscalls/test_ui_image_host.c
        )
        target_link_libraries(aivm_test_ui_image_host PRIVATE aivm_core ailang_package_manager)
        if (WIN32)
            target_link_libraries(aivm_test_ui_image_host PRIVATE ws2_32 psapi)
        endif()

        add_executable(aivm_test_net_async_host
            ../tests/integration/syscalls/test_net_async_host.c
        )
        target_link_libraries(aivm_test_net_async_host PRIVATE aivm_core ailang_package_manager)
        if (WIN32)
            target_link_libraries(aivm_test_net_async_host PRIVATE ws2_32 psapi)
        endif()

        add_executable(aivm_test_host_open_default
            ../tests/integration/syscalls/test_host_open_default.c
        )
        target_link_libraries(aivm_test_host_open_default PRIVATE aivm_core ailang_package_manager)
        if (WIN32)
            target_link_libraries(aivm_test_host_open_default PRIVATE ws2_32 psapi)
        endif()

        add_executable(aivm_test_host_resource_limits
            ../tests/integration/syscalls/test_host_resource_limits.c
        )
        target_link_libraries(aivm_test_host_resource_limits PRIVATE aivm_core ailang_package_manager)
        if (WIN32)
            target_link_libraries(aivm_test_host_resource_limits PRIVATE ws2_32 psapi)
        endif()
    endif()

    add_executable(aivm_test_remote_channel
        ../tests/unit/vm/test_remote_channel.c
    )
    target_link_libraries(aivm_test_remote_channel PRIVATE aivm_core)

    add_executable(aivm_test_remote_session
        ../tests/unit/vm/test_remote_session.c
    )
    target_link_libraries(aivm_test_remote_session PRIVATE aivm_core)

    add_executable(aivm_test_remote_transport
        ../tests/unit/vm/test_remote_transport.c
    )
    target_link_libraries(aivm_test_remote_transport PRIVATE aivm_core)

    add_executable(aivm_test_remote_ws_frame
        ../tests/unit/vm/test_remote_ws_frame.c
    )
    target_link_libraries(aivm_test_remote_ws_frame PRIVATE aivm_core)

    if (AIVM_BUILD_HOST_TESTS)
        add_executable(aivm_test_process_lifecycle_stress
            ../tests/integration/syscalls/test_process_lifecycle_stress.c
        )
        target_link_libraries(aivm_test_process_lifecycle_stress PRIVATE aivm_core ailang_package_manager)
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
                aivm_test_host_resource_limits PRIVATE
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
    endif()

    if (AIVM_BUILD_SHARED)
        add_executable(aivm_test_shared_bridge_loader
            ../tests/unit/bytecode/test_shared_bridge_loader.c
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
        set_tests_properties(
            aivm_test_shared_bridge_loader
            PROPERTIES LABELS "unit;core;bytecode"
        )
    endif()

    if (MSVC)
        target_compile_options(aivm_test_program_loader PRIVATE /W4)
        target_compile_options(aivm_test_worker_catalog PRIVATE /W4)
        target_compile_options(aivm_test_worker_scheduler PRIVATE /W4)
        target_compile_options(aivm_test_worker_runtime PRIVATE /W4)
        target_compile_options(aivm_test_vm_worker_tasks PRIVATE /W4)
        target_compile_options(aivm_test_storage_path PRIVATE /W4)
        target_compile_options(aivm_test_vm_core PRIVATE /W4)
        target_compile_options(aivm_test_values PRIVATE /W4)
        target_compile_options(aivm_test_module_cache PRIVATE /W4)
        target_compile_options(aivm_test_syscall PRIVATE /W4)
        target_compile_options(aivm_test_vm_diagnostics PRIVATE /W4)
        target_compile_options(aivm_test_vm_determinism PRIVATE /W4)
        target_compile_options(aivm_test_vm_stack PRIVATE /W4)
        target_compile_options(aivm_test_vm_frames_locals PRIVATE /W4)
        target_compile_options(aivm_test_vm_ops PRIVATE /W4)
        target_compile_options(aivm_test_debugger_protocol PRIVATE /W4)
        target_compile_options(aivm_test_parity PRIVATE /W4)
        target_compile_options(aivm_test_runtime PRIVATE /W4)
        target_compile_options(aivm_test_syscall_contracts PRIVATE /W4)
        target_compile_options(aivm_test_c_api PRIVATE /W4)
        target_compile_options(aivm_test_host_abi PRIVATE /W4)
        target_compile_options(aivm_test_ailang_native_bridge PRIVATE /W4)
        target_compile_options(aivm_test_ailang_package_manager PRIVATE /W4)
        target_compile_options(aivm_test_memory_rc PRIVATE /W4)
        target_compile_options(aivm_test_memory_cycle PRIVATE /W4)
        target_compile_options(aivm_test_debug_artifact_budget PRIVATE /W4)
        target_compile_options(aivm_test_blob_storage PRIVATE /W4)
        target_compile_options(aivm_test_map_storage PRIVATE /W4)
        target_compile_options(aivm_test_spec_coverage PRIVATE /W4)
        target_compile_options(aivm_test_loader_fuzz PRIVATE /W4)
        target_compile_options(aivm_test_security_malformed_inputs PRIVATE /W4)
        target_compile_options(aivm_test_runtime_stress PRIVATE /W4)
        target_compile_options(aivm_perf_harness PRIVATE /W4)
        target_compile_options(aivm_perf_compare PRIVATE /W4)
        target_compile_options(aivm_perf_opcode_coverage PRIVATE /W4)
        if (AIVM_BUILD_HOST_TESTS)
            target_compile_options(aivm_test_bytes_host PRIVATE /W4)
            target_compile_options(aivm_test_ui_image_host PRIVATE /W4)
            target_compile_options(aivm_test_net_async_host PRIVATE /W4)
            target_compile_options(aivm_test_host_open_default PRIVATE /W4)
            target_compile_options(aivm_test_host_resource_limits PRIVATE /W4)
        endif()
        target_compile_options(aivm_test_remote_channel PRIVATE /W4)
        target_compile_options(aivm_test_remote_session PRIVATE /W4)
        target_compile_options(aivm_test_remote_transport PRIVATE /W4)
        target_compile_options(aivm_test_remote_ws_frame PRIVATE /W4)
        if (AIVM_BUILD_HOST_TESTS)
            target_compile_options(aivm_test_process_lifecycle_stress PRIVATE /W4)
        endif()
    else()
        target_compile_options(aivm_test_program_loader PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_worker_catalog PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_worker_scheduler PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_worker_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_worker_tasks PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_storage_path PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_values PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_module_cache PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_syscall PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_diagnostics PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_determinism PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_stack PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_frames_locals PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_vm_ops PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_debugger_protocol PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_parity PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_syscall_contracts PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_c_api PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_host_abi PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_ailang_native_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_ailang_package_manager PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_memory_rc PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_memory_cycle PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_debug_artifact_budget PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_blob_storage PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_map_storage PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_spec_coverage PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_loader_fuzz PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_security_malformed_inputs PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_runtime_stress PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_perf_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_perf_compare PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_perf_opcode_coverage PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if (AIVM_BUILD_HOST_TESTS)
            target_compile_options(aivm_test_bytes_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_options(aivm_test_ui_image_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_options(aivm_test_net_async_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_options(aivm_test_host_open_default PRIVATE -Wall -Wextra -Wpedantic -Werror)
            target_compile_options(aivm_test_host_resource_limits PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
        target_compile_options(aivm_test_remote_channel PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_session PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
        target_compile_options(aivm_test_remote_ws_frame PRIVATE -Wall -Wextra -Wpedantic -Werror)
        if (AIVM_BUILD_HOST_TESTS)
            target_compile_options(aivm_test_process_lifecycle_stress PRIVATE -Wall -Wextra -Wpedantic -Werror)
        endif()
    endif()

    add_test(NAME aivm_test_program_loader COMMAND aivm_test_program_loader)
    add_test(NAME aivm_test_worker_catalog COMMAND aivm_test_worker_catalog)
    add_test(NAME aivm_test_dynamic_constants COMMAND aivm_test_dynamic_constants)
    add_test(NAME aivm_test_dynamic_instructions COMMAND aivm_test_dynamic_instructions)
    add_test(NAME aivm_test_vm_core COMMAND aivm_test_vm_core)
    add_test(NAME aivm_test_worker_capacity COMMAND aivm_test_worker_capacity)
    add_test(NAME aivm_test_worker_scheduler COMMAND aivm_test_worker_scheduler)
    add_test(NAME aivm_test_worker_runtime COMMAND aivm_test_worker_runtime)
    add_test(NAME aivm_test_storage_path COMMAND aivm_test_storage_path)
    set_tests_properties(
        aivm_test_storage_path
        PROPERTIES LABELS "unit;host;security"
    )
    add_test(NAME aivm_test_vm_worker_tasks COMMAND aivm_test_vm_worker_tasks)
    add_test(NAME aivm_test_values COMMAND aivm_test_values)
    add_test(NAME aivm_test_module_cache COMMAND aivm_test_module_cache)
    add_test(NAME aivm_test_syscall COMMAND aivm_test_syscall)
    add_test(NAME aivm_test_vm_diagnostics COMMAND aivm_test_vm_diagnostics)
    add_test(NAME aivm_test_vm_determinism COMMAND aivm_test_vm_determinism)
    add_test(NAME aivm_test_vm_stack COMMAND aivm_test_vm_stack)
    add_test(NAME aivm_test_vm_frames_locals COMMAND aivm_test_vm_frames_locals)
    add_test(NAME aivm_test_vm_ops COMMAND aivm_test_vm_ops)
    add_test(NAME aivm_test_debugger_protocol COMMAND aivm_test_debugger_protocol)
    add_test(NAME aivm_test_parity COMMAND aivm_test_parity)
    add_test(NAME aivm_test_runtime COMMAND aivm_test_runtime)
    add_test(NAME aivm_test_syscall_contracts COMMAND aivm_test_syscall_contracts)
    add_test(NAME aivm_test_c_api COMMAND aivm_test_c_api)
    add_test(NAME aivm_test_host_abi COMMAND aivm_test_host_abi)
    add_test(NAME aivm_test_ailang_native_bridge COMMAND aivm_test_ailang_native_bridge)
    add_test(NAME aivm_test_ailang_package_manager COMMAND aivm_test_ailang_package_manager)
    add_test(NAME aivm_test_memory_rc COMMAND aivm_test_memory_rc)
    add_test(NAME aivm_test_memory_cycle COMMAND aivm_test_memory_cycle)
    add_test(NAME aivm_test_debug_artifact_budget COMMAND aivm_test_debug_artifact_budget)
    add_test(NAME aivm_test_blob_storage COMMAND aivm_test_blob_storage)
    add_test(NAME aivm_test_map_storage COMMAND aivm_test_map_storage)
    add_test(NAME aivm_test_spec_coverage COMMAND aivm_test_spec_coverage ${CMAKE_CURRENT_SOURCE_DIR}/..)
    add_test(NAME aivm_test_loader_fuzz COMMAND aivm_test_loader_fuzz)
    add_test(NAME aivm_test_security_malformed_inputs COMMAND aivm_test_security_malformed_inputs)
    add_test(NAME aivm_test_runtime_stress COMMAND aivm_test_runtime_stress)
    add_test(NAME aivm_perf_smoke COMMAND aivm_perf_harness)
    add_test(NAME aivm_perf_full COMMAND aivm_perf_harness)
    add_test(
        NAME aivm_perf_compare_fixture
        COMMAND aivm_perf_compare
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/perf/baselines/fixture-baseline.json
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/perf/baselines/fixture-current.json
            15
    )
    add_test(NAME aivm_perf_opcode_coverage COMMAND aivm_perf_opcode_coverage)
    if (AIVM_BUILD_HOST_TESTS)
        add_test(NAME aivm_test_bytes_host COMMAND aivm_test_bytes_host)
        add_test(NAME aivm_test_ui_image_host COMMAND aivm_test_ui_image_host)
        add_test(NAME aivm_test_net_async_host COMMAND aivm_test_net_async_host)
        add_test(NAME aivm_test_host_open_default COMMAND aivm_test_host_open_default)
        add_test(NAME aivm_test_host_resource_limits COMMAND aivm_test_host_resource_limits)
    endif()
    add_test(NAME aivm_test_remote_channel COMMAND aivm_test_remote_channel)
    add_test(NAME aivm_test_remote_session COMMAND aivm_test_remote_session)
    add_test(NAME aivm_test_remote_transport COMMAND aivm_test_remote_transport)
    add_test(NAME aivm_test_remote_ws_frame COMMAND aivm_test_remote_ws_frame)
    if (AIVM_BUILD_HOST_TESTS)
        add_test(NAME aivm_test_process_lifecycle_stress COMMAND aivm_test_process_lifecycle_stress)
    endif()
    add_test(
        NAME aivm_test_parity_cli
        COMMAND aivm_parity_cli
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/golden/parity_left.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/../tests/golden/parity_right.txt
    )
    if (AIVM_BUILD_HOST_TESTS)
        if (WIN32)
            add_test(
                NAME aivm_test_ailang_smoke
                COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/integration/ctest_ailang_smoke.ps1
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
            )
            add_test(
                NAME aivm_test_task_edge_parity
                COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/golden/ctest_task_edge_parity.ps1
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
                    $<TARGET_FILE:aivm_parity_cli>
            )
            add_test(
                NAME aivm_test_debug_memory_smoke
                COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -File
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/integration/ctest_debug_memory_smoke.ps1
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
            )
        else()
            add_test(
                NAME aivm_test_ailang_smoke
                COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/integration/ctest_ailang_smoke.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
            )
            add_test(
                NAME aivm_test_task_edge_parity
                COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/golden/ctest_task_edge_parity.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
                    $<TARGET_FILE:aivm_parity_cli>
            )
            add_test(
                NAME aivm_test_debug_memory_smoke
                COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/integration/ctest_debug_memory_smoke.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
            )
            add_test(
                NAME aivm_test_process_exit_lifecycle
                COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/integration/ctest_process_exit_lifecycle.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/..
            )
            add_test(
                NAME aivm_test_wasm_golden
                COMMAND bash
                    ${CMAKE_CURRENT_SOURCE_DIR}/../tests/golden/ctest_wasm_golden.sh
                    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
            )
            set_tests_properties(
                aivm_test_wasm_golden
                PROPERTIES SKIP_RETURN_CODE 77
            )
        endif()
    endif()

    set_tests_properties(
        aivm_test_program_loader
        aivm_test_values
        aivm_test_module_cache
        PROPERTIES LABELS "unit;core;bytecode"
    )
    set_tests_properties(
        aivm_test_vm_core
        aivm_test_worker_capacity
        aivm_test_worker_scheduler
        aivm_test_worker_runtime
        aivm_test_vm_worker_tasks
        aivm_test_vm_stack
        aivm_test_vm_frames_locals
        aivm_test_vm_ops
        aivm_test_debugger_protocol
        aivm_test_runtime
        aivm_test_c_api
        PROPERTIES LABELS "unit;core;vm"
    )
    set_tests_properties(
        aivm_test_host_abi
        PROPERTIES LABELS "unit;host;vm"
    )
    set_tests_properties(
        aivm_test_syscall
        PROPERTIES LABELS "integration;syscall;syscalls"
    )
    set_tests_properties(
        aivm_test_syscall_contracts
        PROPERTIES LABELS "unit;syscall;syscalls"
    )
    set_tests_properties(
        aivm_test_vm_diagnostics
        aivm_test_vm_determinism
        PROPERTIES LABELS "unit;determinism;vm"
    )
    set_tests_properties(
        aivm_test_memory_rc
        aivm_test_memory_cycle
        aivm_test_debug_artifact_budget
        aivm_test_blob_storage
        aivm_test_map_storage
        PROPERTIES LABELS "unit;memory"
    )
    set_tests_properties(
        aivm_test_spec_coverage
        PROPERTIES LABELS "spec;coverage"
    )
    set_tests_properties(
        aivm_test_remote_channel
        aivm_test_remote_session
        aivm_test_remote_transport
        aivm_test_remote_ws_frame
        PROPERTIES LABELS "unit;remote;vm"
    )
    set_tests_properties(
        aivm_test_ailang_native_bridge
        aivm_test_ailang_package_manager
        PROPERTIES LABELS "unit;core;stdlib"
    )
    if (AIVM_BUILD_HOST_TESTS)
        set_tests_properties(
            aivm_test_bytes_host
            aivm_test_ui_image_host
            aivm_test_net_async_host
            aivm_test_host_open_default
            aivm_test_host_resource_limits
            aivm_test_process_lifecycle_stress
            aivm_test_ailang_smoke
            PROPERTIES LABELS "integration;host;syscalls"
        )
        set_tests_properties(
            aivm_test_debug_memory_smoke
            PROPERTIES LABELS "integration;memory;host"
        )
    endif()
    set_tests_properties(
        aivm_test_parity
        aivm_test_parity_cli
        PROPERTIES LABELS "golden;parity;stdlib"
    )
    set_tests_properties(
        aivm_test_loader_fuzz
        PROPERTIES LABELS "fuzz"
    )
    set_tests_properties(
        aivm_test_security_malformed_inputs
        PROPERTIES LABELS "security"
    )
    set_tests_properties(
        aivm_test_runtime_stress
        PROPERTIES LABELS "stress"
    )
    set_tests_properties(
        aivm_perf_smoke
        PROPERTIES
            LABELS "perf;perf-smoke"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
            ENVIRONMENT "AIVM_PERF_SUITE=smoke;AIVM_PERF_OUTPUT=artifacts/perf/results-smoke.json"
    )
    set_tests_properties(
        aivm_perf_full
        PROPERTIES
            LABELS "perf;perf-full"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
            ENVIRONMENT "AIVM_PERF_SUITE=full;AIVM_PERF_OUTPUT=artifacts/perf/results-full.json"
    )
    set_tests_properties(
        aivm_perf_compare_fixture
        PROPERTIES LABELS "perf;perf-release"
    )
    set_tests_properties(
        aivm_perf_opcode_coverage
        PROPERTIES LABELS "perf;perf-release"
    )
    if (AIVM_BUILD_HOST_TESTS)
        set_tests_properties(
            aivm_test_task_edge_parity
            PROPERTIES LABELS "golden;parity"
        )
    endif()
    if (AIVM_BUILD_HOST_TESTS AND NOT WIN32)
        set_tests_properties(
            aivm_test_wasm_golden
            PROPERTIES LABELS "golden;parity;wasm"
        )
    endif()
endif()
