# ANX-18 — komut takımı: `sntp`, `nslookup`/`host`, `hostname`, `arp`, `telnet`, `nc`, `iperf`
Kurallar: **her komut ayrı commit** (7 commit, sırayla; biri kırmızıysa orada dur, `STOP-REPORT.md`). Referans: AmiNetXDuo `src/tools/{sntp,nslookup,host,hostname,arp,telnet,nc,iperf,iperfcore,iperfwire}.c` (MIT; her biri saf bsdsocket istemcisi — büyük ölçüde doğrudan derlenir; `netstack_weak.c` bağımlılığı varsa stub'la). Tümü `ReadArgs` şablonlu, `-m68000`, `SYS:C/`.
1. `sntp`: `SERVER/A,SET/S,OFFSET/S` — RFC 4330 tek UDP; `SET` ile `timer.device` `TR_SETSYSTIME` + `battclock.resource`; config anahtarı `NTP_SERVER=` (daemon başlangıçta `NTP=YES` ise bir kez sorar, ANX-09 Advanced… penceresine alan).
2. `nslookup`/`host`: A, PTR, `TYPE/K` (MX/TXT — lwIP `dns_gethostbyname` yalnız A; MX/TXT için kendi UDP/53 sorgusu, `src/common/dnswire.c`, host testi `test_dnswire.c` fuzz girdileriyle: uzun etiket, sıkıştırma döngüsü).
3. `hostname`: göster / `SET/K` → `ENV:HostName` + `DEVS:tolunnet.config HOSTNAME=` + daemon `RECONFIG`.
4. `arp`: tablo (`etharp` lwIP tablosu — yeni IPC op `TN_OP_GETARP`, versiyonlu yapı), `DELETE/K`, `ADD/K` (`etharp_add_static_entry`).
5. `telnet`: RFC 854 minimum (IAC DO/DONT/WILL/WONT yanıtla, `ECHO`/`SGA` kabul, `TTYPE` "amiga"), CON: raw mod, Ctrl-] çıkış.
6. `nc`: `HOST,PORT/N,LISTEN/S,UDP/S,EXEC/K` — stdin↔socket köprü, `WaitSelect` ile CON: sinyali.
7. `iperf`: iperf2 tel protokolü (`iperfwire.c`), `CLIENT/K,SERVER/S,TIME/N,PORT/N` — bench'te `tc_iperf_loopback` throughput sayısı `SUMMARY.txt`'e (performans satırı; TNET-106/107 için ölçüm aracı).
Her komut: `README.guide` bölümü, `docs/compat.md` yerine `docs/COMMANDS.md` tablosu (script üretmez, elle; komut adı | şablon | kaynak). Kabul: 7 commit, bench yeşil, `make package` LhA'da 7 yeni ikili + manifest (ANX-13).
