# ADR: Message history durability and bounded reads

## Status

Accepted.

## Context

`MessageHistory` stores chat history as append-only text files. The systems capstone reinforced two
constraints that matter here:

1. writes should have a clear durability boundary when history is user-visible state;
2. reads with a limit should not load an entire growing history file into memory.

## Current decision

- Append writes use a POSIX `open`/`write` loop and can opt into `fsync` via
  `CHERRY_HISTORY_DURABLE_WRITES=1`.
- Limited reads use a bounded `std::deque` tail buffer so `load_*_history(limit)` keeps only the last
  `limit` records in memory.
- `limit == 0` preserves the existing behavior of returning the full file.

## Why not a full journal database here

The capstone implements a checksum journal because job state has multiple transitions and recovery
semantics. Chat history is simpler: append-only text records are enough for the current product. A
full journal/snapshot/index layer would add complexity without a measured product need.

## Trade-off

The history file remains a plain text append log, so there is no checksum-based corrupt-tail recovery
yet. The product-level improvement is narrower and defensible: optional durable appends and bounded
tail reads for limited history queries.
