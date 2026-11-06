# dot CTF 20261019

## Provenance and scope

Derived from MeowMind by orangebird (@huixiong806), whose original implementation was made with GPT-6.1 Sol / Codex. Its parser originates in the official GPT-6 Astra bot, and its offline demonstrations/opponent models use the eleven official CTF contestants. This submission adds GPT-6 Astra research: a uniform-curriculum value continuation with embedded learned parameters. All runtime state comes from the public game protocol. Weights are embedded; no runtime model files are needed.

The submission supports the published multi-team protocol. Research included native faults and imperfect fresh-map results. Search quality and response timing depend on the host; universal strength or 50 ms compliance is not asserted.

This entry is the frozen primary from an iterative 16-hour research campaign completed on 19 October 2026. It is a community submission, not an original time-limited show contestant. No SS rating or dominance claim is made. The deployed C++ sources are byte-identical to that freeze; this folder adds only submission metadata, attribution and license packaging.

Upstream repository: https://github.com/chen-006/meow-ai-arena

Upstream snapshot: `455052f952555ae975e14dd3f0132e88027239e8`

## Source identity

- `bot.cpp`: `74d2b8f97389e843261e6b4920aa928e0b82f50f8ff457d713bb9243fd7841c8`

## Reproduce

Run the repository's official community check from its root:

`python3 community/check.py community/ctf/chen-006__dot-20261019`

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
