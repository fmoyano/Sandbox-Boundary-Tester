#ifndef UTILS_H
#define UTILS_H

#include <cerrno>
#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <cstddef>
#include <concepts>

namespace SBT {

    struct ReadResult
    {
        std::size_t bytes_read;
        int error; // errno from a failed read(), or 0 on completion or EOF.
    };

    [[nodiscard]] inline ReadResult read_exact_fd(int fd, char *buf, std::size_t count)
    {
        std::size_t total_bytes_read = 0;

        while (total_bytes_read < count)
        {
            const ssize_t bytes_read = read(fd, buf + total_bytes_read, count - total_bytes_read);
            if (bytes_read > 0)
                total_bytes_read += static_cast<std::size_t>(bytes_read);
            else if (bytes_read == -1 && errno == EINTR)
                continue;
            else
                return ReadResult {total_bytes_read, bytes_read == -1 ? errno : 0};
        }

        return ReadResult {total_bytes_read, 0};
    }

    // Returns false if the write is incomplete. Some bytes may already have been written.
    [[nodiscard]] inline bool write_exact_fd(int fd, const char *buf, std::size_t count)
    {
        std::size_t total_bytes_written = 0;

        while (total_bytes_written < count)
        {
            const ssize_t bytes_written = write(fd, buf + total_bytes_written, count - total_bytes_written);
            if (bytes_written > 0)
                total_bytes_written += static_cast<std::size_t>(bytes_written);
            else if (bytes_written == -1 && errno == EINTR)
                continue;
            else
                return false;
        }

        return true;
    }


    template<std::integral T>
    std::optional<T> parse(std::string_view s, int base = 10)
    {
        T v{};

        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v, base);
        if (ptr != s.data() + s.size() || ec != std::errc{}) return std::nullopt;

        return v;
    }
}

#endif
