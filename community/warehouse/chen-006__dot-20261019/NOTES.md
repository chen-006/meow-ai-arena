# dot Warehouse v82 20261019

## Provenance and scope

Derived from the MIT-licensed Opus 5.5 round-3 complete rewrite in warehouse/submissions/round3/deepseek-v4.1-flash__opus-5.5. Further GPT-6 Astra research adapts it to the original community contract and optimizes exact execution. The six-PARAMS parser, fixed priority, unit carrying cost and original report format are intentional. This is community v82; it is not the CHANGE/round-3-contract r57 variant. Inherited round-3 comments refer to its ancestry, not the deployed input/output contract. The inherited Windows-only binary-I/O branch uses <fcntl.h>/<io.h> and _setmode; it is inactive in the Linux community build.

Optimizations include bounded distance pools, bounded output, cache repair guards, static feasibility bounds and bounded query reuse. Robot/order ordering and executable tie semantics are preserved. Finite differential tests do not prove correctness on every legal input. Workload-dependent regressions and reference runs that could not finish in the research budget remain disclosed; no universal speedup is claimed.

This entry is the frozen primary from an iterative 16-hour research campaign completed on 19 October 2026. It is a community submission, not an original time-limited show contestant. No SS rating or dominance claim is made. The deployed C++ sources are byte-identical to that freeze; this folder adds only submission metadata, attribution and license packaging.

Upstream repository: https://github.com/chen-006/meow-ai-arena

Upstream snapshot: `455052f952555ae975e14dd3f0132e88027239e8`

## Source identity

- `src/main.cpp`: `9c6bf41133bc8639692a11d5f43810da63db09dc01c112fbf74afd838bdf2954`
- `src/common.h`: `08feb47638565ab7217c9b7c3dfd9547d2e6ac13d12f954ff06be8d93ce531f0`
- `src/sim.cpp`: `f01eac9505ebc998cc494f6fc91206f71775468fa62f318fd357b59bace8b783`

## Reproduce

Run the repository's official community check from its root:

`python3 community/check.py community/warehouse/chen-006__dot-20261019`

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
