# AiVM Release Checklist

Use this checklist for alpha, beta, release-candidate, and stable AiVM releases.

## Preflight

- Confirm the release branch follows the workspace Git Flow policy.
- Confirm `src/CMakeLists.txt` contains the intended base version.
- Confirm `README.md`, `CHANGELOG.md`, and release notes describe the release accurately.
- Confirm the working tree contains no generated artifacts.

## Local Verification

```bash
./build.sh
./test-aivm-c.sh
```

Optional broader gate:

```bash
AIVM_CTEST_LABEL='unit|integration' ./test-aivm-c.sh
```

## Release

- Push the release branch and confirm GitHub Actions pass.
- Tag with `v<version>`, for example `v0.0.1-alpha.17`.
- Confirm the GitHub release is marked as a prerelease for `-alpha`, `-beta`,
  `-rc`, and `-local` tags.
- Confirm Linux, macOS, and Windows artifacts are attached.

## Post-Release

- Update `CHANGELOG.md` for the released version if needed.
- Update the website release page with the known-good AiLangCore alpha set.
- Merge the release branch back according to Git Flow.
