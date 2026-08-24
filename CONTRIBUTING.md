# Contributing to otlp-c

PRs welcome. Before you start, read:

- [CLAUDE.md](CLAUDE.md) — conventions and invariants.
- [docs/roadmap.md](docs/roadmap.md) — phase-by-phase plan.
- [docs/otlp-spec.md](docs/otlp-spec.md) — the protocol reference.

## Workflow

1. **Fork** the repo on GitHub.
2. **Branch** from `main`: `git checkout -b my-feature`.
3. **Commit** with a clear message. Format: `<scope>: <imperative
   summary>` then a blank line then a body that explains the *why*.
4. **Push** to your fork.
5. **Open a PR** against `riboseinc/otlp-c:main`. The CI matrix runs
   on every PR; wait for green.
6. **Rebase-merge** when CI is green.

## What gets merged

- Bug fixes — always welcome.
- Phase implementations (see docs/roadmap.md) — welcome; open a
  draft PR early.
- Property tests — required for new code paths. See
  [docs/architecture.md](docs/architecture.md#testing-strategy).
- Documentation improvements — always welcome.

## What probably won't get merged

- Cosmetic refactors with no behavior change.
- "Spring cleaning" PRs that touch many files.
- Features without a documented use case.
- Vendored third-party code (we're zero-non-libc-deps by design).
- Anything that introduces a C++ dependency.

## Code style

`.clang-format` is Mozilla-based. Run `clang-format -i` on changed
files before committing.

## Signoff

By contributing, you agree that your contributions are licensed
under the BSD 3-Clause license (see [LICENSE](LICENSE)). For
eventual CNCF donation, we may require DCO signoff (`git commit -s`)
in the future; not yet required.

## Code of conduct

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Be respectful, be
technical, be patient. Disagreements about architecture are fine;
personal attacks are not.

Report conduct issues by emailing `opensource@ribose.com`.
