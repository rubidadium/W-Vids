# W Vids

W Vids is a swiss-army knife of tools intended to let you edit audio and video in any way that *isn't* classified as video editing.

The main feature currently offered is trimming videos straight to the clipboard.

## Creating a Windows release

Once `.github/workflows/release.yml` is pushed to the default branch, open the
repository's **Actions** tab, select **Build Windows release**, and click
**Run workflow**. Select the branch to build, enter a new release tag (such as
`v1.0.0`), and optionally enter a changelog. The tag does not need to exist yet;
existing tags are rejected to avoid releasing a different revision by mistake.

The workflow builds the selected revision with CMake and publishes a release
whose title and tag match your input and whose description is your changelog
(or blank). It attaches:

- `w-vids.exe`: the main executable, requiring Qt runtime dependencies.
- `w-vids-windows-x64.zip`: the executable bundled with its Qt dependencies.
  Extract the entire ZIP before running the app. If a Visual C++ redistributable
  installer is included, install it if your PC does not already have that runtime.
- `w-vids-source.zip`: the tracked source files from the exact revision built.

GitHub Actions must be enabled and allowed to use the workflow's
`contents: write` permission. No separate release token is required. Uploads
are prepared as a draft before publication; if publication fails, check for a
leftover draft on the Releases page before retrying.

# FAQ
## What is this for?
Google Vids

## Who is this for?
The Stooges (or, by extension of infinite stooge theory, anyone.)

## Why is this for?
So the whole shared project doesn't die to lag.

## When is this for?
Good question.
