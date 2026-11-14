#!/usr/bin/env bash
# Foreman verify gate for meow-ai-arena community submissions.
#
# Finds community/{land,bomberman,ctf}/henrynebula__*/ entries added on this
# branch, enforces the submission scope (the diff touches only those folders),
# runs the submission-channel regression tests, then runs the official
# community checker (structure + compile + evaluation) on each entry.
set -euo pipefail
cd "$(dirname "$0")"

EPISODES="land bomberman ctf"

# 1. At least one submission folder must exist.
entries=()
for ep in $EPISODES; do
  for d in "community/$ep"/henrynebula__*/; do
    [ -d "$d" ] && entries+=("${d%/}")
  done
done
if [ "${#entries[@]}" -eq 0 ]; then
  echo "FAIL: no community/<episode>/henrynebula__* submission folder found" >&2
  exit 1
fi

# 2. Scope: the branch diff may only touch the submission folder(s).
git fetch origin --quiet
merge_base=$(git merge-base HEAD origin/main)
bad=$(git diff --name-only "$merge_base" HEAD \
      | grep -v -E '^community/(land|bomberman|ctf)/henrynebula__[a-z0-9]+(-[a-z0-9]+)*/' \
      || true)
if [ -n "$bad" ]; then
  echo "FAIL: diff contains paths outside the submission folder(s):" >&2
  printf '%s\n' "$bad" >&2
  exit 1
fi

# 3. CI parity: community submission-channel regression tests.
python3 -m unittest discover -s community/tests -q

# 4. Official checker per submission (structure, compile, evaluation).
for e in "${entries[@]}"; do
  echo "== community/check.py $e =="
  python3 community/check.py "$e"
done

echo "VERIFY OK: ${entries[*]}"
