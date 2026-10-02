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
| `src/cmds/FreezeWatch.c:78-107` | forbid | `dump_tasks` | — | — |
| `src/cmds/TolunnetPrefs.c:193-199` | forbid | `daemon_stop` | — | — |
| `src/common/ipc_client.c:60-65` | forbid | `tn_ipc_oneshot_ex` | — | — |
| `src/common/log.c:61-69` | forbid | `ring_capture` | — | — |
| `src/lib/lib_init.c:36-38` | forbid | `*tn_lib_create` | — | — |
| `src/lib/lib_init.c:53-73` | forbid | `tn_lib_destroy` | — | — |
| `src/lib/lib_vectors.c:495-497` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:504-506` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:518-520` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:612-614` | forbid | `*tn_lib_open` | — | — |
| `src/lib/lib_vectors.c:693-698` | forbid | `tn_lib_close` | — | — |
| `src/lib/lib_vectors.c:718-729` | forbid | `tn_lib_expunge` | — | — |
| `src/lib/lib_vectors.c:2044-2056` | forbid | `tn_lvo_socketbasetaglist` | — | — |
| `src/lib/lib_vectors.c:2143-2155` | forbid | `tn_lvo_getsocketevents` | — | — |
| `src/setup/hw_detect.c:87-89` | forbid | `probe_one_device` | — | — |
| `src/setup/net_test.c:249-251` | forbid | `tn_run_network_tests` | — | — |
| `src/setup/net_test.c:259-261` | forbid | `tn_run_network_tests` | — | — |
| `src/setup/setup_rexx.c:60-71` | forbid | `*tn_setup_rexx_init` | CreateMsgPort | EVET |
| `src/setup/setup_rexx.c:81-83` | forbid | `tn_setup_rexx_cleanup` | — | — |
| `src/setup/stack_detect.c:347-349` | forbid | `tn_stack_detect_all` | — | — |
| `src/setup/stack_detect.c:792-795` | forbid | `tn_stack_request_quit` | — | — |
| `src/setup/stack_detect.c:813-823` | forbid | `tn_stack_request_quit` | — | — |
| `src/setup/stack_detect.c:840-845` | forbid | `tn_stack_request_quit` | — | — |
| `src/setup/stack_detect.c:850-855` | forbid | `tn_stack_request_quit` | — | — |
| `src/task/daemon_main.c:81-94` | forbid | `tn_task_alive` | — | — |
| `src/task/daemon_main.c:136-140` | forbid | `tn_reap_dead_clients` | — | — |
| `src/task/daemon_main.c:222-230` | forbid | `tn_ipc_port_drain_delete` | — | — |
| `src/task/daemon_main.c:664-714` | forbid | `tn_task_real_main` | — | — |
| `src/usergroup/ug_init.c:150-152` | forbid | `ug_lib_expunge` | — | — |
