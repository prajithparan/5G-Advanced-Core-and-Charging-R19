# Security Policy

## Project status — please read before reporting

This is a research/reference-implementation-stage 5G Core, **not** production-hardened software.
As disclosed in [`docs/DECISIONS.md`](../docs/DECISIONS.md) (ADR-0009, ADR-0049), as of today:

- Some transport paths are still lab-grade (e.g. h2c-only in early components; TLS 1.3 + mTLS is
  the target and is live on most NFs, but coverage isn't exhaustive yet — check the relevant NF's
  own ADR entries for its current state).
- There is no HA/clustering across NF instances yet.
- No independent third-party security audit has been performed.

**Do not deploy this against real subscriber data, real telecom infrastructure, or any
production/carrier network.** It is intended for local lab use, development, and research.

## Supported versions

There are no tagged releases yet — only the `main` branch is supported. Security fixes land on
`main`; there is no backport policy at this stage.

## Reporting a vulnerability

If you find a security issue (e.g. a memory-safety bug, an authentication/authorization bypass, a
cryptographic flaw in the SUCI/AKA/EAP-AKA' code paths, a TLS/mTLS misconfiguration), please report
it privately rather than opening a public issue:

- **Email**: mails@prajith.com — include steps to reproduce, affected NF(s)/file(s), and impact.

This is a solo/small-team project, not a funded security team — response times are best-effort,
but reports will be acknowledged and investigated as promptly as possible. Please allow time for a
fix before any public disclosure.

## Scope

In scope: memory-safety issues (use-after-free, buffer overflows, etc. — this project runs
ASan/UBSan/TSan in CI but bugs can still slip through), authentication/authorization logic,
cryptographic implementations (SUCI de-concealment, 5G-AKA/EAP-AKA', TLS/mTLS setup), and
injection-class bugs (SQL, command, etc.) in any NF or shared library.

Out of scope: issues that only manifest when running with the project's own disclosed lab-grade
simplifications intentionally enabled (these are already tracked as open items in
`docs/DECISIONS.md`/`docs/CAPABILITY_GAP_ANALYSIS.md`, not silently hidden) — please check those
documents first if you're unsure whether something is a known, already-disclosed gap rather than a
new finding.
