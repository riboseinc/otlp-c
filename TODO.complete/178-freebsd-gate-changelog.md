# TODO 178 — FreeBSD gating; the changelog page

**Status:** Complete (v1.1.3)
**Priority:** P2 (CI truth, site completeness)

## FreeBSD: best-effort no more

The job carried `continue-on-error: true` since inception,
annotated "INADDR_LOOPBACK visibility quirk; revisit". Revisited:
the jobs API showed 15/15 green — a false reading. Under
`continue-on-error` a step's `conclusion` reads "success" even
when the step failed; the API has no `outcome` field. The flag
was removed on faith and the FIRST ungated run failed for real:
three portability bugs (Apple-only Availability.h pulled in on
FreeBSD; INADDR_LOOPBACK hidden by FreeBSD headers under the
globally-defined `_POSIX_C_SOURCE`; a bare `memmem` call with no
declaration). Fixed by `tests/test_portable.h` — one always-local
byte-search + a spelled-out loopback constant, one deterministic
code path everywhere.

**Lesson paid for:** never judge a `continue-on-error` job by
the jobs API's step conclusions — read the raw logs. The mask is
indistinguishable from green in the API surface.

## Site: /docs/changelog/

The release arc (freeze, exemplars, OTel-native config,
deepening window, the two caught wire bugs) rendered as the
docs sidebar's newest page, linking to the authoritative
CHANGELOG.md. 14 pages.

## Docs truth

CLAUDE.md key-files table gains exporter_sync.c and
exporter_internal.h; test count 52 → 53.
