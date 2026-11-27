#ifndef SHARED_DATA_HPP
#define SHARED_DATA_HPP

#include <cstddef>
#include <ostream>
#include <string_view>
#include <sys/types.h>

namespace SBT {

    inline constexpr char confidentiality[] = "confidentiality";
    inline constexpr char transient_integrity[] = "transient-integrity";
    inline constexpr char persistent_integrity[] = "persistent-integrity";

    // This is the first byte of the canary: used for the integrity test
    inline constexpr char expected_first_byte_integrity = 'A';

    enum class Mode {
        Confidentiality = 0,
        TransientIntegrity,
        PersistentIntegrity
    };

    enum class Status {
        CannotOpen = 0,
        Opened,
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
            case Status::Opened: return "Opened";
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

    enum class WriteResult {
        NotAttempted = 0,
        Success,
        Error
    };

    constexpr std::string_view to_string(WriteResult wr) noexcept
    {
        switch (wr)
        {
            //Don't use default: that way, compiler will warn you if you include a new enum value in Status
            case WriteResult::NotAttempted: return "Not attempted write";
            case WriteResult::Success: return "Successful write";
            case WriteResult::Error: return "Write error";
        }

        return "Status=?";
    }

    inline std::ostream& operator<<(std::ostream& os, WriteResult wr)
    {
        return os << to_string(wr);
    }

    struct Result
    {
        Status file_access_status;
        WriteResult write_result;
        WriteResult restore_result;
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
