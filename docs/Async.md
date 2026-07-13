# Jobs and promises

`System.Job` and `Standard.Promise` provide the experimental `v0.5.2` asynchronous runtime foundation. They intentionally do not add `async`/`await` language syntax or execute Simple closures on worker threads. Job imports currently force interpreter fallback so async state always belongs to the executing VM's resource registry.

## State model

Every job-backed promise is in exactly one state:

| Poll value | State | Meaning |
|---:|---|---|
| `0` | pending | The worker has not completed. |
| `1` | completed | `await` returns the stored `i64` result. |
| `2` | failed | `error` returns the copied failure text. |
| `3` | cancelled | Cancellation won the completion race. |

Completion, failure, and cancellation are terminal. The synchronized promise registry rejects a second completion and keeps a terminal record alive until its owning job handle is closed.

## System.Job

| Function | Result | Behavior |
|---|---|---|
| `spawn(delayMs : i32, result : i64)` | `i64` | Start a worker that completes with `result` after the delay. |
| `spawnFailed(delayMs : i32, error : string)` | `i64` | Start a worker that fails with a copied error message. |
| `poll(job : i64)` | `i32` | Return the state code without blocking. |
| `await(job : i64)` | `i64` | Block until terminal; return the result only for completed jobs. |
| `cancel(job : i64)` | `bool` | Request cancellation; true only when cancellation wins. |
| `error(job : i64)` | `string` | Return failure text, or an empty string when no failure exists. |
| `close(job : i64)` | `void` | Cancel if needed, wake the worker, join it, and release the handle. |

Delays must be between zero and 24 hours. Invalid delays return the null handle (`0`).

## Standard.Promise

`Standard.Promise` is the ergonomic surface backed by `System.Job`:

| Function | Result |
|---|---|
| `run(delayMs : i32, result : i64)` | `i64` |
| `runFailed(delayMs : i32, error : string)` | `i64` |
| `await(promise : i64)` | `i64` |
| `poll(promise : i64)` | `i32` |
| `cancel(promise : i64)` | `bool` |
| `isDone(promise : i64)` | `bool` |
| `isFailed(promise : i64)` | `bool` |
| `isCancelled(promise : i64)` | `bool` |
| `error(promise : i64)` | `string` |
| `close(promise : i64)` | `void` |

Example:

```simple
module Examples.Promise

import Standard.Promise

main :: i32 () {
  promise : i64 = Standard.Promise.run(5, 42)
  value : i64 = Standard.Promise.await(promise)
  Standard.Promise.close(promise)
  if (value != 42) { return 1 }
  return 0
}
```

## Ownership and shutdown

A public job/promise value is a VM-owned generational `Job` resource handle. Normal use ends with `close`. On VM shutdown, every live job is cancelled, its condition variable is notified, its worker is joined, and its promise record is released. Shutdown does not wait for the original delay to expire.

`cancel` does not close the handle. This allows code to inspect `isCancelled`, `poll`, or `error` before calling `close`. Calls using a null, stale, foreign-runtime, wrong-kind, or closed handle are rejected by native resource validation.

## Async value and GC boundary

The `v0.5.2` public worker boundary permits only:

- copied scalar `i64` result payloads;
- copied host-owned failure strings;
- cancellation flags and generational promise identity.

VM references, strings, arrays, bytes, artifacts, closures, raw heap pointers, and other resource handles do not cross to worker threads. `spawnFailed` copies its input string before creating the worker, and `error` creates a new VM-owned string when queried. Workers never access the VM heap and never invoke Simple code. This restriction keeps GC roots and VM frame ownership on the executing VM thread.

The internal promise registry can identify reference payloads for future rooted execution, but that capability is not exposed by the `v0.5.2` job API.

## Blocking and deadlock rules

`poll` is non-blocking. `await` is blocking and native metadata marks it accordingly. Current workers only wait on a bounded timer or cancellation notification; they do not wait for VM execution, other promises, channels, or callbacks. Consequently, the current API cannot construct a worker-to-VM dependency cycle. Future jobs that compose processes, networking, or channels must preserve explicit cancellation and shutdown wakeups before they become public.
