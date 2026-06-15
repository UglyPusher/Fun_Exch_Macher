#include "wal/wal_segment_scanner.hpp"

int main()
{
    wal::WalSegmentScanner scanner;
    return scanner.scan("unused.wal").ok ? 0 : 1;
}
