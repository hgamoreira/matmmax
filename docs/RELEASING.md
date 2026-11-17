# Sharing MatmMax on GitHub

The first public release is a **source snapshot**, with ESP8266 L01-r14
hardware results and ESP32-S3 E03 emulator results clearly separated.
Do not tag it as fully hardware-validated or attach a personal flash dump.
Binary redistribution materials remain a separate release task; see
[third-party notices](../THIRD_PARTY_NOTICES.md).

## Repository

The existing public repository is `hgamoreira/matmmax`. The first source
release was published under the former MathSmith OS name.

The workstation project prepares a clean standalone export using
`sharing/export.py`. It copies an explicit selection from one committed
revision, creates a new README/progress file, omits hardware logs and DOS
programs, and records checksums/provenance. It does not copy workstation Git
history. The exported `docs/DECISIONS.md` is the portable-platform excerpt;
personal network identifiers in exported Markdown are replaced with examples.
Source code is copied byte-for-byte. Credentials and SDKs are not included.

From a clone, push reviewed changes to the existing repository:

```sh
git push origin main
```

## GitHub Pages

In repository **Settings → Pages → Build and deployment**, select
**GitHub Actions**. Then open **Actions → Project website → Run workflow**.
The workflow validates and builds only `website/`, uploads a Pages artifact,
and deploys to the `github-pages` environment. It is manually triggered, so
a source push alone does not publish or update the website.

Project URL: `https://hgamoreira.github.io/matmmax/`.
Links derive from the actual repository name at build time. The site needs
no API key, hosted database or external JavaScript library. It hosts project
information, not the live board's HTTP/WebSocket interface.

The workflow follows GitHub's
[custom Pages workflow documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

## First release

After the initial push, create a release tag such as `v0.1.0` and use the
release notes below. GitHub automatically provides source ZIP/tar archives
for the tag. No compiled firmware is required for this source-first release.
The initial commit and its export manifest identify the exact source.

Suggested title: **MathSmith OS 0.1 — ESP8266 hardware + ESP32-S3 QEMU**.

Suggested release notes:

> Early source release of the microcontroller workstation. ESP8266 L01-r14
> provides serial/browser math, saved Wi-Fi, a browser script editor and
> scriptable GPIO. Core workflows and five LED flashes were confirmed on a
> Wemos D1 mini; the remaining peripheral and stress checks stay open.
> ESP32-S3 E03 provides serial math, FFT, foreground apps and external ELF
> upload, tested in QEMU; physical board validation is pending. RP2350 is a
> draft. AI, attached keyboard/display and SD storage are future work.
> Build instructions and pinned dependency installers are included. Original
> code is MIT; SDKs and tools retain their own licenses. Source-only release.

Create tags/releases only after choosing the exact commit to publish.
Do not upload existing `releases/` folders wholesale: they are development
candidates, with debug artifacts and incomplete redistribution materials.
