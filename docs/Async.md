# Jobs, promises, and async design

`System.Job` and `Standard.Promise` provide the experimental `v0.5.2` runtime
foundation. The tables in this document describe that **current transitional
API**. It does not yet implement the accepted `v0.6` language syntax, and its
`await` members and raw `i64` promise handles are not the final public design.
Job imports currently force interpreter fallback so async state remains owned
by the executing VM's resource registry.

## Target language surface

`v0.5.15` implements the optional/Result/ZII and postfix-propagation pieces
below. `Promise<T>`, `async`, `await`, and suspension remain target behavior.

The accepted language design is:

```simple
fetchBody :: async Result<string, HttpError> (url : string) {
  response :: Response = await Standard.HTTP.get(url)?
  return response.bodyText()
}
```

- `async` follows `:` or `::` and precedes the real, unwrapped return type;
- calling an async function wraps that declared type as `Promise<T>`;
- prefix `await` unwraps `Promise<T>` and is legal only inside `async` functions;
- postfix type `T?` represents ZII absence with `{}` absent and `{ value }` present;
- postfix expression `expr?` unwraps `Result<T,E>` or optional `T?`, returning the
  matching failure/absence shape from the enclosing function immediately;
- no operation throws, invents a default, exposes an inactive zeroed payload, or
  silently discards failure.

See [Language reference](Language.md#async-functions-and-explicit-failure-design)
for the complete design. This work depends on the same `v0.6` language milestone
completing concrete generics, lambdas, and closures: generic specialization
provides `Promise<Result<T,E>>` layouts, while rooted closure environments
provide safe async bodies and callbacks. Async syntax does not replace any of
those features. Optional `T?` directly replaces experimental `Option<T>` with
no alias. Native/standard library expansion does not begin until the language,
external-FFI pointer contract, and conformance burn-in are complete.

An async optional pipeline uses both postfix roles without ambiguity:

```simple
getDataFromAlgo :: async i32? () {
  if (!algorithmHasResult()) { return {} }
  return { calculateResult() }
}

consumeData :: async i32? () {
  result :: i32? = await getDataFromAlgo()
  value :: i32 = result?
  return { value * 2 }
}
```

The declaration `i32?` is a type. The expression `result?` produces `i32` when
present and completes `consumeData` with absence otherwise. The combined form
`value :: i32 = await getDataFromAlgo()?` means
`(await getDataFromAlgo())?`.

## Target `Promise<T>` state model

`Promise<T>` is a managed language type, not an `i64` convention or a Result
alias. It owns three states: `Pending`, `Completed(T)`, and `Cancelled`.

Under ZII, an all-zero Promise is terminal `Cancelled` with no producer or
active payload. The runtime explicitly initializes Pending state, and only an
atomic producer transition activates Completed state. Zeroed payload bytes are
never interpreted while Pending or Cancelled.

For example, `Promise<i32?>` distinguishes all outcomes:

| State | `await` behavior |
|---|---|
| Pending | suspend and produce nothing |
| Cancelled | propagate cancellation and run cleanup |
| Completed with `{}` | produce absent `i32?` |
| Completed with `{ 0 }` | produce present integer zero |

`Completed` means asynchronous production finished; it does not claim the
operation represented by `T` succeeded. Expected failures belong in `T`, usually
as `Promise<Result<Value, DomainError>>`. Such a Promise completes with a Result
carrying either its success payload or typed error payload, and user code
handles or propagates that Result after `await`. Result construction and
matching use contextual `.value`/`.error` literals and structural patterns, not
constructor names:

```simple
response :: Response = await Standard.HTTP.get(url)?
```

The final Promise design does not carry a separate copied-string
failed/rejected state. Cancellation is structured asynchronous control rather
than a domain error:

- awaiting `Completed(value)` produces `value`;
- awaiting `Pending` suspends the current async frame;
- awaiting `Cancelled` cancels the enclosing frame's Promise, performs required
  cleanup, and skips all later statements in that frame.

Cancellation never invents a `T`, becomes absence, becomes an implicit Result
error, or throws. It propagates through the active await chain and is observable through Promise
state/control APIs. Resolution and cancellation race atomically, with exactly
one terminal winner. Cancellation wakes suspended continuations and is
idempotent after reaching a terminal state.

Managed promises do not expose public `close`. The VM owns pending state, roots
continuations and payloads, and releases terminal state when runtime ownership
and GC reachability allow. The current raw job handle and explicit `close` API
remain transitional implementation behavior.

## Library naming policy

Final `System.*` and `Standard.*` APIs use natural operation names. Asyncness is
represented by the signature and metadata, not spelling:

- use `get(...) -> Promise<Result<Response, HttpError>>`, not `getAsync(...)`;
- use the language expression `await promise`, not `Promise.await(promise)` or
  `promise.await()`;
- when a blocking counterpart must coexist, make blocking explicit with a name
  such as `runBlocking`; do not add an `Async` suffix to the primary operation.

LSP completion, hover, and signature help will display an `async` classification
and the full `Promise<T>` result. Go-to-definition will open generated,
read-only library pseudo-source rather than navigating into native C++ backing.
See [Library pseudo-sources](library/README.md).

The current `System.Job.await`, `Standard.Promise.await`, and
`Standard.Process.runAsync` names remain documented only so `v0.5.x` behavior
is accurate. They are scheduled for direct migration during the `v0.7` library
milestone, after the `v0.6` language syntax is stable; they will not be
preserved through compatibility aliases.

## Transitional state model

Every job-backed promise is in exactly one state:

| Poll value | State | Meaning |
|---:|---|---|
| `0` | pending | The worker has not completed. |
| `1` | completed | `await` returns the stored `i64` result. |
| `2` | failed | `error` returns the copied failure text. |
| `3` | cancelled | Cancellation won the completion race. |

Completion, failure, and cancellation are terminal. The synchronized promise registry rejects a second completion and keeps a terminal record alive until its owning job handle is closed.

## Transitional `System.Job`

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

## Transitional `Standard.Promise`

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

In the transitional API, `poll` is non-blocking. The `await` library member is
blocking and native metadata marks it accordingly. Current workers only wait on a bounded timer or cancellation notification; they do not wait for VM execution, other promises, channels, or callbacks. Consequently, the current API cannot construct a worker-to-VM dependency cycle. Future jobs that compose processes, networking, or channels must preserve explicit cancellation and shutdown wakeups before they become public.
