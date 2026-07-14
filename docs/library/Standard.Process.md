# `Standard.Process` pseudo-source

> Target `v0.7` declaration view for planning and LSP design. It is not the
> current transitional API and is not executable Simple source.

```simple
module Standard.Process

ProcessError :: enum {
  NotFound,
  PermissionDenied,
  StartFailed,
  IoFailed
}

ProcessResult :: artifact {
  exitCode :: i32
  stdout :: i32[]
  stderr :: i32[]
}

/// Starts, drains, and reaps a child without blocking the async caller.
run :: Promise<Result<ProcessResult, ProcessError>> (
  program : string,
  arguments : string[]
)

/// Explicit blocking counterpart for scripts that do not use async functions.
runBlocking :: Result<ProcessResult, ProcessError> (
  program : string,
  arguments : string[]
)
```

The target surface replaces transitional `runAsync`; it does not retain an
alias. Text decoding can be layered on `ProcessResult` without losing captured
bytes or exit status.
