# Start Here: Playerbot Compute Offload

This is the first file to read in a fresh session.

The executor must not infer architecture. The executor must follow the files and tasks in the order
below. If a required fact cannot be revalidated from source, stop and report the missing fact.

## Absolute Rules

- Do not implement production source code unless the user explicitly asks for implementation.
- Follow `execution-contract.md` for go/stop status, reading order, and forbidden worker access.
- Do not proceed past any blocked gate.

## Required Reading Order

Read `execution-contract.md` completely, then follow its required reading order.

## Required Source Revalidation

Before changing or proposing anything, revalidate the anchors in `source-anchors.md`.

Required command pattern:

- use `rg` to locate the symbol;
- inspect the current function or symbol body after `rg` reports the location;
- record whether each anchor still matches the documented fact.

If the fact is no longer true, stop. Do not adapt the architecture silently.

## Required Go/Stop Output

Before implementation begins, produce or update a go/stop note using the statuses defined in
`execution-contract.md`.

## Blocked Means Blocked

If a task needs a forbidden object, forbidden singleton, unproven thread-safe read, broader refactor,
or behavioral reorder, stop the task. Do not work around the block by widening scope.
