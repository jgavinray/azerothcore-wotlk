# Static Callgraph

Decision: PROCEED for measurement and infrastructure only. Playerbot loop remains map-thread-only.

## Source Path

Observed call path:

1. `Map::Update`
   - iterates players in map update order;
   - calls each player's update path.
2. `PlayerbotsPlayerScript::OnAfterUpdate`
   - called after the player update hook;
   - calls `PlayerbotAI::UpdateAI(diff)` for bot players;
   - calls `PlayerbotMgr::UpdateAI(diff)` for master/playerbot managers.
3. `PlayerbotAI::UpdateAI`
   - decrements AI delay;
   - validates bot/session/world state;
   - performs cheat upkeep and activity checks;
   - handles current spell interruption;
   - handles transport checks;
   - validates `CanUpdateAI`;
   - calls `UpdateAIInternal`;
   - updates next AI delay.
4. `PlayerbotAI::UpdateAIInternal`
   - starts performance monitor scope;
   - processes chat replies;
   - handles commands;
   - handles logout decisions;
   - processes bot/master packet queues;
   - calls `DoNextAction`.
5. `Engine::DoNextAction`
   - processes triggers;
   - pushes default actions;
   - pops action queue;
   - initializes actions;
   - checks usefulness;
   - applies multipliers;
   - evaluates possibility;
   - pushes prerequisites;
   - executes the selected action;
   - pushes continuers or alternatives based on execution result;
   - removes expired queue entries.

## Classification

Map-thread-only:

- `Map::Update` player iteration;
- `PlayerbotsPlayerScript::OnAfterUpdate`;
- `PlayerbotAI::UpdateAI`;
- `PlayerbotAI::UpdateAIInternal`;
- `Engine::DoNextAction`;
- trigger processing;
- action initialization;
- action usefulness/possibility checks;
- action execution;
- action queue mutation;
- playerbot packet handling;
- movement commits.

Worker-forbidden:

- anything that can call `Action::Execute`;
- anything that can mutate action queue state;
- anything that reads or writes live `Player`, `Unit`, `WorldObject`, `Map`, packet/session, DB,
  group, inventory, aura, threat, spell, or movement state.

Approved worker boundary:

- none inside the bot loop itself;
- only external compute jobs using copied inputs and returning advisory outputs.

## Architectural Decision

Do not parallelize the bot loop. Do not add a map-start hook for decision work. Do not reorder
playerbot updates. Any worker compute must be submitted from the current map-thread path and must
not affect the same tick unless a later task explicitly proves that behavior is equivalent.
