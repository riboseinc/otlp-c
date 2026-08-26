# TODO 180 — the adoption path, kept true (v1.1.5)

**Status:** Complete (v1.1.5)
**Priority:** P3 (documentation truth)

## What was actually wrong

- phase-21 (vcpkg registry) carried one unticked in-repo
  criterion: "Quickstart shows the vcpkg path end-to-end."
  The quickstart's overlay mention was a "(see README)"
  hand-off, not a path.
- README and quickstart both pinned FetchContent examples to
  v1.0.5 as "latest" — four releases stale, and it rots again
  every release.
- The site /docs/changelog/ page (shipped v1.1.3) had no 1.1.4
  entry — the page itself became drift within one release.

## The fix

Quickstart gains a self-contained overlay-port subsection (the
exact recipe tests/consumers/vcpkg_overlay pins in CI, plus the
"delete one line when the registry accepts us" note). Tag
examples bumped to v1.1.4. Site changelog carries 1.1.4 + 1.1.5.
phase-21's in-repo boxes ticked; the three external criteria
(the microsoft/vcpkg PR and its CI) are annotated user-owned.

## Lesson

Every "latest" literal in docs is a future lie. Bump sites:
README FetchContent, quickstart FetchContent — and add the site
changelog entry IN the release that ships, not after.
