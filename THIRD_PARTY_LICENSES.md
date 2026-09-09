# THIRD_PARTY_LICENSES.md

tolunnet vendors third-party code under licences that require notice retention.
The notices below are reproduced verbatim from the vendored sources.

## lwIP — `vendor/lwip/`

lwIP is vendored unmodified from the official 2.2.0 release
(`lwip-2.2.0.zip`, `https://download.savannah.gnu.org/releases/lwip/`).
The exact source, version, and checksum are recorded in
[`vendor/README.md`](vendor/README.md).

lwIP is licensed under the BSD licence. The notice below is reproduced from the
vendored `COPYING` file.

```
Copyright (c) 2001, 2002 Swedish Institute of Computer Science.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
```

## SANA-II Network Device Specification Headers — `include/devices/sana2.h`

The SANA-II network device interface headers are standard AmigaOS interface definitions originally published by Commodore-Amiga, Inc. and distributed in the Amiga Native Development Kit (NDK) and Roadshow TCP/IP SDK.

Permission is granted to use and distribute standard AmigaOS header definitions for development of compatible AmigaOS drivers and network stacks.

## Roadshow TCP/IP Stack 'C' Header Files (SDK 1.8) — `include/netinclude/`

The BSD socket, networking, and system include headers under `include/netinclude/` are vendored from the Roadshow TCP/IP SDK version 1.8 ("Freely Distributable", Copyright (C) 2001-2016 by Olaf Barthel).

Notice reproduced verbatim from the header files:

```
/*
 * :ts=8
 *
 * 'Roadshow' -- Amiga TCP/IP stack
 * Copyright (C) 2001-2016 by Olaf Barthel.
 * All Rights Reserved.
 *
 * Amiga specific TCP/IP 'C' header files;
 * Freely Distributable
 */
```



## bsdsocktest — `vendor/bsdsocktest/`

bsdsocktest is a third-party bsdsocket.library conformance test suite by
tbdye, vendored (source only) from `https://github.com/tbdye/bsdsocktest`
at commit `cb08680843bc9cff93d57ca4760e59ab71e93b57` (2026-02-16). It is
built unmodified with the project toolchain as the `bsdsocktest` Makefile
target and used as a bench-only test binary (ANX-01); it is not part of
any distributed package.

bsdsocktest is licensed under the GNU General Public License, version 3.
The full licence text is reproduced verbatim in
[`vendor/bsdsocktest/LICENSE`](vendor/bsdsocktest/LICENSE).

```
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```
