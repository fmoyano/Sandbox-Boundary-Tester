#ifndef SHARED_DATA_HPP
#define SHARED_DATA_HPP

#include <cstddef>
#include <ostream>
#include <string_view>
#include <sys/types.h>

namespace SBT {

    enum class Status {
        CannotOpen = 0,
        ReadError,
        PartialRead,
        FullRead
    };

    constexpr std::string_view to_string(Status s) noexcept
    {
        switch (s)
        {
            //Don't use default: that way, compiler will warn you if you include a new enum value in Status
            case Status::CannotOpen: return "CannotOpen";
            case Status::ReadError: return "ReadError";
            case Status::PartialRead: return "PartialRead";
            case Status::FullRead: return "FullRead";
        }

        return "Status=?";
    }

    inline std::ostream& operator<<(std::ostream& os, Status s)
    {
        return os << to_string(s);
    }


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
}

#endif
