cmake_minimum_required(VERSION 3.16)

project(matching_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MATCHING_ENGINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(configure_matching_engine_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
    )

    if(MATCHING_ENGINE_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
            $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>
        )
    endif()
endfunction()

add_subdirectory(src/wal)
configure_matching_engine_warnings(wal)

add_library(matching_engine_domain
    src/domain/src/record_types.cpp
)
target_include_directories(matching_engine_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/domain/include
)
configure_matching_engine_warnings(matching_engine_domain)

add_library(matching_engine_core
    src/core/src/dummy_instrument_engine.cpp
    src/core/src/instrument_engine.cpp
    src/core/src/order_book.cpp
    src/core/src/replay.cpp
)
target_include_directories(matching_engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
)
target_link_libraries(matching_engine_core PUBLIC
    matching_engine_domain
    matching_engine_wal
)
configure_matching_engine_warnings(matching_engine_core)

add_library(matching_engine_projections
    src/projections/src/market_data_projection.cpp
)
target_include_directories(matching_engine_projections PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/projections/include
)
target_link_libraries(matching_engine_projections PUBLIC
    matching_engine_domain
)
configure_matching_engine_warnings(matching_engine_projections)

add_executable(matching_engine_app
    src/app/event_dump.cpp
    src/app/main.cpp
    src/app/scenario_loader.cpp
)
set_target_properties(matching_engine_app PROPERTIES
    OUTPUT_NAME matching_engine
)
target_link_libraries(matching_engine_app PRIVATE
    matching_engine_core
    matching_engine_projections
)
configure_matching_engine_warnings(matching_engine_app)

add_executable(load_pipeline_benchmark
    benchmarks/load_pipeline_benchmark.cpp
)
target_link_libraries(load_pipeline_benchmark PRIVATE
    matching_engine_core
)
configure_matching_engine_warnings(load_pipeline_benchmark)

add_executable(wal_demo
    examples/wal_demo.cpp
)
target_link_libraries(wal_demo PRIVATE
    matching_engine_wal
)
configure_matching_engine_warnings(wal_demo)

add_custom_target(run
    COMMAND $<TARGET_FILE:matching_engine_app> run ${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_session.txt ${CMAKE_CURRENT_BINARY_DIR}/wal/commands.wal ${CMAKE_CURRENT_BINARY_DIR}/wal/events.wal
    DEPENDS matching_engine_app
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Run matching_engine demo session"
)

find_program(MATCHING_ENGINE_GDB gdb)
if(MATCHING_ENGINE_GDB)
    add_custom_target(debug
        COMMAND ${MATCHING_ENGINE_GDB} --args $<TARGET_FILE:matching_engine_app> run ${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_session.txt ${CMAKE_CURRENT_BINARY_DIR}/wal/commands.wal ${CMAKE_CURRENT_BINARY_DIR}/wal/events.wal
        DEPENDS matching_engine_app
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Debug matching_engine demo session with gdb"
    )
else()
    add_custom_target(debug
        COMMAND ${CMAKE_COMMAND} -E echo "gdb was not found. Install gdb or run: $<TARGET_FILE:matching_engine_app> run ${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_session.txt ${CMAKE_CURRENT_BINARY_DIR}/wal/commands.wal ${CMAKE_CURRENT_BINARY_DIR}/wal/events.wal"
        DEPENDS matching_engine_app
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
    )
endif()

enable_testing()

function(add_matching_engine_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE matching_engine_core)
    configure_matching_engine_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

function(add_matching_engine_projection_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE matching_engine_projections)
    configure_matching_engine_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_matching_engine_test(order_book_tests tests/order_book_tests.cpp)
add_matching_engine_test(matching_tests tests/matching_tests.cpp)
add_matching_engine_test(replay_tests tests/replay_tests.cpp)
add_matching_engine_test(instrument_engine_wal_pipeline_tests tests/instrument_engine_wal_pipeline_tests.cpp)
add_matching_engine_projection_test(market_data_projection_tests tests/market_data_projection_tests.cpp)

add_matching_engine_test(wal_record_header_tests tests/wal/wal_record_header_tests.cpp)
add_matching_engine_test(wal_segment_writer_tests tests/wal/wal_segment_writer_tests.cpp)
add_matching_engine_test(wal_segment_reader_tests tests/wal/wal_segment_reader_tests.cpp)
add_matching_engine_test(wal_recovery_tests tests/wal/wal_recovery_tests.cpp)
add_matching_engine_test(typed_wal_tests tests/wal/typed_wal_tests.cpp)
add_matching_engine_test(wal_checksum_tests tests/wal/wal_checksum_tests.cpp)
add_matching_engine_test(wal_facade_tests tests/wal/wal_facade_tests.cpp)
