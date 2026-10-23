# dot Tank 20261019

## Provenance and scope

Derived from the official GPT-6 Astra second-half bot, `tank/bots/half2/gpt-6-astra.cpp`, in meow-ai-arena. This submission is the frozen `oracle_cache_v31` primary from subsequent GPT-6 Astra research. It adds joint-response coverage, behavior-preserving root-value memoization and incremental response-matrix reuse, retaining a legal anytime fallback. Runtime inputs come from the public `tank::State`; the bot needs only the official `tank.hpp` and standard C++ headers.

The source is byte-identical to the frozen primary from an iterative 16-hour research campaign completed on 19 October 2026. This is a community submission rather than an original time-limited show contestant. The folder adds only metadata, attribution and license packaging.

Research results did not establish universal dominance, an SS rating, or reliable 50 ms compliance. Prior native tests included over-budget turns and fault forfeits; those outcomes were retained. Fixed-work exploratory tests are not native timing certificates and are not interchangeable with the community score. This entry uses the original CPU budget and penalty rules without modification.

Upstream repository: https://github.com/chen-006/meow-ai-arena

Upstream source snapshot: `455052f952555ae975e14dd3f0132e88027239e8`

## Source identity

- `bot.cpp`: `0ff08776522a0d97b2a30293e43c999f5b1b4e232544560c61f5b0257232f1f2`

## Reproduce

After Tank community support is on the target branch, run from the repository root:

`python3 community/check.py community/tank/chen-006__dot-20261019`

`--skip-eval` checks structure and native compilation only. The default check plays 320 formal-mode games against 16 opponents; `--full` plays 1280. Each map is played from both seats. The native budget is 2000 CPU ms for startup, init and the first decide, then 50 CPU ms per turn. The fixed official final-tournament opponent ratings are used only as references when fitting this entry's rating. Local results and GitHub evaluation are separate runs; estimated reference placement is not an official tournament result.

## Local community evaluation

The official quick community check completed on 19 October 2026: 320 formal-mode games, 256 wins / 1 draws / 63 losses, fixed-reference rating +426.9, estimated reference placement 3 / 15. This local run took 134 seconds on Linux with GCC 14.2.0.

The entrant had 168 over-budget turns, 0 illegal-output turns and 14 fault forfeits. These penalties remain in the recorded results. Passing the check means the evaluation completed and produced a score; it does not certify clean timing, superiority over every opponent, or a formal-tournament placing. GitHub CI is a separate run and may produce different results.

## License and upstream notice

The source's original license comment is preserved. The upstream MIT notice is reproduced below.

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
