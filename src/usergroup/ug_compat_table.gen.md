# usergroup.library Compatibility Matrix

Generated automatically by `scripts/gen_usergroup_table.py` from `sfd/usergroup_lib.sfd`.

> Note: `crypt()` uses an internal FNV hash, not Unix DES; existing AmiTCP passwd files are not compatible.

| Offset | Function | Signature | Status | Implementation Details |
|--------|----------|-----------|--------|------------------------|
| `-30` | `ug_SetupContextTagList` | `LONG ug_SetupContextTagList(STRPTR name,struct TagItem *tags)` | **BUILT** | In-memory DB + file reader fallback |
| `-36` | `ug_GetErr` | `LONG ug_GetErr()` | **BUILT** | In-memory DB + file reader fallback |
| `-42` | `ug_StrError` | `STRPTR ug_StrError(LONG err)` | **BUILT** | In-memory DB + file reader fallback |
| `-48` | `getuid` | `LONG getuid()` | **BUILT** | In-memory DB + file reader fallback |
| `-54` | `geteuid` | `LONG geteuid()` | **BUILT** | In-memory DB + file reader fallback |
| `-60` | `setreuid` | `LONG setreuid(LONG real,LONG effective)` | **BUILT** | In-memory DB + file reader fallback |
| `-66` | `setuid` | `LONG setuid(LONG uid)` | **BUILT** | In-memory DB + file reader fallback |
| `-72` | `getgid` | `LONG getgid()` | **BUILT** | In-memory DB + file reader fallback |
| `-78` | `getegid` | `LONG getegid()` | **BUILT** | In-memory DB + file reader fallback |
| `-84` | `setregid` | `LONG setregid(LONG real,LONG effective)` | **BUILT** | In-memory DB + file reader fallback |
| `-90` | `setgid` | `LONG setgid(LONG gid)` | **BUILT** | In-memory DB + file reader fallback |
| `-96` | `getgroups` | `LONG getgroups(LONG gidsetlen,LONG *gidset)` | **BUILT** | In-memory DB + file reader fallback |
| `-102` | `setgroups` | `LONG setgroups(LONG gidsetlen,LONG *gidset)` | **BUILT** | In-memory DB + file reader fallback |
| `-108` | `initgroups` | `LONG initgroups(STRPTR name,LONG basegid)` | **BUILT** | In-memory DB + file reader fallback |
| `-114` | `getpwnam` | `struct passwd * getpwnam(STRPTR login)` | **BUILT** | In-memory DB + file reader fallback |
| `-120` | `getpwuid` | `struct passwd * getpwuid(LONG uid)` | **BUILT** | In-memory DB + file reader fallback |
| `-126` | `setpwent` | `VOID setpwent()` | **BUILT** | In-memory DB + file reader fallback |
| `-132` | `getpwent` | `struct passwd * getpwent()` | **BUILT** | In-memory DB + file reader fallback |
| `-138` | `endpwent` | `VOID endpwent()` | **BUILT** | In-memory DB + file reader fallback |
| `-144` | `getgrnam` | `struct group * getgrnam(STRPTR name)` | **BUILT** | In-memory DB + file reader fallback |
| `-150` | `getgrgid` | `struct group * getgrgid(LONG gid)` | **BUILT** | In-memory DB + file reader fallback |
| `-156` | `setgrent` | `VOID setgrent()` | **BUILT** | In-memory DB + file reader fallback |
| `-162` | `getgrent` | `struct group * getgrent()` | **BUILT** | In-memory DB + file reader fallback |
| `-168` | `endgrent` | `VOID endgrent()` | **BUILT** | In-memory DB + file reader fallback |
| `-174` | `crypt` | `UBYTE * crypt(UBYTE *key,UBYTE *set)` | **BUILT** | FNV-based hash (internal; existing AmiTCP passwd files are not compatible) |
| `-180` | `ug_GetSalt` | `UBYTE * ug_GetSalt(struct passwd *user,UBYTE *buf,ULONG size)` | **BUILT** | In-memory DB + file reader fallback |
| `-186` | `getpass` | `STRPTR getpass(STRPTR prompt)` | **BUILT** | In-memory stub (returns empty string without prompt) |
| `-192` | `umask` | `ULONG umask(UWORD mask)` | **BUILT** | In-memory DB + file reader fallback |
| `-198` | `getumask` | `ULONG getumask()` | **BUILT** | In-memory DB + file reader fallback |
| `-204` | `setsid` | `LONG setsid()` | **BUILT** | In-memory DB + file reader fallback |
| `-210` | `getpgrp` | `LONG getpgrp()` | **BUILT** | In-memory DB + file reader fallback |
| `-216` | `getlogin` | `STRPTR getlogin()` | **BUILT** | In-memory DB + file reader fallback |
| `-222` | `setlogin` | `LONG setlogin(STRPTR name)` | **BUILT** | In-memory DB + file reader fallback |
| `-228` | `setutent` | `VOID setutent()` | **BUILT** | No-op stub |
| `-234` | `getutent` | `struct utmp * getutent()` | **BUILT** | Fixed root/console record |
| `-240` | `endutent` | `VOID endutent()` | **BUILT** | No-op stub |
| `-246` | `getlastlog` | `struct lastlog * getlastlog(LONG uid)` | **BUILT** | In-memory tracking only |
| `-252` | `setlastlog` | `LONG setlastlog(LONG uid,STRPTR name,STRPTR host)` | **BUILT** | In-memory tracking only |
| `-258` | `getcredentials` | `struct UserGroupCredentials * getcredentials(struct Task *task)` | **BUILT** | In-memory DB + file reader fallback |
