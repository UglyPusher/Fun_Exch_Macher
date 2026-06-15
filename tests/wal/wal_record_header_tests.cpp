#include "wal/wal_record_header.hpp"

int main()
{
    return wal::wal_record_header_size() == 0 ? 1 : 0;
}
