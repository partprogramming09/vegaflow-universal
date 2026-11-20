# Contributing

Thanks for wanting to help. This guide says how to do it without wasting time.

## Before you start
- Search the issues to see whether what you want to report or propose already exists.
- For a large change, open an issue first and tell us the idea, so nobody works on something that cannot be accepted.
- Everyone who takes part follows the [code of conduct](CODE_OF_CONDUCT.md).
- AI-assisted contributions are welcome; they go through the same review and tests.

## Report a bug or propose an improvement
Use the issue forms. A good report says what you did, what you expected and what happened, with the real output pasted. For a security problem, do **not** open an issue: follow [SECURITY.md](SECURITY.md).

## Send a change
1. Fork the repository and create a branch whose name says what changes.
2. Make small commits with [Conventional Commits](https://www.conventionalcommits.org/) messages (`feat:`, `fix:`, `docs:`).
3. Write or update the tests and run them: they must pass. On Windows, `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests` builds the daemon and runs the C++ suite; `cargo test` in `rust/vegaflow-core` runs the Rust tests on any OS. The C++ suite changes the registry and the power plan for real and restores them at the end: do not stop it halfway. Details in [Build and test](docs/guides/build-and-test.md).
4. Every function that changes the system takes a `dry_run` parameter, and nothing is ever injected into or written to a game that uses anti-cheat: all disk and process work goes through `SandboxGuard`.
5. If the change alters behaviour, update the documentation (in English, under `docs/guides/`) and the [CHANGELOG](CHANGELOG.md) in the same pull request. An optimization without its own measurement is listed as not measured in [`docs/measurements/verification-status.md`](docs/measurements/verification-status.md).
6. Open the pull request and fill in its template.

## What to expect
We answer within about 14 days. We may ask for changes or say no: it is not personal, it keeps the project manageable.
