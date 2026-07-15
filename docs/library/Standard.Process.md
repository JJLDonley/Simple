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

ProcessResult :: class {
  exitCode :: i32
  stdout :: i32[]
  stderr :: i32[]
}

/// Starts, drains, and reaps a child without blocking the async caller.
run :: (
  program : string,
  arguments : string[]
) -> Promise<Result<ProcessResult, ProcessError>>

/// Explicit blocking counterpart for scripts that do not use async functions.
runBlocking :: (
  program : string,
  arguments : string[]
) -> Result<ProcessResult, ProcessError>
```

The target surface replaces transitional `runAsync`; it does not retain an
alias. Text decoding can be layered on `ProcessResult` without losing captured
bytes or exit status.
