cmake_minimum_required(VERSION 3.16)

project(matching_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/wal)

add_library(matching_engine_domain
    src/domain/src/record_types.cpp
)
target_include_directories(matching_engine_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/domain/include
)

add_library(matching_engine_core
    src/core/src/dummy_instrument_engine.cpp
)
target_include_directories(matching_engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/include
)
target_link_libraries(matching_engine_core PUBLIC
    matching_engine_domain
    matching_engine_wal
)

add_executable(matching_engine_app
    src/app/main.cpp
)
set_target_properties(matching_engine_app PROPERTIES
    OUTPUT_NAME matching_engine
)
target_link_libraries(matching_engine_app PRIVATE
    matching_engine_core
)

add_custom_target(run
    COMMAND $<TARGET_FILE:matching_engine_app> ${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_session.txt
    DEPENDS matching_engine_app
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    USES_TERMINAL
    COMMENT "Run matching_engine with examples/simple_session.txt"
)

find_program(MATCHING_ENGINE_GDB gdb)
if(MATCHING_ENGINE_GDB)
    add_custom_target(debug
        COMMAND ${MATCHING_ENGINE_GDB} --args $<TARGET_FILE:matching_engine_app> ${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_session.txt
        DEPENDS matching_engine_app
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Debug matching_engine with gdb"
    )
else()
    add_custom_target(debug
        COMMAND ${CMAKE_COMMAND} -E echo "gdb was not found. Install gdb or run: $<TARGET_FILE:matching_engine_app> ${CMAKE_CURRENT_SOURCE_DIR}/examples/simple_session.txt"
        DEPENDS matching_engine_app
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
    )
endif()

enable_testing()

function(add_matching_engine_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE matching_engine_core)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_matching_engine_test(order_book_tests tests/order_book_tests.cpp)
add_matching_engine_test(matching_tests tests/matching_tests.cpp)
add_matching_engine_test(replay_tests tests/replay_tests.cpp)
add_matching_engine_test(dummy_engine_wal_pipeline_tests tests/dummy_engine_wal_pipeline_tests.cpp)

add_matching_engine_test(wal_record_header_tests tests/wal/wal_record_header_tests.cpp)
add_matching_engine_test(wal_segment_writer_tests tests/wal/wal_segment_writer_tests.cpp)
add_matching_engine_test(wal_segment_reader_tests tests/wal/wal_segment_reader_tests.cpp)
add_matching_engine_test(wal_recovery_tests tests/wal/wal_recovery_tests.cpp)
add_matching_engine_test(typed_wal_tests tests/wal/typed_wal_tests.cpp)
add_matching_engine_test(wal_checksum_tests tests/wal/wal_checksum_tests.cpp)
