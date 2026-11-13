# Third-Party Notices

Deox Download Manager is licensed under the MIT License (see [LICENSE](LICENSE)).

This repository contains **no vendored third-party source code**. The programs
it builds link against the libraries below at run time. When you redistribute
a binary build, and especially when you bundle any of these libraries with it
(for example a Windows installer, AppImage, or Flatpak), you must also ship the
notices and meet the license terms of each bundled library.

| Component | Used by | License | Linking |
| :--- | :--- | :--- | :--- |
| [libcurl](https://curl.se/) | `deox-dm`, `deox-dm-gui` | curl license (MIT-style) | Dynamic (system library) |
| TLS backend of libcurl: [OpenSSL 3](https://www.openssl.org/) **or** [GnuTLS](https://www.gnutls.org/) | via libcurl | Apache-2.0 (OpenSSL 3) / LGPL-2.1-or-later (GnuTLS) | Dynamic, chosen by the system's libcurl build |
| Other libcurl dependencies (nghttp2, libidn2, libpsl, libssh2, ...) | via libcurl | Various, see your distribution's package | Dynamic, chosen by the system's libcurl build |
| [Qt 6](https://www.qt.io/) (Core, Gui, Widgets) | `deox-dm-gui` only | LGPL-3.0-only (open-source edition) | Dynamic, enforced by the build (see below) |
| C++ standard library: libstdc++ **or** libc++ | all programs | GPL-3.0 with GCC Runtime Library Exception / Apache-2.0 with LLVM Exception | Either (the runtime exceptions permit static linking) |

## Dependency licensing policy

Copyleft dependencies are only accepted when they are **dynamically linked**.
The CMake build refuses to configure the GUI against a static Qt 6 build
unless `-DDEOXDM_ALLOW_STATIC_COPYLEFT=ON` is passed explicitly, and it warns
when libcurl is a static archive, since a static libcurl usually pulls in
LGPL libraries such as GnuTLS or libidn2.

---

## libcurl

`src/net/curl_abi.hpp` declares the libcurl functions and option numbers that
Deox Download Manager calls, so that the libcurl development headers are not
required at build time. The values match the public libcurl API defined in
`curl/curl.h`.

```
COPYRIGHT AND PERMISSION NOTICE

Copyright (c) 1996 - 2025, Daniel Stenberg, <daniel@haxx.se>, and many
contributors, see the THANKS file.

All rights reserved.

Permission to use, copy, modify, and distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright
notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS. IN
NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall not
be used in advertising or otherwise to promote the sale, use or other dealings
in this Software without prior written authorization of the copyright holder.
```

## Qt 6

The desktop application `deox-dm-gui` uses the Qt 6 toolkit, Copyright (C)
The Qt Company Ltd. and other contributors, under the GNU Lesser General Public
License version 3 (LGPL-3.0-only).

- License text: <https://www.gnu.org/licenses/lgpl-3.0.html> (which
  incorporates the GPL-3.0 text: <https://www.gnu.org/licenses/gpl-3.0.html>)
- Qt source code: <https://download.qt.io/official_releases/qt/> or your
  distribution's source packages.

Qt is linked dynamically, so users can replace the Qt libraries with a
modified, compatible version. If you distribute `deox-dm-gui` together with Qt
libraries, include the LGPL-3.0 and GPL-3.0 license texts and make the
corresponding Qt source code available, as the LGPL requires. The
application's **About** dialog shows the Qt license information at run time.

## TLS and other transitive libraries

The system's libcurl decides which TLS library and helper libraries are loaded.
Deox Download Manager does not link them directly. Their licenses are in your
operating system's package documentation (on Debian and Ubuntu,
`/usr/share/doc/<package>/copyright`).

---

## Trademarks

Qt is a registered trademark of The Qt Company Ltd. and its subsidiaries.
Linux is the registered trademark of Linus Torvalds in the U.S. and other
countries. Windows is a trademark of the Microsoft group of companies. Rust is
a trademark of the Rust Foundation. Ubuntu is a registered trademark of
Canonical Ltd. All other trademarks are the property of their respective
owners. Their use here identifies compatibility or comparison only and does not
imply endorsement or affiliation.
