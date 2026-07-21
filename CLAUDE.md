# Working notes for Claude on this repo

## Daily-project PRs: auto-merge

As of 2026-07-21, the repo owner asked for every PR opened by the daily-project
routine to be merged automatically once it's up — no need to pause and ask first.
This applies going forward to future runs, not just the PR that prompted it.

Before merging, still confirm the basics that would make a PR *not* mergeable as-is:
the project actually builds, its test suite (if any) passes, and there are no
unresolved review comments or failing CI checks. If any of those fail, fix them or
flag the problem instead of merging broken work.
