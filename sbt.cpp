#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>
#include <signal.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include "shared_data.h"

constexpr char canary[] =
    "e4b71906f85c2d3a94e0b76f1c328d5a"
    "6f0a2c9d73e18b45a5d06e9c4b128f37"
    "c93d4f7a10b25e688f0c1a7d62e49b35"
    "58a1e0c7d9f6423b0f7a8d5e21c9634b";
constexpr size_t canary_size = sizeof(canary) - 1;
static_assert(canary_size == 128);

void kill_wait_and_clean_up(pid_t child_pid, int pipe_read_fd, int canary_fd)
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

    if (unlink("/tmp/private_A.txt") == -1)
    {
        perror("unlink");
    }

    if (close(canary_fd) == -1)
    {
        perror("close canary file");
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Expected path to test" << std::endl;
        exit(EXIT_FAILURE);
    }

    int canary_fd = open("/tmp/private_A.txt", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (canary_fd == -1)
    {
        perror("open file");
        exit(EXIT_FAILURE);
    }

    struct stat stat_info;
    if (fstat(canary_fd, &stat_info) == -1)
    {
        perror("fstat");

        if (unlink("/tmp/private_A.txt") == -1)
        {
            perror("unlink");
        }

        if (close(canary_fd) == -1)
        {
            perror("close canary file");
        }

        exit(EXIT_FAILURE);
    }

    ssize_t bytes_written = 0;
    std::size_t total_bytes_written = 0;
    do
    {
        bytes_written = write(canary_fd, canary + total_bytes_written, canary_size - total_bytes_written);
        if (bytes_written > 0)
            total_bytes_written += static_cast<std::size_t>(bytes_written);

    } while((bytes_written > 0 && total_bytes_written < canary_size) ||
            (bytes_written == -1 && errno == EINTR));

    if (total_bytes_written < canary_size)
    {
        if (bytes_written == -1)
        {
            perror("write to file");
        }

        if (unlink("/tmp/private_A.txt") == -1)
        {
            perror("unlink");
        }

        if (close(canary_fd) == -1)
        {
            perror("close canary file");
        }

        exit(EXIT_FAILURE);
    }

    int pipefd[2];
    if (pipe(pipefd) == -1)
    {
        perror("pipe");

        if (unlink("/tmp/private_A.txt") == -1)
        {
            perror("unlink");
        }

        if (close(canary_fd) == -1)
        {
            perror("close canary file");
        }

        exit(EXIT_FAILURE);
    }


    pid_t child_pid = fork();
    if (child_pid == -1)
    {
        perror("fork");

        if (unlink("/tmp/private_A.txt") == -1)
        {
            perror("unlink");
        }

        if (close(canary_fd) == -1)
        {
            perror("close canary file");
        }

        exit(EXIT_FAILURE);
    }

    if (child_pid > 0)
    {
        std::cout << "Created child with PID: " << child_pid << '\n';
        if (close(pipefd[1]) == -1) //Close write end of pipe
        {
            perror("close pipe");
            kill_wait_and_clean_up(child_pid, pipefd[0], canary_fd);
            exit(EXIT_FAILURE);
        }

        Result child_result {};
        char* buffer = reinterpret_cast<char*>(&child_result);
        constexpr std::size_t bytes_expected = sizeof(child_result);
        ssize_t bytes_read = 0;
        std::size_t total_bytes_read = 0;
        do
        {
            bytes_read = read(pipefd[0], buffer + total_bytes_read, bytes_expected - total_bytes_read);
            if (bytes_read != -1)
                total_bytes_read += static_cast<std::size_t>(bytes_read);

        } while ((bytes_read > 0 && total_bytes_read < bytes_expected) ||
                (bytes_read == -1 && errno == EINTR));


        if (total_bytes_read == bytes_expected)
        {
            if (!child_result.identity_checked)
            {
                if (child_result.file_access_status == Status::CannotOpen)
                {
                    std::cout << "Child could not open file\n";
                }
                else
                {
                    std::cout << "File identity could not be checked by child with error " << child_result.fstat_error;
                }
            }
            else if (child_result.st_dev == stat_info.st_dev &&
                child_result.st_ino == stat_info.st_ino)
            {
                std::cout << "File has same identity in orchestrator and in child\n";
            }
            else
            {
                std::cout << "File has different identity in orchestrator and in child\n";
            }

            if (child_result.bytes_read > 0 && child_result.bytes_read <= sizeof(child_result.data))
            {
                std::cout << "Child read " << child_result.bytes_read << " bytes of canary\n";
                if (std::memcmp(canary, child_result.data, child_result.bytes_read) == 0)
                {
                    std::cout << "Child read " << child_result.bytes_read << " bytes matching the canary prefix\n";
                }
                else
                {
                    std::cout << "Child read something different\n";
                }
            }
            else
            {
                std::cout << "Error reading canary " <<
                        static_cast<int>(child_result.file_access_status) << ' ' <<
                        child_result.error << '\n';
            }

            kill_wait_and_clean_up(child_pid, pipefd[0], canary_fd);
        }
        else //Communication error
        {
            if (bytes_read == -1)
                perror("read");

            kill_wait_and_clean_up(child_pid, pipefd[0], canary_fd);

            exit(EXIT_FAILURE);
        }
    }
    else
    {
        if (close(pipefd[0]) == -1) //Close read end of pipe
        {
            perror("close pipe");
            _exit(EXIT_FAILURE);
        }

        if (close(canary_fd) == -1)
        {
            perror("close file on child");
        }

        std::vector<std::string> arguments =
        {
            "bwrap",
            "--ro-bind", "/", "/",
            "--unshare-pid",
            "--proc", "/proc",
            "--dev", "/dev",
            "--ro-bind", "/tmp", "/mnt",
            "--tmpfs", "/tmp",
            "--die-with-parent",
            "./probe_b", "--notify-fd", std::to_string(pipefd[1]), argv[1]
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
            perror("execve");
            _exit(EXIT_FAILURE);
        }
    }

    return 0;
}
