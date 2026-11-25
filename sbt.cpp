#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <signal.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include "shared_data.h"
#include "utils.h"

using namespace SBT;

constexpr char canary[] =
    "e4b71906f85c2d3a94e0b76f1c328d5a"
    "6f0a2c9d73e18b45a5d06e9c4b128f37"
    "c93d4f7a10b25e688f0c1a7d62e49b35"
    "58a1e0c7d9f6423b0f7a8d5e21c9634b";
constexpr size_t canary_size = sizeof(canary) - 1;
static_assert(canary_size == 128);
static_assert(canary_size == sizeof(Result::data));

void close_and_delete_canary(int canary_fd)
{
    if (unlink("/tmp/private_A.txt") == -1)
    {
        perror("unlink");
    }

    if (close(canary_fd) == -1)
    {
        perror("close canary file");
    }
}

void kill_wait_and_clean_up(pid_t child_pid, int pipe_read_fd)
{
    // TODO: If kill() fails, a blocking waitpid() may hang indefinitely.
    // Handle child termination failures without losing supervision.
    if (kill(child_pid, SIGKILL) == -1)
    {
        perror("kill");
    }

    int status = 0;
    pid_t res = 0;
    do
    {
        // By default (options = 0): wait only for child termination
        res =  waitpid(child_pid, &status, 0);
    } while (res == -1 && errno == EINTR);

    if (res == -1)
    {
        perror("waitpid");
    }
    else
    {
        // Normal child termination
        if (WIFEXITED(status))
        {
            std::cout << "Child exited with status " << WEXITSTATUS(status) << '\n';        }
        // Child terminated due to signal
        else if (WIFSIGNALED(status))
        {
            std::cout << "Child terminated due to signal: " << WTERMSIG(status) << '\n';        }
        }

    if (close(pipe_read_fd) == -1) //Close read end of pipe
    {
        perror("close pipe");
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Expected path to test" << std::endl;
        return EXIT_FAILURE;
    }

    int canary_fd = open("/tmp/private_A.txt", O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (canary_fd == -1)
    {
        perror("open file");
        return EXIT_FAILURE;
    }

    struct stat stat_info;
    if (fstat(canary_fd, &stat_info) == -1)
    {
        perror("fstat");
        close_and_delete_canary(canary_fd);
        return EXIT_FAILURE;
    }

    if (!write_exact_fd(canary_fd, canary, canary_size))
    {
        close_and_delete_canary(canary_fd);
        return EXIT_FAILURE;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        close_and_delete_canary(canary_fd);
        return EXIT_FAILURE;
    }

    pid_t child_pid = fork();
    if (child_pid == -1)
    {
        perror("fork");
        close_and_delete_canary(canary_fd);
        return EXIT_FAILURE;
    }

    if (child_pid > 0)
    {
        // PARENT CODE
        std::cout << "A: Created child with PID: " << child_pid << '\n';
        if (close(pipefd[1]) == -1) //Close write end of pipe
        {
            perror("close pipe");
            kill_wait_and_clean_up(child_pid, pipefd[0]);
            close_and_delete_canary(canary_fd);
            return EXIT_FAILURE;
        }

        Result child_result {};
        char* buffer = reinterpret_cast<char*>(&child_result);
        constexpr std::size_t bytes_expected = sizeof(child_result);
        const ReadResult result = read_exact_fd(pipefd[0], buffer, bytes_expected);
        if (result.bytes_read != bytes_expected)
        {
            std::cerr << "A: Communication error between parent and child: parent could not read all bytes\n";
            std::cerr << "A: Parent received " << result.bytes_read << " bytes out of expected " << bytes_expected << " bytes\n";
            if (result.error != 0)
                std::cerr << "A: Error during reading through pipe: " << result.error;

            kill_wait_and_clean_up(child_pid, pipefd[0]);
            close_and_delete_canary(canary_fd);

            return EXIT_FAILURE;
        }
        else
        {
            std::cout << "A: Child file access status: " << child_result.file_access_status << std::endl;
            if (child_result.error != 0)
                std::cout << "A: Error experienced by child while trying to open or read private file: " << child_result.error << std::endl;

            if (!child_result.identity_checked)
            {
                std::cout << "A: Child could not check file identity\n";
                if (child_result.file_access_status == Status::CannotOpen)
                {
                    std::cout << "A: Child could not open file with O_RDWR\n";
                }
                else
                {
                    std::cout << "A: File identity could not be checked by child with error " << child_result.fstat_error << std::endl;
                }
            }
            else if (child_result.st_dev == stat_info.st_dev &&
                child_result.st_ino == stat_info.st_ino)
            {
                std::cout << "A: File has same identity in orchestrator and in child\n";
            }
            else
            {
                std::cout << "A: File has different identity in orchestrator and in child\n";
            }

            const std::size_t n = child_result.bytes_read;
            if (n > sizeof(child_result.data))
            {
                std::cerr << "A: Invalid probe result: reported " << n
                          << " bytes, but the buffer holds " << sizeof(child_result.data) << '\n';

                kill_wait_and_clean_up(child_pid, pipefd[0]);
                close_and_delete_canary(canary_fd);
                return EXIT_FAILURE;
            }
            else if (n > 0)
            {
                if (std::memcmp(canary, child_result.data, n) == 0)
                    std::cout << "A: The " << n << " bytes read match the canary prefix\n";
                else
                    std::cout << "A: The " << n << " bytes read differ from the canary prefix\n";
            }
            else if (child_result.file_access_status == Status::PartialRead)
            {
                std::cout << "A: Child reached EOF before reading any bytes\n";
            }

            kill_wait_and_clean_up(child_pid, pipefd[0]);

            //Check if we can detect potential modification once the child process is dead
            //Read file and compare with the canary stored in memory
            {
                errno = 0;
                std::size_t total_bytes_read = 0;
                ssize_t bytes_read = 0;
                char buf[canary_size];
                do
                {
                    //In general code, the static cast to "off_t" could be problematic if file is large
                    bytes_read = pread(canary_fd, buf + total_bytes_read, canary_size - total_bytes_read,
                        static_cast<off_t>(total_bytes_read));
                    if (bytes_read > 0)
                        total_bytes_read += static_cast<size_t>(bytes_read);
                } while((bytes_read > 0 && total_bytes_read < canary_size) ||
                    (bytes_read == -1 && errno == EINTR));

                if (bytes_read == -1)
                {
                    perror("A: read canary");
                    close_and_delete_canary(canary_fd);
                    return EXIT_FAILURE;
                }
                else
                {
                    struct stat canary_file_info;
                    if (fstat(canary_fd, &canary_file_info) == -1)
                    {
                        perror("A: fstat");
                        close_and_delete_canary(canary_fd);
                        return EXIT_FAILURE;
                    }

                    if (canary_size == canary_file_info.st_size)
                        std::cout << "A: File keeps the same size.\n";
                    else
                        std::cout << "A: File size changed to " << canary_file_info.st_size << std::endl;


                    if (std::memcmp(buf, canary, total_bytes_read) == 0 && total_bytes_read == canary_size &&
                        canary_size == canary_file_info.st_size)
                    {
                        std::cout << "A: Same " << total_bytes_read << " bytes\n";
                    }
                    else
                    {
                        std::cout << "A: Not the same " << total_bytes_read << " bytes\n";
                    }
                }
            }

            close_and_delete_canary(canary_fd);
        }
    }
    else
    {   //CHILD CODE
        if (close(pipefd[0]) == -1) //Close read end of pipe
        {
            perror("B: close pipe");
            _exit(EXIT_FAILURE);
        }

        if (close(canary_fd) == -1)
        {
            perror("B: close file on child");
        }

        std::vector<std::string> arguments =
        {
            "bwrap",
            "--ro-bind", "/", "/",
            "--unshare-pid",
            "--proc", "/proc",
            "--dev", "/dev",
            "--bind", "/tmp", "/mnt",
            "--tmpfs", "/tmp",
            "--die-with-parent",
            "./probe_b", "--notify-fd", std::to_string(pipefd[1]), argv[1],
            std::to_string(stat_info.st_dev), std::to_string(stat_info.st_ino)
        };

        std::vector<char*> args_for_execv;
        args_for_execv.reserve(arguments.size() + 1);
        for (auto& str : arguments)
        {
            args_for_execv.push_back(str.data());
        }
        args_for_execv.push_back(nullptr);

        int result = execve("/usr/bin/bwrap", args_for_execv.data(), nullptr);
        if (result == -1)
        {
            perror("B: execve");
            _exit(EXIT_FAILURE);
        }
    }

    return 0;
}
