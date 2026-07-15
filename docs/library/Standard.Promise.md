# `Standard.Promise` pseudo-source

> Target declaration view for planning and LSP design. It is not current
> executable Simple source.

```simple
module Standard.Promise

/// Returns state without suspending.
poll<T> :: (promise :: Promise<T>) -> PromiseState<T>

isPending<T> :: (promise :: Promise<T>) -> bool
isCompleted<T> :: (promise :: Promise<T>) -> bool
isCancelled<T> :: (promise :: Promise<T>) -> bool

/// Requests structured cancellation when the producer supports it.
cancel<T> :: (promise :: Promise<T>) -> bool
```

There is deliberately no `await` function or method. `await` is a language
expression that unwraps `Promise<T>` only inside an `async` function. Producers,
such as `Standard.HTTP.get`, return typed promises directly. Managed promises
do not expose `close`; the VM owns pending state and releases terminal state
when it is no longer reachable.
