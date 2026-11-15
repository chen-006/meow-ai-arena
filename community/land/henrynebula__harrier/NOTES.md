# Harrier

A from-scratch territory bot for Episode 1 (land). Single-file C++20,
standard library only.

## Approach

**Farming.** The core of the game is throughput: cells banked per turn. A
rectangle loop of sides a x b costs 2a + 2b turns for up to a*b fresh cells,
so big squares dominate when they survive. Every turn the planner:

1. Enumerates rectangle-loop candidates in all legal opening directions,
   including anchors slid through owned land along the opening side. Each
   candidate gets an O(1) upper bound on its capture from a prefix-sum grid
   of not-owned cells.
2. Sorts candidates by bound score (bound / estimated time) and evaluates
   them best-first inside a ~12 ms soft budget. Evaluation replays the exact
   engine capture: walls = territory + would-be trail, flood fill from the
   board border, protected zones excluded.

**Safety.** Possibility-based vetoes ("an opponent *could* beeline to this
cell before it closes") starve the bot against passive opponents - measured
as ~750 average area and 4:36 against the greedy anchor. Harrier instead:

- Prices risk as a per-opponent hazard `K / detour`, where the detour is how
  far beyond the acute window an opponent would have to travel to convert a
  cut. This biases loop orientation and size away from opponents without
  banning big loops outright.
- Commits to plans and reacts mid-loop: if a *pursuing* opponent can reach a
  laid trail cell before the loop closes, the bot bails for home when that
  still banks the trail sliver (any re-entry captures the whole trail), and
  rides the loop out otherwise.
- Detects pursuit structurally: sustained closing movement toward our
  nearest trail cell, committed outside the opponent's own land, within
  range. While hunted, loops are capped and the bail trigger tightens.

**Melee.** In 4-player games opponents convert "possible" cuts often enough
that a hard filter pays: loops are capped at 40 turns and rejected when any
opponent could arrive before the loop closes by a small margin.

**Tactics.** Opportunistic cuts of adjacent enemy trails, dives at exposed
opponents mid-loop (weighted by their trail length), and counter-raids into
undefended home land while opponents are extended.

## Results (local challenge.py, ~380 games)

- Duel Elo +174 over the greedy anchor (52.1% win rate, avg area 591).
- Uncontested throughput ~1480 average area vs the random anchor.
- Overall 6th of 12 on the local ladder (Bradley-Terry), clear of the
  greedy-anchor tier; melee deaths cut from 7.5 to 2.0 per game by the
  arrival-window filter.

Weak spot: melee against the strongest officials, who both farm and hunt;
Harrier trades too much area for survival there.
