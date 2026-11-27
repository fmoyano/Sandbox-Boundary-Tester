#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "shared_data.h"
#include "utils.h"

using namespace SBT;

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "B: Expected arguments: --notify-fd <num> <path_to_test>\n";
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "--notify-fd"))
    {
        std::cerr << "B: Expected --notify-fd argument\n";
        return EXIT_FAILURE;
    }

    const std::optional<int> fd_parse = parse<int>(argv[2]);
    if (!fd_parse || *fd_parse < 0)
    {
        std::cerr << "B: Error parsing pipe descriptor\n";
        return EXIT_FAILURE;
    }
    const int notify_fd = *fd_parse;

    Result operation_result {};
    std::cout << "B: Trying to open path: " << argv[3] << std::endl;

    int fd = open(argv[3], O_RDONLY);
    if (fd == -1)
    {
        operation_result.error = errno;
        operation_result.file_access_status = Status::CannotOpen;
        perror("B: open");
    }
    else
    {
        struct stat stat_info;
        if (fstat(fd, &stat_info) == -1)
        {
            operation_result.fstat_error = errno;
            operation_result.identity_checked = false;
            perror("B: fstat");
        }
        else
        {
            operation_result.identity_checked = true;
            operation_result.st_dev = stat_info.st_dev;
            operation_result.st_ino = stat_info.st_ino;
        }

        constexpr std::size_t expected_bytes = sizeof(operation_result.data);
        const ReadResult result = read_exact_fd(fd, operation_result.data, expected_bytes);
        operation_result.bytes_read = result.bytes_read;
        operation_result.error = result.error;
        if (result.error != 0)
            operation_result.file_access_status = Status::ReadError;
        else if (result.bytes_read < expected_bytes)
            operation_result.file_access_status = Status::PartialRead;
        else
            operation_result.file_access_status = Status::FullRead;

        if (close(fd) == -1)
        {
            perror("B: close");
        }
    }

    const char *buf = reinterpret_cast<char*>(&operation_result);
    constexpr std::size_t total_bytes_to_write = sizeof(operation_result);

    if (!write_exact_fd(notify_fd, buf, total_bytes_to_write))
    {
        std::cerr << "B: Error while sending data through pipe to parent" << std::endl;
        return EXIT_FAILURE;
    }
    else
    {
        pause(); //Block until a signal is delivered
    }

    return 0;
}
