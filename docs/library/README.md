# Library pseudo-sources

The public `System.*` and `Standard.*` surfaces should be inspectable like
ordinary Simple source even when an implementation is native, generated, or
composed from several runtime layers.

This directory records the target pseudo-source presentation. These files are
design documents, not inputs accepted by the current `v0.5.12` compiler.

## Editor behavior

The `v0.6` language tooling milestone will establish stable virtual-source
infrastructure and generate one read-only view for every catalog module.
Those views may still show accurately labelled transitional `v0.5.x` library
members. The `v0.7` library milestone migrates the APIs and regenerates their
final typed pseudo-sources. Completion, hover, signature help, generated
reference documentation, and pseudo-source must consume the same declaration
metadata.

Go-to-definition on a library module or member should open a stable virtual URI:

```text
simple-library://System.Job
simple-library://Standard.Process
simple-library://Standard.HTTP
```

A pseudo-source contains:

- the canonical module declaration;
- public types and function signatures;
- documentation comments;
- async classification and the complete `Promise<T>` type;
- availability, capability, blocking, allocation, ownership, and platform notes;
- a source-wrapper location when real Simple source exists;
- a native/runtime backing label when no Simple body exists.

It must not invent executable bodies or expose C++ implementation paths as the
public definition. Generated locations must remain stable across sessions so
LSP clients can cache and reopen them.

## Async presentation

Async operations keep natural names. Their signatures show `Promise<T>`, and
LSP presentation adds an `async` classification without changing the member
name. Pseudo-sources do not create `getAsync`, `runAsync`, or `.await` members.
Waiting is expressed by the language keyword:

```simple
response :: Response = await Standard.HTTP.get(url)?
```

If blocking and async forms coexist, only the blocking form receives an explicit
suffix such as `Blocking`.

## Initial design views

- [`System.Job`](System.Job.md)
- [`Standard.Promise`](Standard.Promise.md)
- [`Standard.Process`](Standard.Process.md)
- [`Standard.HTTP`](Standard.HTTP.md)

The generated implementation should eventually replace these hand-maintained
design views for every library module.
