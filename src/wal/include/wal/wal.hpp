#pragma once

/**
 * @file wal.hpp
 * @brief Umbrella include for the WAL subsystem.
 *
 * Prefer including a narrower header in production code when possible. This
 * file exists for small tools and tests that need the full WAL surface.
 */

#include "wal/raw_wal_reader.hpp"
#include "wal/raw_wal_writer.hpp"
#include "wal/typed_wal_reader.hpp"
#include "wal/typed_wal_writer.hpp"
#include "wal/wal_alignment.hpp"
#include "wal/wal_checksum.hpp"
#include "wal/wal_commit_policy.hpp"
#include "wal/wal_error.hpp"
#include "wal/wal_file.hpp"
#include "wal/wal_position.hpp"
#include "wal/wal_record_header.hpp"
#include "wal/wal_record_view.hpp"
#include "wal/wal_result.hpp"
#include "wal/wal_segment_header.hpp"
#include "wal/wal_segment_reader.hpp"
#include "wal/wal_segment_scanner.hpp"
#include "wal/wal_segment_writer.hpp"
#include "wal/wal_types.hpp"
