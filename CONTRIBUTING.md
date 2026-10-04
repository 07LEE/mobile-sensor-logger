# Contributing

Thanks for helping with Mobile Sensor Logger. This page covers where to send changes and what a change should look like.

## Where to send changes

Pull requests go to `develop`, not to `main`. The main branch holds released states only, and the maintainer merges `develop` into it when a version is released.

Start a short-lived branch from `develop`, open the pull request against `develop`, and wait for the build check to pass. A pull request cannot be merged while it is failing.

## Before you start

For a bug or a new feature, open an issue first using the bug report or feature request template. A small fix can go straight to a pull request.

## Building and testing

See the [build and install guide](docs/building.md) for building the app. The sampling logic has host tests that run without a phone:

```bash
./scripts/test_sampling.sh
```

The export scripts have Python tests, run with `python3 -m pytest tests`.

Say in the pull request what you ran and, for a change to capture or recording, which device you checked it on. Recording behavior differs between phones, so a device name is worth including.

## Commit messages and pull request titles

Use a type prefix, then a lowercase imperative subject with no trailing period, for example `fix: count only indexed frames as saved`. The types are feat, fix, docs, style, refactor, test and chore. Add a one-line body that says why the change is needed.

External pull requests are squash merged, so the pull request title becomes the commit subject. Write it in the same format.

## Pull request description

Follow the pull request template: a short summary of what changed, what you actually checked, and `Fixes #N` when an issue exists. Leave out anything you did not check unless a reviewer needs it to judge the change.
