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
    if (argc != 7)
    {
        std::cerr << "B: Expected 6 arguments: --notify-fd <num> <path_to_test> <file_device_ID> <file_inode> <mode>\n";
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

    const std::optional<dev_t> dev_t_parse = parse<dev_t>(argv[4]);
    if (!dev_t_parse)
    {
        std::cerr << "B: Problem while parsing dev_t argument\n";
        return EXIT_FAILURE;
    }
    const dev_t expected_st_dev = *dev_t_parse;

    const std::optional<ino_t> ino_t_parse = parse<ino_t>(argv[5]);
    if (!ino_t_parse)
    {
        std::cerr << "B: Problem while parsing ino_t argument\n";
        return EXIT_FAILURE;
    }
    const ino_t expected_st_ino = *ino_t_parse;

    Mode experiment_mode {};
    const char *const mode = argv[6];
    if (std::strcmp(mode, transient_integrity) == 0)
    {
        experiment_mode = Mode::TransientIntegrity;
    }
    else if (std::strcmp(mode, persistent_integrity) == 0)
    {
        experiment_mode = Mode::PersistentIntegrity;
    }
    else
    {
        std::cerr << "B: Invalid mode. Use transient-integrity or persistent-integrity.\n";
        return EXIT_FAILURE;
    }

    Result operation_result {};
    std::cout << "B: Trying to open path: " << argv[3] << std::endl;

    int fd = open(argv[3], O_WRONLY);
    if (fd == -1)
    {
        operation_result.error = errno;
        operation_result.file_access_status = Status::CannotOpen;
        perror("B: open");
    }
    else
    {
        operation_result.file_access_status = Status::Opened;

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

        // The probe will only write if the identity of the file
        // has been confirmed to be the same as the one passed by the parent
        if (operation_result.identity_checked &&
                operation_result.st_dev == expected_st_dev &&
                operation_result.st_ino == expected_st_ino)
        {
            //Write one byte to the file
            ssize_t bytes_written = 0;
            constexpr char buff_to_write = 'B';
            do
            {
                bytes_written = pwrite(fd, &buff_to_write, 1, 0);
            } while (bytes_written == -1 && errno == EINTR);

            if (bytes_written == 1)
            {
                std::cout << "B: Success in changing byte\n";
                operation_result.write_result = WriteResult::Success;

                if (experiment_mode == Mode::TransientIntegrity)
                {
                    //Restore the original byte
                    bytes_written = 0;
                    do
                    {
                        bytes_written = pwrite(fd, &expected_first_byte_integrity, 1, 0);
                    } while (bytes_written == -1 && errno == EINTR);

                    if (bytes_written == 1)
                    {
                        std::cout << "B: Success in restoring byte\n";
                        operation_result.restore_result = WriteResult::Success;
                    }
                    else
                    {
                        if (bytes_written == -1)
                        {
                            operation_result.error = errno;
                            perror("B: write");
                        }

                        std::cerr << "B: Error during write\n";
                        operation_result.restore_result = WriteResult::Error;
                    }
                }
            }
            else
            {
                if (bytes_written == -1)
                {
                    operation_result.error = errno;
                    perror("B: write");
                }

                std::cerr << "B: Error during write\n";
                operation_result.write_result = WriteResult::Error;
            }
        }

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
