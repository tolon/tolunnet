# FORBID.md — Forbid()/Disable() bölge envanteri

Üretim: `python3 scripts/check_forbid.py --md` (elle yazılmaz; ANX-03).
Kapı: `scripts/check-forbid.sh` — `python-checks` hedefine bağlıdır; yeni bir
ihlal derlemeyi düşürür. Yasaklı çağrı listesi ve istisna dosyası:
`scripts/check_forbid.py`, `scripts/check-forbid-known.txt`.

## Neden: tek görevlilik penceresi

`Forbid()` çoklu görevlendirmeyi durdurur ama kesintileri kapatmaz — kesme
bağlamı paylaşılan yapıları değiştirebilir. Bölgede bekleyen (Wait*, DoIO,
Delay, ObtainSemaphore) veya ayıran (Alloc*, Open*, Lock) bir çağrı,
görevlendirmeyi kapalı tutarak sistemi kilitleyebilir; ayrıca PutStr/Printf/
tn_logf gibi DOS çağrıları Forbid altında kendi Forbid/Permit çiftlerini
içerebildiğinden pencere istemeden kapanabilir.

## Disable() bölgeleri — daha katı kural

`Disable()` kesintileri de kapatır: bölgede Permit/Enable/Forbid dışında
hiçbir çağrı yasaktır. Ağaçta bugün `Disable()` bölgesi yoktur; tablodaki
satırların tümü `Forbid()` bölgeleridir.

| dosya:satır | açıklama | ne bloklarsa bozar |
|---|---|---|
| (örnek satır) SANA-II yanıt portu | Aygıt kendi kesmesinden `ReplyMsg` eder; `PutMsg` `mp_SigTask/mp_SigBit/mp_Flags`'ı tek `Disable` birimi olarak okur — port yaratımı/alt edilmesi `Disable` ister (ANX-03 kural 3) | Kesme yarısı `ReplyMsg` yaparken port listesini bozmak → bozuk mesaj/donma |

## Bölge envanteri (otomatik)

| dosya:satır | tip | işlev | bölgedeki çağrılar | istisna |
|---|---|---|---|---|
| `src/lib/lib_init.c:34-36` | forbid | `*tn_lib_create` | — | — |
| `src/lib/lib_init.c:50-54` | forbid | `tn_lib_destroy` | tn_logf | EVET |
| `src/lib/lib_vectors.c:199-201` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:208-210` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:222-224` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:235-237` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:327-332` | forbid | `tn_lib_close` | — | — |
| `src/lib/lib_vectors.c:326-336` | forbid | `tn_lib_close` | — | — |
| `src/lib/lib_vectors.c:1424-1429` | forbid | `tn_lvo_getsocketevents` | — | — |
| `src/setup/hw_detect.c:86-88` | forbid | `probe_one_device` | — | — |
| `src/setup/net_test.c:293-295` | forbid | `tn_run_network_tests` | — | — |
| `src/setup/net_test.c:303-305` | forbid | `tn_run_network_tests` | — | — |
| `src/setup/setup_rexx.c:25-27` | forbid | `*tn_setup_rexx_init` | — | — |
| `src/setup/setup_rexx.c:43-45` | forbid | `tn_setup_rexx_cleanup` | — | — |
| `src/setup/stack_detect.c:486-488` | forbid | `tn_stack_detect_all` | — | — |
| `src/setup/stack_detect.c:706-709` | forbid | `tn_stack_request_quit` | — | — |
| `src/setup/stack_detect.c:732-737` | forbid | `tn_stack_request_quit` | — | — |
| `src/setup/stack_detect.c:742-747` | forbid | `tn_stack_request_quit` | — | — |
