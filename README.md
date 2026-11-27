# Sandbox Boundary Tester (SBT)

Sandbox Boundary Tester is an experimental C++20 project for testing isolation boundaries on Linux. Its goal is to evaluate concrete file accesses against an expected isolation policy, including in environments that run sandboxed AI agents.

## Current status

The orchestrator creates a 128-byte canary on the host to represent task A's private file and keeps its descriptor open throughout the experiment. For each run it launches one of two controlled probes inside Bubblewrap. The selected probe plays the role of B. A separate sandboxed process for A is future work.

The orchestrator uses `getrandom(2)` to generate fresh bytes for each run. Confidentiality uses all 128 random bytes. For integrity, the orchestrator sets byte zero to `A` before writing the file, leaving 127 random bytes. This lets the integrity probe restore the original byte using a write-only descriptor.

| Mode | Executable | What B attempts |
| --- | --- | --- |
| `confidentiality` | `probe_confidentiality` | Open the path read-only and return up to 128 bytes. |
| `persistent-integrity` | `probe_integrity` | Open the path write-only and change the first byte from `A` to `B`. |
| `transient-integrity` | `probe_integrity` | Perform the same write, then restore `A` if the first write succeeds. |

Before writing, B checks that the device and inode of its open descriptor match the identity supplied by the orchestrator. For confidentiality, B reports identity and content separately, so the orchestrator can distinguish matching bytes from a matching file identity.

## Requirements

- Linux with permission to run Bubblewrap and create the namespaces used by the experiment.
- CMake 3.16 or later and a build tool such as Make or Ninja.
- A C++20 compiler. The build commands below use Clang.
- Bubblewrap installed at `/usr/bin/bwrap`.
- A project directory visible inside the sandbox, such as a directory under your home directory.

Run the experiment as your normal user in a development environment. This configuration exposes the host root read-only and the host's `/tmp` through a writable bind mount. It is intended for the controlled probes included in this repository.

## Build

From the project root (the directory containing `CMakeLists.txt`), configure a Debug build with Clang and build `sbt`, `probe_confidentiality` and `probe_integrity`:

```bash
cmake -S . -B build -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

CMake stores the configuration and build files in `build/`. After editing a source file or a shared header, run `cmake --build build` again from the project root. Changes to `shared_data.h` trigger recompilation of the orchestrator and both probes so they agree on the layout of `Result`.

## Run

Usage: `./sbt <path_to_test> --mode <mode>`. The path is resolved inside B's sandbox.

From the project root, enter `build/` and run the examples sequentially. The orchestrator launches `./probe_confidentiality` or `./probe_integrity` relative to its working directory.

```bash
cd build
./sbt /tmp/private_A.txt --mode confidentiality
./sbt /mnt/private_A.txt --mode confidentiality
./sbt /tmp/private_A.txt --mode persistent-integrity
./sbt /mnt/private_A.txt --mode persistent-integrity
./sbt /tmp/private_A.txt --mode transient-integrity
./sbt /mnt/private_A.txt --mode transient-integrity
```

The orchestrator creates a fresh `/tmp/private_A.txt` for each run. Inside B, `/tmp` is a fresh tmpfs and `/mnt` is a writable bind mount of the host's `/tmp`. Access through `/mnt` is deliberately configured as part of the experiment.

### Manual validation

All six combinations above were run on the developer’s Ubuntu VM.

| Path inside B | Mode | Probe report | Orchestrator observation |
| --- | --- | --- | --- |
| `/tmp/private_A.txt` | `confidentiality` | Open fails with `ENOENT`. | B reports `CannotOpen` and returns zero bytes. |
| `/mnt/private_A.txt` | `confidentiality` | B reads all 128 bytes. | The reported device and inode match the host canary, and all returned bytes match its contents. |
| `/tmp/private_A.txt` | `persistent-integrity` | Open failed with `ENOENT`. B skipped the write. | Size and all 128 bytes matched the original. |
| `/mnt/private_A.txt` | `persistent-integrity` | The first write succeeds. | The file still has 128 bytes and its contents differ from the original. |
| `/tmp/private_A.txt` | `transient-integrity` | Open failed with `ENOENT`. B skipped both the write and the restoration. | Size and all 128 bytes matched the original. |
| `/mnt/private_A.txt` | `transient-integrity` | The write and restoration both succeed. | The final size and all 128 bytes match the original. |

In the transient case, the same final contents are possible if B restores the byte or if it never writes at all. B reports the write and restoration separately. The orchestrator independently checks the final file through its retained descriptor. It does not sample the intermediate byte.

After sending its report, B pauses. The orchestrator sends `SIGKILL` to its direct child, waits for it and cleans up. A message such as `Child terminated due to signal: 9` is expected with this lifecycle and refers to the direct child launched as Bubblewrap.

If canary creation fails with `EEXIST`, inspect the existing path before removing anything. An interrupted run may have left the file behind. Run one instance at a time.

## Evidence and limitations

- A successful operation demonstrates access through the tested path. A failed operation describes that particular attempt. Interpreting either result requires an expected isolation policy.
- A full confidentiality match links the returned bytes to the current run. A short matching prefix provides weaker evidence. File identity is checked separately using device and inode.
- Device and inode comparisons assume a local filesystem with stable identity semantics.
- The orchestrator trusts the controlled probe's identity, read, write and restoration reports. Independent observation is limited to the final size and contents in integrity mode.
- The pipe protocol uses the native C++ representation of `Result`, including possible padding. It assumes compatible builds of the orchestrator and the selected probe.
- Pipe reads and child termination have no timeout. Cleanup uses a fixed pathname. Path replacement races and interruption-safe cleanup remain unresolved.
- A successful exit indicates that the reporting flow completed. Results are human-readable, and some cleanup failures appear only in diagnostics. The prototype does not issue an automatic policy verdict.
