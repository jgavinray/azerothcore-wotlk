# Recovery Procedures

Each section is keyed by a token that may appear in staging logs. When an integration check fails,
feed the matching section to the implementation model with the failing log line.

The model must not invent a recovery outside this file.

## Token: PARALLEL_VERIFY_FAIL path=<...>

Cause: worker path compute diverged from the old synchronous map-thread path.

Steps:

1. Set `AiPlayerbot.AsyncPathComputePercent = 0` in the staging config to stop the canary.
2. Append the failing diagnostic line to
   `docs/playerbot-parallelization/known-path-divergences.md`.
3. Do not attempt to fix path computation from the recovery step.
4. Stop and report the divergence.

Allow-list for a recovery commit:

- `docs/playerbot-parallelization/known-path-divergences.md`;
- config files only if this repository stores the staging config being edited.

## Token: WARNING: ThreadSanitizer

Cause: ThreadSanitizer flagged a race during compute offload.

Steps:

1. Set `AiPlayerbot.AsyncPathComputePercent = 0` in the staging config.
2. Append the first TSan warning block to
   `docs/playerbot-parallelization/known-tsan-findings.md`.
3. Do not add suppressions as an automatic recovery.
4. Stop and report the finding.

Allow-list for a recovery commit:

- `docs/playerbot-parallelization/known-tsan-findings.md`;
- config files only if this repository stores the staging config being edited.

## Token: PARALLEL_VERIFY_FAIL value=<...>

Generic parallel value compute is not part of the approved current plan.

If this token appears, it came from stale code or stale instructions. Stop and report that value
parallelism must remain disabled.

## Anything Else

If the failing token does not match a section above, stop and report. Do not invent a fix.
