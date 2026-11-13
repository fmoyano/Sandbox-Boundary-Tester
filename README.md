# Sandbox Boundary Tester (SBT)

Experimental C++20 project for testing isolation boundaries using controlled probes and verifiable evidence. The intended scope is to evaluate concrete accesses against an expected isolation policy, including environments that run sandboxed AI agents.

## Current status

The first prototype runs one controlled file-reading probe inside Bubblewrap. The orchestrator creates a resource representing A's private file on the host; there is not yet a separate sandboxed A process.

The experiment demonstrates that hiding a file's original path does not prevent access through another mount exposing the same object. This is a deliberately configured access path, not a Bubblewrap vulnerability or a sandbox escape.

There is no policy-file parser, automatic probe selection, integrity test, or general security certification yet.

## Requirements

- Linux with permission to run Bubblewrap and create the namespaces used by this experiment.
- A C++20 compiler (commands below use Clang).
- Bubblewrap installed at `/usr/bin/bwrap`.
- Run from a project directory that remains visible inside the sandbox (for example, under your home directory, not `/tmp`).

Use a development environment and your normal user account. The configuration exposes the host root read-only and is designed for this controlled probe; it is not a configuration for running arbitrary untrusted programs.

## Build

```bash
clang++ -std=c++20 -Wall -Wextra -Wpedantic -g -O1 sbt.cpp -o sbt
clang++ -std=c++20 -Wall -Wextra -Wpedantic -g -O1 probe_b.cpp -o probe_b
```

Both programs must be built together against `shared_data.h`.

## Run

Run these commands sequentially from the directory containing both executables:

```bash
./sbt /tmp/private_A.txt
./sbt /mnt/private_A.txt
```

| Path tested inside B | Expected observation |
| --- | --- |
| `/tmp/private_A.txt` | Opening fails with `ENOENT`: the sandbox has a fresh tmpfs at `/tmp`. |
| `/mnt/private_A.txt` | The probe reports matching device/inode identity and 128 bytes matching the canary. |

The orchestrator creates `/tmp/private_A.txt` with exclusive creation and keeps its descriptor open. Bubblewrap exposes the host `/tmp` read-only at `/mnt`, then mounts a fresh tmpfs at `/tmp`. The probe reports its observations through a pipe.

After receiving the result, the orchestrator sends `SIGKILL` to its direct child, waits for it and cleans up. `Child terminated due to signal: 9` is expected with the current lifecycle. The probe pauses after sending its result; this line describes the direct child, which is launched as Bubblewrap.

The current diagnostic `Error reading canary 0 2` in the negative case means `CannotOpen` with `ENOENT`; the failure occurred during opening, not reading.

If creation fails with `EEXIST`, inspect the existing path before removing anything. A previous interrupted run may have left the file behind. Do not run multiple instances concurrently.

## Evidence and limitations

- A successful controlled probe demonstrates a concrete access path. A failed attempt does not prove that every other path is blocked.
- Whether an access violates isolation depends on the expected policy. Sharing is not inherently a violation.
- The canary is a fixed, public 128-byte value embedded in the orchestrator, not an unpredictable secret.
- Device/inode comparisons assume a local filesystem with stable identity semantics. These assumptions should not be generalized to every filesystem.
- The orchestrator trusts the controlled probe's identity and read reports. It does not independently monitor arbitrary programs.
- `Result` is transferred as a native C++ object representation, including possible padding. This is a same-build prototype protocol, not a portable or hardened wire format.
- Pipe reads and child termination have no timeout. Some failure modes can block indefinitely; cleanup failures are not consistently reflected in the exit status.
- Cleanup uses a fixed pathname. Path replacement races and interruption-safe cleanup remain unresolved.
- A successful program exit indicates completion of the current reporting flow, not a security pass. Results are human-readable, not a stable machine-readable report.

## Validation so far

Manual runs on the developer's Ubuntu VM showed `ENOENT` for the hidden path and matching identity plus all 128 bytes for the alternative mount. Repeating the alternative-path run succeeded, consistent with cleanup between runs. This is a functional check, not a comprehensive automated test suite.

## Next experiment

Add a controlled write attempt and have the orchestrator inspect the retained original file to check final content and length. Compare a persistent modification with a modification that the probe restores before completion, illustrating the limits of checking only final state.
