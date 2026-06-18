# Phase 3 — Deferred (do not implement)

Do not write any code for this phase. It exists only as a placeholder for if, after Phase 2 is
measured under load, the **serial apply phase** (the mutations that must stay on the map thread —
casting, damage, aura/threat application, movement commits) is still the ceiling that limits adding
more bots.

Only when Phase 2's measurements prove that, a detailed Phase 3 sub-spec must be generated (paste the
README regen prompt plus the Phase 2 results to a planning model) before any execution.
Likely directions (NOT yet specified):
- batching similar mutations to cut per-action overhead;
- extending the decide/apply split into the action layer so actions emit "intents" during the
  parallel phase and a thin serial phase applies them.

Until that sub-spec file exists in this folder, the executor must not start any Phase 3 work.
