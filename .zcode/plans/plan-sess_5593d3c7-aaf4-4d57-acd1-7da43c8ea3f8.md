## TOLUNET — M0 İskeleti + M1 Kaynak Kodu (ilk oturum)

Tüm etkileşim Türkçe; kod/commit'ler İngilizce. Sözleşme v3.0'nın §4 kurallarına (uydurma API yok, kopya stack kodu yok, "çalışmalı" yok, her commit derlenir) sıkı bağlılık.

### Onaylanan kararlar
- **Kapsam:** M0 iskeleti + M1 kaynak kodu. Tüm M1 parçaları STATUS.md'de KANITLANMADI işaretli. Exit testleri insana ait.
- **lwIP:** `lwip-2.2.0.zip` indir → `vendor/lwip/` (değiştirilmemiş), BSD COPYING → THIRD_PARTY_LICENSES.md. Repoya commit, CI çevrimdışı.
- **Toolchain:** AmigaPorts prebuilt container CI'da; bebbo→AmigaPorts taşıması QUESTIONS.md maddesi. Flags: `-O2 -fomit-frame-pointer -m68020 -noixemul`.

### Üretilecek dosyalar (sözleşme §2 sırasıyla)

**Kök dokümanlar:** LICENSE (GPL-3.0-or-later, Copyright 2026 tolon), README.md, THIRD_PARTY_LICENSES.md (lwIP BSD), STATUS.md (şablon: Proven boş / Built-unproven M0+M1 / Next M0 exit), ISSUES.md (TNET-xxx şema), QUESTIONS.md (seed'ler + toolchain konumu), .gitignore.

**Makefile:** `make all`, `make clean`, `make lwip`. Hello-task hedefi. amiga-gcc değişkenleri.

**Vendor:** `vendor/lwip/` (lwip-2.2.0.zip içeriği, değiştirilmemiş), `vendor/patches/` (.gitkeep), `vendor/README.md` (sürüm, URL, sha256).

**Include:** `include/tolunet/protocol.h` (§5 birebir: TN_PROTO_VERSION, TnReqKind enum, TnRequest struct), `include/tolunet/config.h` (§5.2 anahtar sabitleri).

**lwipopts:** `lwipopts/lwipopts.h` (§6 start değerleri birebir).

**Kaynak (M0):** `src/task/main.c` (hello-task: Exec kurulumu, DOS'a satır yazar), `src/common/log.c` (DOS log, no stdio), `src/common/mem.c` (yer tutucu).

**Kaynak (M1 — KANITLANMADI):** `src/sana2/sana2_netif.c` (device açma, S2_DEVICEQUERY, copyfuncs, S2_ONLINE, ≥4 CMD_READ pump, S2_OFFLINE+close — tüm sabitler SANA-II Rev 7'den, şüpheliler VERIFY), `src/sana2/buffers.c` (copyfuncs + >4KB copy ring iskeleti), `src/cmds/TolunetStatus.c` (iskelet + raw frame loglayıcı).

**CI:** `.github/workflows/build.yml` — Job 1 amiga-gcc container'da `make all` + artefact, Job 2 `tests/host` native. Her push/branch.

**Tests:** `tests/host/` (protocol encode/decode iskeleti), `tests/amiga/` (.gitkeep).

**Docs:** `docs/architecture.md`, `docs/protocol.md` (§5 union stub), `docs/bench.md` (§8 birebir), `docs/compat.md` (yer tutucu), `docs/probes/` (.gitkeep).

### Commit sırası (her biri derlenebilir)
1. scaffold: repo skeleton, LICENSE, tracking docs, README
2. scaffold: vendor lwIP 2.2.0, record checksum
3. scaffold: protocol.h + config.h + lwipopts.h
4. scaffold: Makefile + hello-task + log.c
5. ci: two-job workflow
6. scaffold: bench.md + architecture.md + protocol.md stubs
7. m1: sana2_netif.c + buffers.c (UNPROVEN, STATUS.md'de işaretli)

### Disiplin
lwIP değiştirilmez (§2). AmiTCP/Roadshow/AROS/PaulaNET kodu kopyalanmaz (§4.2). Doğrulanamayan API'ler `/* VERIFY */` + QUESTIONS.md (§4.1). "Çalışmalı"/"derliyor" tabusu (§4.3). M0 exit testi geçmeden M2'ye geçilmez.

### İnsanın (tolon) sonraki adımları
M0 exit: WinUAE'de hello-task → WORK: satırı → STATUS.md'ye yapıştır. M1 exit: bench.md konfig + TolunetStatus raw frame tool, broadcast + log. Toolchain kararını QUESTIONS.md'de onayla. SDI_headers.lha'yı stage et.