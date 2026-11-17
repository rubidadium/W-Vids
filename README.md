# W Vids

W Vids is a swiss-army knife of tools intended to let you edit audio and video in any way that *isn't* classified as video editing.

The main feature currently offered is trimming videos straight to the clipboard.

It's basically LosslessCut but tailored specifically for getting files into Google Vids.

# Windows releases

Download `w-vids-windows-x64.exe` from a release and open it. This is a single-file
launcher, with no installer wizard or administrator access required. It contains
the app and Qt; the first launch unpacks them, and later launches reuse those files.

Files are stored per Windows user:

| Files | Location |
| --- | --- |
| Downloaded videos | `Videos\wvids\download` |
| Exported clips | `Videos\wvids\clip` |
| FFmpeg and ffprobe | `%LOCALAPPDATA%\wvids\tools\ffmpeg` |
| yt-dlp and Deno | `%LOCALAPPDATA%\wvids\tools` |
| Unpacked app and Qt | `%LOCALAPPDATA%\wvids\runtime\<payload hash>` |

The Videos path follows Windows folder settings, including moved or redirected
Videos folders. Media directories are created when a download or export needs them.
FFmpeg is downloaded after the first-run setup prompt; yt-dlp and Deno are downloaded
when needed for links. Downloads are verified before installation, and installed
tools are reused across launches and app releases. Internet access is needed for
initial tool setup and video downloads, but not for editing local videos once
FFmpeg is installed.

Replacing or moving the launcher does not remove videos, clips, or downloaded
tools. Different app releases have separate runtime folders, so an update does not
overwrite a running app. Missing runtime files are extracted again on the next
launch. Old runtime folders are retained; they can be removed when their app
versions are closed. Existing files beside older executables are not automatically
moved or deleted.

## Building the single-file launcher

Build `w-vids` in Release mode, deploy it into a clean directory with `windeployqt`,
then run the packaging script with NSIS 3.12 installed or extracted:

```powershell
New-Item -ItemType Directory -Force package | Out-Null
Copy-Item build/bin/w-vids.exe package/
windeployqt --release --compiler-runtime --no-translations package/w-vids.exe
./packaging/build-wrapper.ps1 -PackageDirectory package `
    -OutputFile dist/w-vids-windows-x64.exe -Makensis 'C:\path\to\NSIS\makensis.exe'
```

The Windows release workflow builds and checks the launcher before uploading the
single `.exe` and matching source archive. To run its isolated launcher checks
locally, configure with `-DWVIDS_BUILD_TESTS=ON`, build `app_paths_test` and
`wrapper_probe`, then run:

```powershell
./build/bin/app_paths_test.exe
./tests/wrapper_test.ps1 -Makensis 'C:\path\to\NSIS\makensis.exe' `
    -ProbeExecutable build/test-tools/wrapper_probe.exe -WorkDirectory build/wrapper-check
```

Use a fresh work directory for each launcher test. These checks do not write to
your Videos or app-data folders.

# FAQ
## What is this for?
Google Vids

## Who is this for?
The Stooges (or, by extension of infinite stooge theory, anyone.)

## Why is this for?
So the whole shared project doesn't die to lag.

## When is this for?
Good question.
