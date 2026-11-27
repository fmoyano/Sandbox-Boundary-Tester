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
#include <sys/random.h>
#include "shared_data.h"
#include "utils.h"

using namespace SBT;

constexpr size_t canary_size = sizeof(Result::data);
static_assert(canary_size == 128);
constexpr char const *private_file = "/tmp/private_A.txt";

void close_and_delete_canary(int canary_fd)
{
    if (unlink(private_file) == -1)
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
    if (argc != 4)
    {
        std::cerr << "Incorrect number of arguments: sbt <path_to_test> --mode <mode>\n";
        return EXIT_FAILURE;
    }

    if (std::strcmp(argv[2], "--mode"))
    {
        std::cerr << "Missing --mode: sbt <path_to_test> --mode <mode>\n";
        return EXIT_FAILURE;
    }

    Mode experiment_mode {};
    const char *const mode = argv[3];
    if (std::strcmp(mode, confidentiality) == 0)
    {
        experiment_mode = Mode::Confidentiality;
    }
    else if (std::strcmp(mode, transient_integrity) == 0)
    {
        experiment_mode = Mode::TransientIntegrity;
    }
    else if (std::strcmp(mode, persistent_integrity) == 0)
    {
        experiment_mode = Mode::PersistentIntegrity;
    }
    else
    {
        std::cerr << "A: Invalid mode. Use confidentiality, transient-integrity or persistent-integrity.\n";
        return EXIT_FAILURE;
    }

    char canary[canary_size];
    ssize_t bytes_read = 0;
    do
    {
        bytes_read = getrandom(canary, canary_size, 0);

        //According to the manual, this situation should never happen for sizes <= 256...
        //"but the careful programmer will check for this anyway!"
        if (bytes_read >= 0 && static_cast<size_t>(bytes_read) < canary_size)
        {
            std::cerr << "Obtained bytes vs expected bytes: " << bytes_read << " " << canary_size << std::endl;
            return EXIT_FAILURE;
        }
    } while (bytes_read == -1 && errno == EINTR);
    if (bytes_read == -1)
    {
        perror("getrandom");
        return EXIT_FAILURE;
    }

    if (experiment_mode != Mode::Confidentiality)
    {
        canary[0] = expected_first_byte_integrity;
    }

    int canary_fd = open(private_file, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
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
                std::cout << "A: Error experienced by child while trying to open, read or write file: " << child_result.error << std::endl;

            if (!child_result.identity_checked)
            {
                std::cout << "A: Child could not check file identity\n";
                if (child_result.file_access_status == Status::CannotOpen)
                {
                    std::cout << "A: Child could not open file\n";
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

            if (experiment_mode == Mode::Confidentiality)
            {
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
            }

            kill_wait_and_clean_up(child_pid, pipefd[0]);

            //Check if we can detect potential modification once the child process is dead
            //Read file and compare with the canary stored in memory
            if (experiment_mode == Mode::PersistentIntegrity ||
                experiment_mode == Mode::TransientIntegrity)
            {

                std::cout << "A: Child write result: " << child_result.write_result << std::endl;
                if (experiment_mode == Mode::TransientIntegrity)
                {
                    std::cout << "A: Child restore result: " << child_result.restore_result << std::endl;
                }

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


                    if (std::memcmp(buf, canary, total_bytes_read) == 0 &&
                        total_bytes_read == canary_size &&
                        canary_size == canary_file_info.st_size)
                    {
                        std::cout << "A: Same " << total_bytes_read << " bytes\n";
                    }
                    else
                    {
                        std::cout << "A: Canary differs in size or content. Total bytes read: "
                                << total_bytes_read << std::endl;
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
                "--die-with-parent"
        };

        const bool is_confidentiality = experiment_mode == Mode::Confidentiality;
        arguments.insert(arguments.end(), {
            is_confidentiality ? "./probe_confidentiality" : "./probe_integrity",
            "--notify-fd",
            std::to_string(pipefd[1]),
            argv[1] //path to test
        });

        if (!is_confidentiality)
        {
            arguments.insert(arguments.end(),
            {
                std::to_string(stat_info.st_dev),
                std::to_string(stat_info.st_ino),
                argv[3] //transient-integrity or persistent-integrity
            });
        }

        std::vector<char*> args_for_execv;
        args_for_execv.reserve(arguments.size() + 1);
        for (auto& str : arguments)
        {
            args_for_execv.push_back(str.data());
        }
        args_for_execv.push_back(nullptr);

        execve("/usr/bin/bwrap", args_for_execv.data(), nullptr);
        perror("B: execve");
        _exit(EXIT_FAILURE);
    }

    return 0;
}
