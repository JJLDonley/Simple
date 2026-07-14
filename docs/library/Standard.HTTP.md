# `Standard.HTTP` pseudo-source

> Target `v0.7` declaration view for planning and LSP design. HTTP is not an
> implemented current library surface.

```simple
module Standard.HTTP

TextError :: enum {
  InvalidEncoding
}

Header :: artifact {
  name :: string
  value :: string
}

HttpError :: enum {
  InvalidUrl,
  ResolveFailed,
  ConnectFailed,
  Timeout,
  ProtocolError
}

Response :: artifact {
  status :: i32
  headers :: Header[]
  body :: i32[]

  bodyText :: Result<string, TextError> ()
}

/// Async HTTP GET. Requires the network-client capability.
get :: Promise<Result<Response, HttpError>> (url : string)

/// Async HTTP POST. Requires the network-client capability.
post :: Promise<Result<Response, HttpError>> (
  url : string,
  body : i32[]
)
```

The public members are `get` and `post`, not `getAsync` and `postAsync`.
Calling source uses `await Standard.HTTP.get(url)?`. A future blocking surface,
if justified, must use explicit names such as `getBlocking`.
