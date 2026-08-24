# TODO 160 — License change: Apache-2.0 → BSD 3-Clause

**Status:** Complete (v0.6.14)
**Priority:** maintainer decision (explicitly requested)

## What shipped

Per maintainer instruction, the project license changes from
Apache-2.0 to BSD 3-Clause:

- LICENSE rewritten (holder: Ribose, Inc. — flag for correction
  if the legal entity differs)
- all 135 SPDX headers (this pass also fixed a pre-existing
  `SPDX-License-Identifier-Identifier` typo in seven files)
- both vcpkg manifests (root + ports/otlp-c)
- README (badge, license section, maintainer note), CONTRIBUTING,
  and CLAUDE.md's non-negotiable #3

## Consequences, for the record

- CNCF projects must be Apache-2.0 — the documented
  donation path (README, CLAUDE.md) is closed under BSD-3-Clause
  unless relicensed back with contributor consent. Stated in
  CLAUDE.md and README.
- Releases up to and including v0.6.13 were published under
  Apache-2.0 and remain so; this change applies going forward.
- If the repo has external contributors, a unilateral relicense
  requires their consent — maintainer's responsibility to verify.

## Verification

51/51 tests; Doxygen zero warnings; a repo-wide sweep shows the
only remaining "Apache" mentions are (a) upstream
opentelemetry-proto's own license (fact, unchanged), and (b)
historical records (CHANGELOG, TODO.complete, frozen
release-notes).
