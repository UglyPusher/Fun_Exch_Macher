#include "wal/wal_segment_reader.hpp"

int main()
{
    wal::WalSegmentReader reader{"unused.wal"};
    wal::WalRecordView record;
    return reader.read_next(record).status == wal::WalReadStatus::EndOfLog ? 0 : 1;
}
