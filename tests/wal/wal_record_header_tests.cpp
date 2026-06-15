#include "wal/wal_record_header.hpp"

namespace
{
    int expect(bool condition)
    {
        return condition ? 0 : 1;
    }
}

int main()
{
    wal::WalRecordHeader header{
        .record_length = sizeof(wal::WalRecordHeader),
        .stream_id = 1,
        .epoch = 1,
        .sequence = 1
    };

    if (expect(header.has_valid_static_fields()) != 0) {
        return 1;
    }

    header.magic = 0;
    if (expect(!header.has_valid_static_fields()) != 0) {
        return 2;
    }

    header.magic = wal::WalRecordMagic;
    header.version = 0;
    if (expect(!header.has_valid_static_fields()) != 0) {
        return 3;
    }

    header.version = wal::WalFormatVersion;
    header.record_length = sizeof(wal::WalRecordHeader) - 1;
    if (expect(!header.has_valid_static_fields()) != 0) {
        return 4;
    }

    return 0;
}
