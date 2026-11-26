# Sandbox Boundary Tester (SBT)

Sandbox Boundary Tester is an experimental C++20 project for testing isolation boundaries on Linux. Its goal is to evaluate concrete file accesses against an expected isolation policy, including in environments that run sandboxed AI agents.

## Current status

The orchestrator creates a 128-byte canary on the host to represent task A's private file and keeps its descriptor open throughout the experiment. It launches one controlled probe, B, inside Bubblewrap. A separate sandboxed process for A is future work.

Each run selects one of three modes:

| Mode | What B attempts |
| --- | --- |
| `confidentiality` | Open the path read-only and return up to 128 bytes. |
| `persistent-integrity` | Open the path write-only and change the first byte from `A` to `B`. |
| `transient-integrity` | Perform the same write, then restore `A` if the first write succeeds. |

Before writing, B checks that the device and inode of its open descriptor match the identity supplied by the orchestrator. For confidentiality, B reports identity and content separately, so the orchestrator can distinguish matching bytes from a matching file identity.

## Requirements

- Linux with permission to run Bubblewrap and create the namespaces used by the experiment.
- A C++20 compiler. The build commands below use Clang.
- Bubblewrap installed at `/usr/bin/bwrap`.
- A project directory visible inside the sandbox, such as a directory under your home directory.

Run the experiment as your normal user in a development environment. This configuration exposes the host root read-only and the host's `/tmp` through a writable bind mount. It is intended for the controlled probe included in this repository.

## Build

From the project directory:

```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -g -O1 sbt.cpp -o sbt
clang++ -std=c++20 -Wall -Wextra -Wpedantic -g -O1 probe_b.cpp -o probe_b
```

Rebuild both executables after changing `shared_data.h`. They exchange a native `Result` structure through a pipe and must agree on its layout.

## Run

Usage: `./sbt <path_to_test> --mode <mode>`. The path is resolved inside B's sandbox.

Run these examples sequentially from the directory containing both executables:

```bash
./sbt /tmp/private_A.txt --mode confidentiality
./sbt /mnt/private_A.txt --mode confidentiality
./sbt /mnt/private_A.txt --mode persistent-integrity
./sbt /mnt/private_A.txt --mode transient-integrity
```

The orchestrator creates a fresh `/tmp/private_A.txt` for each run. Inside B, `/tmp` is a fresh tmpfs and `/mnt` is a writable bind mount of the host's `/tmp`. Access through `/mnt` is deliberately configured as part of the experiment.

### Expected observations

| Path inside B | Mode | Probe report | Orchestrator observation |
| --- | --- | --- | --- |
| `/tmp/private_A.txt` | `confidentiality` | Open fails with `ENOENT`. | B reports `CannotOpen` and returns zero bytes. |
| `/mnt/private_A.txt` | `confidentiality` | B reads all 128 bytes. | The reported device and inode match the host canary, and all returned bytes match its contents. |
| `/mnt/private_A.txt` | `persistent-integrity` | The first write succeeds. | The file still has 128 bytes and its contents differ from the original. |
| `/mnt/private_A.txt` | `transient-integrity` | The write and restoration both succeed. | The final size and all 128 bytes match the original. |

In the transient case, the same final contents are possible if B restores the byte or if it never writes at all. B reports the write and restoration separately. The orchestrator independently checks the final file through its retained descriptor. It does not sample the intermediate byte.

After sending its report, B pauses. The orchestrator sends `SIGKILL` to its direct child, waits for it and cleans up. A message such as `Child terminated due to signal: 9` is expected with this lifecycle and refers to the direct child launched as Bubblewrap.

If canary creation fails with `EEXIST`, inspect the existing path before removing anything. An interrupted run may have left the file behind. Run one instance at a time.

## Evidence and limitations

- A successful operation demonstrates access through the tested path. A failed operation describes that particular attempt. Interpreting either result requires an expected isolation policy.
- The canary is a fixed, public 128-byte value. Matching content alone cannot establish file identity.
- Device and inode comparisons assume a local filesystem with stable identity semantics.
- The orchestrator trusts the controlled probe's identity, read, write and restoration reports. Independent observation is limited to the final size and contents in integrity mode.
- The pipe protocol uses the native C++ representation of `Result`, including possible padding. It assumes compatible builds of both executables.
- Pipe reads and child termination have no timeout. Cleanup uses a fixed pathname. Path replacement races and interruption-safe cleanup remain unresolved.
- A successful exit indicates that the reporting flow completed. Results are human-readable, and some cleanup failures appear only in diagnostics. The prototype does not issue an automatic policy verdict.

## Validation so far

Manual runs of the earlier read-only prototype on the developer's Ubuntu VM showed `ENOENT` for the hidden path and matching identity plus all 128 bytes for the alternative mount. Repeating the alternative-path run also succeeded. The table above gives expected outcomes for the current three-mode CLI.

## Next step

Record the probe report and the orchestrator's final observation for both integrity modes. Include a run against the hidden path to check that B reports the write as `NotAttempted`.
