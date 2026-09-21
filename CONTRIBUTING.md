# Contributing

Thank you for helping improve Open Media Sanitizer.

## Development workflow

1. Create a focused branch from `main`.
2. Build with `make`.
3. Run `make test` before submitting a pull request.
4. Explain any change that affects target selection, confirmation, write behavior, or verification.

Changes to destructive behavior should include a test that uses a temporary regular file. Automated tests must never write to block devices.

Keep the core dependency-free where practical. New dependencies should have a clear maintenance or safety benefit and a license compatible with this project's dual-license terms.

## Licensing contributions

Unless you explicitly state otherwise, any contribution intentionally submitted for inclusion in this project is licensed under either Apache-2.0 or MIT, at the recipient's option, without additional terms or conditions.
