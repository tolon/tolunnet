# AmiNetXDuo'dan alınacaklar — prompt serisi

Sıra ile, her dosya tek prompt; agent'a **yalnızca o dosya** verilir (bu README'yi verme). Ortak kurallar her prompt'un 2. satırında, aynı metin. Prompt'a asla 'yapılanlar/geçmiş' bölümü ekleme; bittiğinde dosyaya dokunma, sıradakine geç.

Her prompt'tan sonra sen kontrol et (agent'ın sözüne değil): `git log -1`, bench dizini `SUMMARY.txt`, `STOP-REPORT.md` var mı. Üçü tutmuyorsa sıradakine geçme. Referans klon: `E:\amiga\AmiNetXDuo` (github.com/tinic/AmiNetXDuo, MIT — alınan her dosyanın başına MIT bildirimi + `THIRD_PARTY_LICENSES.md` satırı).

| # | Prompt | Çıktı |
|---|---|---|
| 01 | bench: `bsdsocket_emu=false` + bsdsocktest | `ci/bench.sh` N/142 satırı |
| 02 | compat tablosunu üret, `docs/compat.md` sil | `src/lib/lib_compat_table.gen.md` tek gerçek |
| 03 | `check-forbid.sh` statik kapı | `scripts/check-forbid.sh`, `docs/FORBID.md` |
| 04 | 68000 donması teşhis: sağlık sayaçları | `netstat -h`, TNET-113 |
| 05 | `getaddrinfo` ailesi | 4 vektör BUILT |
| 06 | aminet-survey: gerçek kullanım taraması | `docs/aminet-survey/lvo-usage.tsv` |
| 07a | wizard: Roadshow/Genesis dahil tespit + bilgi ekranı | `stack_detect.c` |
| 07b | wizard: devre dışı bırak (yedekli) + geri al | `RESTORE` |
| 08 | Roadshow config içe aktarma | `DEVS:NetInterfaces/*`, `DEVS:Internet/*` okuyucu |
| 09 | AutoIP + mDNS | lwIP bayrakları + config anahtarları |
| 10 | `usergroup.library` | `src/usergroup/`, LIBS: kurulumu |
| 11 | IPv6 açma | `LWIP_IPV6 1`, `sockaddr_in6` |
| 12 | küçük bellek profili | `lwipopts/lwipopts-small.h`, runtime seçim |
| 13 | `ARCHIVE-MANIFEST` + `GAPS.md` + README "Why" | paketleme kapısı, eksik listesi |
| 14 | `mbuf_*` vektörleri pbuf üstünde | 11 vektör BUILT |
| 15 | BPF vektörleri + `NetCapture` (pcap) | tcpdump benzeri |
| 16 | `anxnet.device` paketleme (port değil) | isteğe bağlı SANA-II sürücü |
| 17 | `TCP:` DOS handler | `L:tcp-handler` |
| 18a–g | komutlar, her biri ayrı prompt | sntp, nslookup/host, hostname, arp, telnet, nc, iperf |
| 19 | LAN httpd + parolalı tarayıcı Shell | `/status`, `/shell` |
| 20 | `tls.library` (AmiNetXDuo ABI) mbedTLS üstünde | 68000'de TLS |
| 21 | Dropbear ssh/scp (68020+ build) | `C:ssh`, `C:scp` |
| 22 | CPU'ya göre asm checksum/memcpy | `LWIP_CHKSUM` dispatch |

Bağımlılıklar: 14→15, 02→06, 05→11, 18(iperf)→22 ölçümü, 20 kripto fazları ayrı prompt'larla devam eder. Alınmayan tek şey ThreadX baton/`netx_call` katmanı (lwIP `NO_SYS` ile anlamsız).
