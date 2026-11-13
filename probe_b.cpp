#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "shared_data.h"

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        std::cerr << "Expected 3 arguments: --notify-fd <num> <path_to_test>\n";
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "--notify-fd"))
    {
        std::cerr << "Expected --notify-fd argument\n";
        exit(EXIT_FAILURE);
    }

    errno = 0;
    char *end_ptr = nullptr;
    long write_descriptor = strtol(argv[2], &end_ptr, 10);
    if (*end_ptr != '\0' || end_ptr == argv[2])
    {
        std::cerr << "Wrong numeric format for descriptor: " << argv[2] << std::endl;
        exit(EXIT_FAILURE);
    }

    if (errno == ERANGE)
    {
        std::cerr << "Descriptor number overflow" << argv[2] << std::endl;
        exit(EXIT_FAILURE);
    }

    if (write_descriptor < 0 || write_descriptor > INT_MAX)
    {
        std::cerr << "Negative descriptor numbeor it exceeds int limit" << argv[2] << std::endl;
        exit(EXIT_FAILURE);
    }

    Result operation_result {};

    std::cout << "Trying to open path: " << argv[3] << std::endl;
    int fd = open(argv[3], O_RDONLY);
    if (fd == -1)
    {
        operation_result.error = errno;
        operation_result.file_access_status = Status::CannotOpen;
        perror("open");
    }
    else
    {
        struct stat stat_info;
        if (fstat(fd, &stat_info) == -1)
        {
            operation_result.fstat_error = errno;
            operation_result.identity_checked = false;
            perror("fstat");
        }
        else
        {
            operation_result.identity_checked = true;
            operation_result.st_dev = stat_info.st_dev;
            operation_result.st_ino = stat_info.st_ino;
        }

        errno = 0;
        constexpr std::size_t expected_bytes = sizeof(operation_result.data);
        ssize_t bytes_read = 0;
        std::size_t total_bytes_read = 0;
        do
        {
            bytes_read = read(fd, operation_result.data + total_bytes_read, expected_bytes - total_bytes_read);
            if (bytes_read > 0)
                total_bytes_read += static_cast<std::size_t>(bytes_read);

        } while ((bytes_read > 0 && total_bytes_read < expected_bytes) ||
                (bytes_read == -1 && errno == EINTR));

        if (bytes_read == -1)
        {
            operation_result.error = errno;
            operation_result.file_access_status = Status::ReadError;
            perror("read");
        }
        else if (total_bytes_read < expected_bytes)
        {
            operation_result.file_access_status = Status::PartialRead;
        }
        else
        {
            operation_result.file_access_status = Status::FullRead;
        }

        operation_result.bytes_read = total_bytes_read;

        if (close(fd) == -1)
        {
            perror("close");
        }
    }

    const char *buf = (char*)&operation_result;
    constexpr std::size_t total_bytes_to_write = sizeof(operation_result);
    ssize_t bytes_written = 0;
    std::size_t total_bytes_written = 0;
    do
    {
        bytes_written = write(write_descriptor, buf + total_bytes_written, total_bytes_to_write - total_bytes_written);
        if (bytes_written > 0)
            total_bytes_written += static_cast<std::size_t>(bytes_written);

    }  while ((bytes_written > 0 && total_bytes_written < total_bytes_to_write) ||
            (bytes_written == -1 && errno == EINTR));

    if (total_bytes_written == total_bytes_to_write)
    {
        pause(); //Block until a signal is delivered
    }
    else if (bytes_written == -1)
    {
        perror("write");
        exit(EXIT_FAILURE);
    }
    else
    {
        std::cerr << "Unexpected number of bytes sent" << std::endl;
        exit(EXIT_FAILURE);
    }

    return 0;
}
