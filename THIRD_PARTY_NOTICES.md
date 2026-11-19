# Third-party notices

SlopScope implementation code is licensed under the root [MIT license](LICENSE).
Third-party material retains its original licenses, reproduced below. These
notices do not grant rights over stock firmware. Pinned revisions and source hashes are recorded in the vendor directories.

## LittleFS v2.9.3 — included in the scope application

Upstream: https://github.com/littlefs-project/littlefs/tree/v2.9.3

`vendor/littlefs/lfs.c`, `lfs.h`, `lfs_util.c`, `lfs_util.h` and `LICENSE.md`
were byte-for-byte verified against that tag on 2026-11-17. Exact hashes are in
`vendor/littlefs/UPSTREAM.json`. Sources and original notices are unmodified.
The firmware links read-only LittleFS support. Include this entire notice with
source and firmware binary distributions; a link alone does not carry the
required copyright notice, conditions and disclaimer into a binary release.

### LittleFS license (BSD-3-Clause)

Copyright (c) 2022, The littlefs authors.  
Copyright (c) 2017, Arm Limited. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

-  Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
-  Redistributions in binary form must reproduce the above copyright notice, this
   list of conditions and the following disclaimer in the documentation and/or
   other materials provided with the distribution.
-  Neither the name of ARM nor the names of its contributors may be used to
   endorse or promote products derived from this software without specific prior
   written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## Adafruit classic bitmap font — included in the scope application

Upstream: https://github.com/adafruit/Adafruit-GFX-Library/tree/f256ef6b0f248711a0cd354be0da5ccd224808c3

`vendor/adafruit-gfx-font/` retains unmodified `src/glcdfont.c` and its license.
`tools/fonts.py` generates a printable-ASCII subset transposed to seven rows in
`firmware/tables/hud_font.h`. SlopScope retains its 5×7 cells and six-pixel advance.
The font data, including that generated subset, retains the following license.

### Adafruit font license (BSD-2-Clause)

Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

## External build, maintenance and development dependencies

These packages are installed separately; their source, wheels, framework code,
executables and caches are not part of the proposed SlopScope source tree.
Their licenses do not become the license of this repository merely because a
build script invokes or imports them. Bundling them requires reviewing the actual packaged components and their
notices.

| Dependency | Use | Upstream license/reference |
| --- | --- | --- |
| NumPy | Artwork processing | [BSD-3-Clause](https://github.com/numpy/numpy/blob/main/LICENSE.txt); wheels also carry component licenses |
| Pillow / PIL | Artwork processing | [MIT-CMU](https://github.com/python-pillow/Pillow/blob/main/LICENSE) |
| pyelftools | Read ELF build outputs | [Public-domain dedication / Unlicense](https://github.com/eliben/pyelftools/blob/main/LICENSE); bundled Construct has separate terms |
| PyUSB | Host maintenance and diagnostics | [BSD-3-Clause](https://github.com/pyusb/pyusb/blob/v1.3.1/LICENSE) |
| libusb | Host USB backend | [LGPL-2.1-or-later](https://github.com/libusb/libusb/blob/v1.0.30/COPYING) |
| pySerial | Optional ESP32 console | [BSD-3-Clause](https://github.com/pyserial/pyserial/blob/v3.5/LICENSE.txt) |
| Clang / LLVM / LLD | ARM compilation and linking | [Apache-2.0 with LLVM exceptions](https://github.com/llvm/llvm-project/blob/main/llvm/LICENSE.TXT) |
| sunxi-tools / sunxi-fel | External FEL loader | [Upstream per-file notices](https://github.com/linux-sunxi/sunxi-tools); tool binary is not bundled |

The optional development scripts also import the separately maintained `hscope`
project. Its licensing and that of the separate web UI require their own audits.
Neither project is copied into this repository.

## Technical references and unresolved material

Linux register documentation, libusb behavior and the ARM ABI were used as
technical references. References do not grant permission to copy their source;
no Linux, libusb, Apple SDK or ARM runtime implementation is vendored here.
Hardware register addresses, configuration values and lookup data recovered from
stock are retained as interoperability data in the source. They are
not automatically copies of a vendor implementation. Actual stock BOOT0
instructions copied into the local FEL helper remain outside release contents.
