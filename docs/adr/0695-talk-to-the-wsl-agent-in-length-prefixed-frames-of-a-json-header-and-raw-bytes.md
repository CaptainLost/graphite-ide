# 0695. Talk to the WSL agent in length-prefixed frames of a JSON header and raw bytes

Status: proposed
Date: 2026-11-10

## Context

Graphite drives a helper running inside a WSL distro over that process's stdin and stdout
(the WSL support design). The traffic is small requests (rename, watch), replies that carry
results, events the agent sends unasked (a watched tree changed), and file contents both ways
— a save of a multi-megabyte file among them. Both ends are C++ built from Core, and
nlohmann/json is already a dependency on both.

## Decision

Every message is one frame: a little-endian `u32` header length, the header, a little-endian
`u32` payload length, and the payload. The header is a JSON object; the payload is raw bytes
and is often empty. A reader refuses a header over 1 MiB or a payload over 256 MiB and treats
the stream as broken from then on.

The header's keys say what the message is:

- request `{"id":7,"op":"writeFile","args":{...}}`
- reply `{"id":7,"ok":true,"result":{...}}` or `{"id":7,"ok":false,"code":"NotFound","message":"..."}`
- event `{"event":"changes","body":{...}}`

Error codes travel as `Core::ErrorCode` names. Paths travel as absolute Linux paths in UTF-8.
`kAgentProtocolVersion` (1) is checked by a `hello` request first. The code is
`Core/src/Core/Remote/Frame.*` and `AgentProtocol.*`, built on both platforms.

## Rationale

A length prefix makes framing trivial over a byte stream and lets a reader take frames from
any split of reads. JSON keeps the headers readable in a log and easy to extend; a separate
raw payload keeps file contents out of JSON, avoiding base64's extra third and a copy on
every save.

## Consequences

### Positive

- Adding an operation is a new `op` name and its arguments; neither side's framing changes.
- A frame can be logged by its header alone.

### Negative

- A file over 256 MiB cannot be written through the agent in one request.
- JSON parsing costs something per message; fine for requests, worth watching for chatty
  events.

### Neutral

- The agent must write nothing but frames to stdout; its logs go to stderr.

## Alternatives Considered

- **JSON-RPC over newline-delimited JSON, file contents base64-encoded.** Lost: a third more
  bytes and an encode/decode for every save, and no way to stream raw bytes later.
- **A binary format such as protobuf.** Lost: a new dependency and a code generator for a
  protocol with a dozen messages.

## Related

- [0693](0693-cross-compile-the-linux-parts-with-a-pinned-zig-fetched-by-a-script.md)
- [0694](0694-build-the-headless-part-of-core-for-linux-from-the-lists-in-build-zig.md)
