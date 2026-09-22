# THIRD_PARTY_LICENSES.md

tolunnet includes third-party code and headers whose licences require their
notices to be kept. The notices below are copied verbatim from the included
sources.

## lwIP 2.2.0 (`vendor/lwip/`)

lwIP comes from the official 2.2.0 release (`lwip-2.2.0.zip`,
`https://download.savannah.gnu.org/releases/lwip/`). It is compiled into the
`tolunnet` daemon.

The copy in this repository has three local changes:
- `src/core/tcp_out.c`: a guard and walk cap against a `tcp_output` livelock in a circular unsent queue.
- `src/core/dns.c` and `src/include/lwip/dns.h`: `dns_set_dest_port()`, which overrides the resolver's destination port. The bench uses it.

The lwIP sources compiled into the binaries carry the following copyright
lines:

```
Copyright (c) 2001-2004 Swedish Institute of Computer Science.
Copyright (c) 2001-2003 Swedish Institute of Computer Science.
uIP version Copyright (c) 2002-2003, Adam Dunkels.
Copyright (c) 2001-2004 Leon Woestenberg <leon.woestenberg@gmx.net>
Copyright (c) 2003-2004 Leon Woestenberg <leon.woestenberg@axon.tv>
Copyright (c) 2001-2004 Axon Digital Design B.V., The Netherlands.
Copyright (c) 2003-2004 Axon Digital Design B.V., The Netherlands.
Copyright (c) 2007 Dominik Spies <kontakt@dspies.de>
Copyright (c) 2018 Jasper Verschueren <jasper.verschueren@apart-audio.com>
Copyright (c) 2015 Verisure Innovation AB
```

Apart from `igmp.c` (see below), they are all licensed under these terms
(from the lwIP `COPYING` file):

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

`src/core/ipv4/igmp.c`:

```
Copyright (c) 2002 CITEL Technologies Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of CITEL Technologies Ltd nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY CITEL TECHNOLOGIES AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL CITEL TECHNOLOGIES OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

## Roadshow TCP/IP SDK 1.8 headers (`include/netinclude/`, `include/libraries/bsdsocket.h`, `sfd/`)

The BSD socket and networking headers under `include/netinclude/` are taken
from the Roadshow TCP/IP SDK 1.8, and so are `include/libraries/bsdsocket.h`
and the library definition files `sfd/bsdsocket_lib.sfd` and
`sfd/usergroup_lib.sfd`. The Roadshow notice (years vary per file from
2001-2016 to 2001-2023):

```
'Roadshow' -- Amiga TCP/IP stack
Copyright (C) 2001-2016 by Olaf Barthel.
All Rights Reserved.

Amiga specific TCP/IP 'C' header files;
Freely Distributable
```

Many of these headers also contain BSD-derived code under the notices below.

### University of California, Berkeley

This notice appears in 44 headers: `arpa/*`, `grp.h`, `pwd.h`, `utmp.h`,
`netdb.h`, `resolv.h`, `libraries/usergroup.h`, `net/*`, most of
`netinet/*`, and `sys/*`. The copyright years differ per file, from 1980 to
1995.

```
Copyright (c) 1983, 1993
     The Regents of the University of California.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. All advertising materials mentioning features or use of this software
   must display the following acknowledgement:
     This product includes software developed by the University of
     California, Berkeley and its contributors.
4. Neither the name of the University nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

The University of California rescinded clause 3 (the advertising clause) in
1999. The headers are still reproduced unchanged.

`netinet/igmp.h`, `netinet/igmp_var.h` and `netinet/ip_mroute.h` also carry
`Copyright (c) 1988, 1989 Stephen Deering` (code contributed to Berkeley by
Stephen Deering of Stanford University) under the same Berkeley terms.

### Digital Equipment Corporation (`arpa/nameser.h`, `netdb.h`, `resolv.h`)

```
Portions Copyright (c) 1993 by Digital Equipment Corporation.

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies, and that
the name of Digital Equipment Corporation not be used in advertising or
publicity pertaining to distribution of the document or software without
specific, written prior permission.

THE SOFTWARE IS PROVIDED "AS IS" AND DIGITAL EQUIPMENT CORP. DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS.   IN NO EVENT SHALL DIGITAL EQUIPMENT
CORPORATION BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
```

### Darren Reed (`netinet/ip_fil.h`, `netinet/ip_nat.h`)

```
Copyright (C) 1993-2001 by Darren Reed.

The author accepts no responsibility for the use of this software and
provides it on an ``as is'' basis without express or implied warranty.

Redistribution and use, with or without modification, in source and binary
forms, are permitted provided that this notice is preserved in its entirety
and due credit is given to the original author and the contributors.

The licence and distribution terms for any publically available version or
derivative of this code cannot be changed. i.e. this code cannot simply be
copied, in part or in whole, and put under another distribution licence
[including the GNU Public Licence.]
```

These two headers are part of the unmodified Roadshow SDK header set and
stay under the author's own licence above.

## SANA-II headers (`include/devices/sana2.h`, `include/devices/sana2specialstats.h`)

These are taken unchanged from the AmigaOS NDK (Release 50.1) as shipped
with the Roadshow SDK. Their notice:

```
(C) Copyright 1991-2003 Amiga, Inc.
    All Rights Reserved
```

## bsdsocktest (`vendor/bsdsocktest/`)

bsdsocktest is a third-party `bsdsocket.library` conformance test suite by
tbdye. Only its source is included, from `https://github.com/tbdye/bsdsocktest`
at commit `cb08680843bc9cff93d57ca4760e59ab71e93b57` (2026-02-16).
- It is built unchanged with the project toolchain as `build/bsdsocktest`.
- It is used only as a bench test binary and is not part of any release package.

bsdsocktest is licensed under the GNU General Public License, version 3. The
full licence text is in
[`vendor/bsdsocktest/LICENSE`](vendor/bsdsocktest/LICENSE).

```
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```
