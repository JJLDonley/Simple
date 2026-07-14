# Processes

Simple exposes child-process execution through the experimental `System.Process` resource API and the `Standard.Process` convenience API.

## System.Process

```simple
module Example.Process

import System.Process

main :: i32 () {
  arguments : string[] = ["--version"]
  child : i64 = System.Process.spawn("svm", arguments)
  code : i32 = System.Process.wait(child)
  output : string = System.Process.stdout(child)
  diagnostic : string = System.Process.stderr(child)
  System.Process.close(child)
  return code
}
```

The available operations are:

- `spawn(program, arguments)` creates a runtime-owned process resource and returns its opaque `i64` handle;
- `stdin(handle, text)` writes text to the child's standard input;
- `closeStdin(handle)` sends end-of-input;
- `wait(handle)` closes stdin, waits for termination, drains captured output, and returns the exit status;
- `exitCode(handle)` polls without blocking and returns `-1` while the child is running;
- `stdout(handle)` and `stderr(handle)` return snapshots of captured output, which are complete after `wait`;
- `kill(handle)` forcibly terminates the child;
- `close(handle)` terminates a still-running child, waits for cleanup, closes host handles, and invalidates the resource.

Processes always use piped stdin and captured stdout/stderr in this API revision. Stream inheritance, file redirection, PTYs, and detached mode are not accepted options. This fixed contract avoids silently emulating unsupported modes.

Process handles are generational and VM-owned. Null, stale, wrong-owner, wrong-kind, and closed handles are rejected. Any process left open is terminated and reaped during VM shutdown. Reader threads continuously drain stdout and stderr so a child cannot deadlock merely because its output exceeds a pipe buffer.

## Standard.Process

`Standard.Process` provides:

- `run(program, arguments) -> i32`;
- `runText(program, arguments) -> string`;
- `runBytes(program, arguments) -> i32[]`;
- `runAsync(program, arguments) -> i64`.

The synchronous helpers close stdin, wait, and clean up automatically. `runText` and `runBytes` return captured stdout. `runAsync` returns a runtime-owned `System.Job` handle that can be awaited, polled, cancelled, and closed with `Standard.Promise`. Its Promise result is the child exit status. Cancellation forcibly terminates and reaps the child.

## Portability and security

No process API invokes a command shell. The program and argument list are passed directly to `posix_spawnp` on Linux/macOS and `CreateProcessW` on Windows. Windows arguments are quoted according to command-line parsing rules. A program without a directory component is resolved using the host executable search behavior.

Unix `kill` uses `SIGKILL`; Windows uses `TerminateProcess`. Exit status after forced termination is therefore platform-specific and only guaranteed to be nonzero. Text capture preserves bytes in the runtime string's current ASCII-oriented native boundary; use `runBytes` when byte preservation matters.

Expected host failures currently produce native runtime diagnostics. They will migrate to `Result` values when the standard result surface is complete.
