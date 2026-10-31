# Where the license texts come from

The About screen of PB Store shows these files (named by `manifest.txt`). They are the standard published texts, copied unchanged
unless noted. Nothing in them is written by this project.

| File | Source |
|------|--------|
| `Apache-2.0.txt` | `common-licenses/Apache-2.0` of the Debian/Ubuntu `base-files` package |
| `LGPL-2.1.txt` | `common-licenses/LGPL-2.1` of the same package |
| `LGPL-3.0.txt` | `common-licenses/LGPL-3` of the same package |
| `GPL-3.0.txt` | `common-licenses/GPL-3` of the same package |
| `MPL-2.0.txt` | `common-licenses/MPL-2.0` of the same package |
| `curl.txt` | `COPYING` of the curl sources that accompany the PocketBook SDK's CMake (copyright line `1996 - 2024, Daniel Stenberg`); the libcurl built by `scripts/build-curl-arm.sh` (8.18.0) is a newer release under the same license |
| `zlib.txt` | the license block of `zlib.h` of zlib 1.3.1 (the comment markers removed), which is the zlib license in full |
| `miniz-MIT.txt` | `third_party/miniz/LICENSE`, unchanged (a test compares the two) |
| `inkfeed-MIT.txt` | the InkFeed section of `NOTICE` (MIT, Copyright (c) 2026 Joost Rohde) |
| `../LICENSE` (named as `LICENSE`) | this project's own license |

## Components without a text file

- **PocketBook InkView** (`libinkview`): the SDK ships no license file and `inkview.h` carries no license statement. The entry names no
  license; it says the library is part of the reader's firmware and is not redistributed by this project.
- **GCC runtime libraries**: the GCC Runtime Library Exception 3.1 text is not part of the SDK's files. The entry shows the GPL-3.0 text and
  gives the published address of the exception.
- **Icons**: the menu icons `res/icons/pbstore.bmp` and `pbstore_f.bmp` are new artwork drawn by `scripts/make-icons.py` (see `res/icons/README.md`); the line icons of the
  user interface `qml/icons/*.svg` belong to the icon set of the sibling apps PB Notes and PB Cloud (same author, MIT, see research R-13). All carry the
  MIT license of this project; none is a PocketBook logo.
