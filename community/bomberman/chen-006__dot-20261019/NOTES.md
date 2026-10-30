# dot Bomberman 20261019

## Provenance and scope

Derived from the official GPT-6 Astra bot, with further GPT-6 Astra research. Public initial player count selects a corrected duel policy for two players and the inherited Astra policy for multiplayer. Both branches include the same support-preserving mixed-sampler repair. This is one fixed source; it does not identify opponents or use evaluation seeds. The implementation uses g++ integer/bit-operation extensions (__uint128_t and __builtin operations), including inherited Astra code; no external libraries are required. The supported evaluation target is the repository's Linux g++ C++20 build, rather than a claim of strict ISO-only portability.

The exact small-board search specializes the native default bomb parameters. The inherited multiplayer policy is retained for the published 2–10-player protocol. Long-horizon stalls, map sensitivity, ten-player uncertainty and native timeout/fault observations remain limitations. The sampler repair has finite-domain correctness tests; those tests alone do not establish a strength improvement.

This entry is the frozen primary from an iterative 16-hour research campaign completed on 19 October 2026. It is a community submission, not an original time-limited show contestant. No SS rating or dominance claim is made. The deployed C++ sources are byte-identical to that freeze; this folder adds only submission metadata, attribution and license packaging.

Upstream repository: https://github.com/chen-006/meow-ai-arena

Upstream snapshot: `455052f952555ae975e14dd3f0132e88027239e8`

## Source identity

- `bot.cpp`: `ed584e6767f40df2005e3c8d42768d9729f54656ee05877d2ab1a9ef97e094ba`

## Reproduce

Run the repository's official community check from its root:

`python3 community/check.py community/bomberman/chen-006__dot-20261019`

`--skip-eval` checks structure and compilation only. The default check runs the official quick community evaluation. Local results are reported in the pull request; GitHub evaluation remains separate. Published scores use the community evaluation's own definitions and are not interchangeable with custom research panels.

## License and upstream notice

MIT License

Copyright (c) 2026 meow-ai-arena contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
