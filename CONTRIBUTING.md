# Contributing

`qu` is pre-alpha. The API, schemas, and storage layout may change quickly while the core
queue model settles.

## Prerequisites

- C++20 compiler, currently `clang++` by default
- `make`
- `python3`
- `clang-format`
- sibling `mt` checkout at `$(HOME)/repos/mt`
- optional: `plantuml` for diagram generation

The repository vendors only Catch2 under `third_party/catch2/`.

## Build And Test

Run the full test target before submitting code changes:

```sh
make test
```

Useful targets:

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

`make test` formats C++ sources as part of the build. Use `make format-check` when you want
to check formatting without changing files.

## Generated Tables

Schemas live in `src/tables/schemas/`. Generated headers live in `src/tables/generated/`.

Do not edit generated headers directly. To change table shape:

1. Edit the matching `*.mt.json` schema.
2. Run `make codegen`.
3. Run `make codegen-check`.
4. Commit both the schema and generated header changes.

Because `qu` is not deployed, do not bump schema versions automatically. Bump versions only
when a change intentionally needs versioned schema behavior.

## Queue Semantics

Please preserve these behaviors unless the contribution explicitly changes them:

- Messages are scoped by namespace, channel, and message id.
- Duplicate protection is scoped to namespace/channel/id.
- Message lifecycle is `pending -> claimed -> processed`.
- `fail` and expired-claim reaping return claimed messages to `pending`.
- Claim ownership uses general `consumer` terminology.
- FIFO ordering within a namespace/channel uses the database-backed `sequence`, not
  timestamps or message ids.
- Public convenience APIs should have matching `mt::Transaction&` overloads where callers
  may need atomic composition with other `mt` users.

## Tests

Add focused Catch2 tests in `tests/queue_tests.cpp` for behavior changes.

Good tests should cover observable queue behavior, especially:

- enqueue duplicate handling
- claim ordering and conflicts
- namespace/channel isolation
- ack/fail/reap state transitions
- caller-owned transaction overloads
- generated schema behavior when table shape changes

## Documentation

Keep `README.md` examples aligned with the public API.

Architecture diagrams live in `docs/*.puml`. Generated PNGs are ignored by git. To update
diagrams:

```sh
make docs-png
```

Do not hand-edit generated PNGs.

## Pull Request Expectations

Before opening a PR or handing off a change, run:

```sh
make codegen-check
make test
```

Also run `make docs-png` when PlantUML sources changed.

Keep changes narrowly scoped. Avoid unrelated refactors, dependency additions, or metadata
churn.
