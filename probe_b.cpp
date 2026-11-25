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
    if (argc != 6)
    {
        std::cerr << "B: Expected 5 arguments: --notify-fd <num> <path_to_test> <file_device_ID> <file_inode>\n";
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
        std::cerr << "Error parsing pipe descriptor\n";
        return EXIT_FAILURE;
    }
    const int notify_fd = *fd_parse;

    const std::optional<dev_t> dev_t_parse = parse<dev_t>(argv[4]);
    if (!dev_t_parse)
    {
        std::cerr << "Problem while parsing dev_t argument\n";
        return EXIT_FAILURE;
    }
    const dev_t expected_st_dev = *dev_t_parse;

    const std::optional<ino_t> ino_t_parse = parse<ino_t>(argv[5]);
    if (!ino_t_parse)
    {
        std::cerr << "Problem while parsing ino_t argument\n";
        return EXIT_FAILURE;
    }
    const ino_t expected_st_ino = *ino_t_parse;

    Result operation_result {};
    std::cout << "B: Trying to open path: " << argv[3] << std::endl;
    int fd = open(argv[3], O_RDWR);
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

        {
            //Read file
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
        }

        {
            // The probe will only write if at least one byte has been read and the identity
            // of the file has been confirmed to be the same as the one passed by the parent
            if (operation_result.identity_checked &&
                operation_result.st_dev == expected_st_dev &&
                operation_result.st_ino == expected_st_ino &&
                operation_result.bytes_read > 0)
            {
                //Write one byte to the file
                errno = 0;
                ssize_t bytes_written = 0;
                std::size_t total_bytes_written = 0;
                const char previous_byte = operation_result.data[0];
                constexpr char buff_to_write = 'c';
                constexpr std::size_t num_bytes_to_write = 1;
                do
                {
                    //In general code, the static cast to "off_t" could be problematic if file is large
                    bytes_written = pwrite(fd, &buff_to_write + total_bytes_written, num_bytes_to_write - total_bytes_written,
                        static_cast<off_t>(total_bytes_written));
                    if (bytes_written > 0)
                        total_bytes_written += static_cast<std::size_t>(bytes_written);
                } while ((bytes_written > 0 && total_bytes_written < num_bytes_to_write) ||
                    (bytes_written == -1 && errno == EINTR));

                if (total_bytes_written == num_bytes_to_write)
                {
                    std::cout << "B: Success in changing byte\n";

                    {
                        //Restore the same byte
                        errno = 0;
                        ssize_t bytes_written = 0;
                        std::size_t total_bytes_written = 0;
                        const char buff_to_write = previous_byte;
                        constexpr std::size_t num_bytes_to_write = 1;
                        do
                        {
                            //In general code, the static cast to "off_t" could be problematic if file is large
                            bytes_written = pwrite(fd, &buff_to_write + total_bytes_written, num_bytes_to_write - total_bytes_written,
                                static_cast<off_t>(total_bytes_written));
                            if (bytes_written > 0)
                                total_bytes_written += static_cast<std::size_t>(bytes_written);
                        } while ((bytes_written > 0 && total_bytes_written < num_bytes_to_write) ||
                            (bytes_written == -1 && errno == EINTR));

                        if (total_bytes_written == num_bytes_to_write)
                        {
                            std::cout << "B: Success in restoring byte\n";
                        }
                        else
                        {
                            if (bytes_written == -1)
                                perror("B: write");

                            std::cerr << "B: Error during write\n";
                        }
                    }
                }
                else
                {
                    if (bytes_written == -1)
                        perror("B: write");

                    std::cerr << "B: Error during write\n";
                }
            }

            if (close(fd) == -1)
            {
                perror("B: close");
            }
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
