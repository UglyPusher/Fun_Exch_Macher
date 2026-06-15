#include "wal/wal_alignment.hpp"
#include "wal/wal_record_header.hpp"

namespace
{
    wal::WalRecordHeader valid_header(std::uint32_t payload_length = 0)
    {
        return wal::WalRecordHeader{
            .record_length = wal::align_up(
                static_cast<std::uint32_t>(sizeof(wal::WalRecordHeader)) + payload_length,
                wal::DefaultRecordAlignment),
            .stream_id = 1,
            .epoch = 1,
            .sequence = 1,
            .payload_length = payload_length
        };
    }
}

int main()
{
    auto header = valid_header();
    if (!header.has_valid_static_fields()) {
        return 1;
    }

    header.magic = 0;
    if (header.has_valid_static_fields()) {
        return 2;
    }

    header = valid_header();
    header.version = 0;
    if (header.has_valid_static_fields()) {
        return 3;
    }

    header = valid_header();
    header.record_length = sizeof(wal::WalRecordHeader) - 1;
    if (header.has_valid_static_fields()) {
        return 4;
    }

    header = valid_header(3);
    if (!header.has_valid_static_fields()) {
        return 5;
    }

    header.record_length = sizeof(wal::WalRecordHeader) + header.payload_length;
    if (header.has_valid_static_fields()) {
        return 6;
    }

    header = valid_header(3);
    header.record_length += wal::DefaultRecordAlignment;
    if (header.has_valid_static_fields()) {
        return 7;
    }

    return 0;
}
