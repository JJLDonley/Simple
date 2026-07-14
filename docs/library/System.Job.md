# `System.Job` pseudo-source

> Target declaration view for planning and LSP design. It is not current
> executable Simple source.

```simple
module System.Job

JobError :: enum {
  StartFailed
}

JobSpec<T> :: artifact {
  // Runtime-owned description of copied host work; never a raw VM closure.
}

PromiseState<T> :: artifact {
  // Pending, Completed(T), or Cancelled.
}

/// Starts low-level host work. Native backing; may allocate and suspend.
spawn<T> :: Promise<Result<T, JobError>> (work : JobSpec<T>)

/// Inspects state without suspending.
poll<T> :: PromiseState<T> (promise :: Promise<T>)

/// Requests structured cancellation of this managed Promise.
cancel<T> :: bool (promise :: Promise<T>)
```

There is deliberately no `await` member. Source uses prefix
`await promise` inside an `async` function. Exact low-level job construction
remains subject to closure/rooting and cancellation design.
