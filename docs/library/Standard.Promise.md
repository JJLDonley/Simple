# `Standard.Promise` pseudo-source

> Target declaration view for planning and LSP design. It is not current
> executable Simple source.

```simple
module Standard.Promise

/// Returns state without suspending.
poll<T> :: PromiseState<T> (promise :: Promise<T>)

isPending<T> :: bool (promise :: Promise<T>)
isCompleted<T> :: bool (promise :: Promise<T>)
isCancelled<T> :: bool (promise :: Promise<T>)

/// Requests structured cancellation when the producer supports it.
cancel<T> :: bool (promise :: Promise<T>)
```

There is deliberately no `await` function or method. `await` is a language
expression that unwraps `Promise<T>` only inside an `async` function. Producers,
such as `Standard.HTTP.get`, return typed promises directly. Managed promises
do not expose `close`; the VM owns pending state and releases terminal state
when it is no longer reachable.
