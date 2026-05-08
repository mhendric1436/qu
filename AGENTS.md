# Agent Guidelines

## Project Shape

This repository is a small C++20 transactional queue library built on the sibling `mt`
library.

- `include/qu/queue.hpp` contains the public queue API.
- `src/queue.cpp` contains the queue implementation.
- `src/tables/schemas/` contains private `mt_codegen.py` schema metadata.
- `src/tables/generated/` contains generated row and mapping headers. Do not edit these by
  hand; update the matching schema and run codegen.
- `tests/queue_tests.cpp` contains Catch2 unit tests.
- `third_party/catch2/` contains the vendored Catch2 amalgamated files.
- `docs/` contains PlantUML architecture diagrams. Generated PNGs are ignored.
- `images/` contains README imagery.

The non-vendored dependency is `mt`, expected by the Makefile at `$(HOME)/repos/mt`.

## Build And Test

Use the existing Makefile targets:

```sh
make test
```

Useful targeted commands:

```sh
make build
make codegen
make codegen-check
make format
make format-check
make docs-png
make clean
make clean-docs
```

Run `make codegen-check` after changing files in `src/tables/schemas/`.
Run `make docs-png` after changing `docs/*.puml`.

## Working Rules

- Do not make code changes when the request is analysis-only.
- Prefer small, focused changes that preserve the dependency-minimal design.
- Preserve C++20 compatibility.
- Do not introduce external runtime dependencies unless explicitly requested.
- Use `rg` or `rg --files` for search.
- Run `make test` after code changes when feasible.
- Run `make codegen-check` after schema or generated-header changes.
- Keep Catch2 as the only vendored third-party code unless the user explicitly asks otherwise.
- Do not edit generated diagram PNGs directly; update `docs/*.puml` and regenerate with
  `make docs-png`.
- Include a suggested git commit message after every code or documentation change.

## Style

- Follow `.clang-format`; the Makefile runs `clang-format` over C++ files during builds.
- Public API belongs under `include/qu/`; implementation belongs under `src/`.
- Use the existing `qu` namespace for public queue types and `qu::tables` for generated rows.
- Prefer value-oriented structs and straightforward helper functions over new abstractions unless
  the change clearly reduces complexity.
- Keep public API terminology general-purpose. Use `consumer`, not `worker`, for claim ownership.
- Keep backend-specific details behind `mt::Database`; `qu::Queue` should remain backend-neutral.

## Queue Semantics

- Messages are scoped by `(namespace_name, channel_name, id)`.
- Duplicate message protection is scoped to that namespace/channel/id tuple.
- Existing convenience APIs without namespace/channel use the default namespace and channel.
- Every operation that opens its own transaction should have a matching `mt::Transaction&`
  overload so callers can compose `qu` with `wf` or other `mt` users atomically.
- A message lifecycle is `pending -> claimed -> processed`.
- `fail` returns a claimed message to `pending`.
- `reap_expired` returns expired claimed messages to `pending`.
- Claim ownership is stored as `consumerId` in generated rows and surfaced as `consumer_id`.
- FIFO ordering within a namespace/channel is based on a database-backed monotonically increasing
  `sequence`, not wall-clock timestamps.
- Sequence counters are stored in `queue_channel_counters`; gaps are acceptable when transaction
  conflicts or aborted attempts consume counter values.

## Generated Tables

Schema files currently include:

- `src/tables/schemas/queue_message.mt.json`
- `src/tables/schemas/queue_channel_counter.mt.json`

Generated headers currently include:

- `src/tables/generated/queue_message_row.hpp`
- `src/tables/generated/queue_channel_counter_row.hpp`

When adding or changing schemas:

1. Update the schema JSON.
2. Run `make codegen`.
3. Run `make codegen-check`.
4. Commit both the schema and generated header changes.

This repo is pre-alpha and not deployed; do not bump schema versions automatically unless the user
asks for versioning or migration behavior.

## Test Guidance

- Add focused Catch2 tests near the behavior being changed.
- For public API changes, cover both convenience methods and `mt::Transaction&` overloads when
  behavior differs or composition is at risk.
- For queue ordering, test behavior with ids and timestamps that would expose accidental key or
  clock ordering.
- For namespace/channel behavior, test isolation across namespaces and channels.
- For claim, ack, fail, and reap behavior, verify both status transitions and relevant metadata.

## Documentation

- Keep README examples aligned with the public API.
- Keep `docs/architecture.puml` aligned with storage and transaction architecture.
- `docs/*.png` are generated artifacts and ignored by git; regenerate locally when useful, but do
  not hand-edit them.
