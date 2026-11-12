# Third-Party Notices

This project is distributed under the MIT License (see `LICENSE`). The portions listed
below carry their own copyright notices, and MIT requires those notices to travel with
every copy, source or binary — being MIT ourselves does not absorb them.

If you redistribute this software — including the MSI installers — this file must be
included.

## OpenEGo Hub

Ultra-EGoHub is a fork of OpenEGo Hub by NihilDigit. The root LICENSE preserves
the upstream MIT license and copyright notices.

Source: https://github.com/NihilDigit/openego-hub

---

## EGoTouchRev

Ultra-EGoHub uses EGoTouchRev by Detach2233 for touch and pen input.
The vendored source is based on commit
6365e327f2aea50024e8ba96d5cb810ca39e21ad. The full license is retained in
third_party/EGoTouchRev/LICENSE and shipped with the driver.

Source: https://github.com/awarson2233/EGoTouchRev

```
MIT License

Copyright (c) 2025 Detach2233

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## Assets that are deliberately not redistributed

The Ultra-EGoHub UI can render pen product photographs and battery icons from an existing
HUAWEI PC Manager installation, read at runtime from `%ProgramFiles%\Huawei\PCManager\`.
When PC Manager is absent, it uses its own Fluent fallback artwork.

Those image files are HUAWEI's artwork. They are read from the user's own installation
and are **never** copied into this repository or into the installers.

---

## Qualcomm display library

Qualcomm's qdcmlib.dll is a platform library with its own licensing. The public
release does not bundle it. Colour gamut presets require a compatible library
already provided by the system or supplied locally. Some system copies fail to
initialise on this hardware. See hal/docs/display-manage.md.

---

## GitHub Octicons

The GitHub mark displayed on the About page is derived from `mark-github-16` in
GitHub Octicons.

Source: https://github.com/primer/octicons

```
MIT License

Copyright (c) 2026 GitHub Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## HuaweiPods protocol reference (not bundled)

- Repository: https://github.com/Nshpiter/HuaweiPods
- Reference commit: dc5b9d14e9030be95b178699d674d1a3217e1e6d
- Upstream license: GPL-3.0 (see its LICENSE)
- Used as a reference for public wire-format, command and device-capability facts.
  The new native Windows implementation is independently written; no upstream
  Kotlin/Xposed implementation, APK or application assets are bundled. Official CDN artwork is separately documented below.
- See docs/headphones-protocol.md for scope, protocol details and hardware limits.

## Huawei headphone artwork references

The public repository and MSI contain an artwork manifest and built-in fallback
icons. The original Huawei CDN images remain the property of their rights holders.
The project's MIT license applies to its code. The manifest records the artwork
sources, dimensions and checksums for provenance.

## EnergyStar (native integration)

Source: https://github.com/imbushuo/EnergyStar

Pinned commit: 18872289ac4de7d22208f1fc5b156abaa904dff8

The native tray implementation adapts the foreground/background efficiency-mode logic. Original source and license are preserved in third_party/EnergyStar.

```
Copyright 2022 Bingxing Wang

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

If you are Microsoft (and/or its affiliates) employee, vendor or contractor who is working on Windows-specific integration projects, you may use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so without the restriction above.
```

## Microsoft Windows App SDK

The MSI includes the redistributable Microsoft Windows App Runtime bootstrap
library from Windows App SDK 2.6.4. The Microsoft license in
third_party/WindowsAppSDK/LICENSE.txt is installed as WindowsAppSDK-LICENSE.txt.
Install the matching ARM64 runtime separately.

Source: https://www.nuget.org/packages/Microsoft.WindowsAppSDK.Foundation/2.6.4
