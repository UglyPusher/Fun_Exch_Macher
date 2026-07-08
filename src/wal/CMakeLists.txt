add_library(wal STATIC
    src/wal.cpp
    src/wal_position.cpp
    src/wal_record_header.cpp
    src/wal_segment_header.cpp
    src/wal_result.cpp
    src/wal_error.cpp
    src/wal_segment_writer.cpp
    src/wal_segment_reader.cpp
    src/wal_segment_scanner.cpp
    src/wal_checksum.cpp
    src/wal_alignment.cpp
    src/wal_file.cpp
)
add_library(matching_engine_wal ALIAS wal)

target_include_directories(wal
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(wal PUBLIC cxx_std_20)
