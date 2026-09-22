# Issue tracker

[GitHub Issues for linvelive/frost-ecy-stat](https://github.com/linvelive/frost-ecy-stat/issues)
is this repository's work tracker. Use `gh` from this checkout, or pass
`--repo linvelive/frost-ecy-stat` explicitly.

When a skill says "spec", "PRD", or "ticket", use a GitHub issue describing
the intended result and observable acceptance. "Publish to the tracker" means
`gh issue create --title "..." --body-file /path/to/body.md`.
Read with `gh issue view <number> --comments`; list with `gh issue list`.

Issue identity, open/closed state and assignees live in GitHub. Use native
blocking relationships and sub-issues for dependent work when needed. Keep the
current findings, remaining acceptance and next action in the issue body;
record the resolved outcome and evidence in a resolution comment. Hardware
acceptance remains open when only host or build checks have passed.

A PR completing all acceptance uses `Fixes #<number>`. A partial delivery uses
`Refs #<number>` and leaves the issue open. Project Frost's private experiment
history is not a second tracker for this public library; published evidence
and limitations belong in [../validation.md](../validation.md).
