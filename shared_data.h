#ifndef SHARED_DATA
#define SHARED_DATA

#include <cstddef>
#include <sys/types.h>

enum class Status {
        CannotOpen = 0,
        ReadError,
        PartialRead,
        FullRead
    };

struct Result
{
    Status file_access_status;
    std::size_t bytes_read;
    int error;
    bool identity_checked;
    dev_t st_dev;
    ino_t st_ino;
    int fstat_error;
    char data[128];
};

#endif
