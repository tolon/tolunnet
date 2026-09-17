# tolunnet — AmiNetXDuo karşılaştırması ve eksik listesi

Tarih: 2026-09-09. Kaynaklar: tolunnet çalışma ağacı (109 commit, son `06b0aae`), AmiNetXDuo v0.26.5 klonu (`README`, `SECURITY.md`, `docs/*`, `tests/`, `install/`).

## 0. Önce dürüst bir düzeltme

AmiNetXDuo "yeni başlamış" bir proje değil. 47 sürüm (0.14.0 → 0.26.5), ~277K satır C, bsdsocktest 142/142, gerçek donanımda (A3000/060 + X-Surf-100) doğrulanmış, 26 aşamalı CI, host tarafında 127 test + ASan/UBSan fuzzer, Enforcer/MungWall temiz. README'ye göre kod Claude Opus 5 tarafından insan yönetiminde yazılmış — yani senin yöntemine çok benzer bir yöntemle, ama daha uzun süre ve daha sıkı kapılarla. Lisansı MIT; ödünç almak serbest (atıf şart).

tolunnet'in tek başına farklı olduğu yer **lwIP tabanı** (NetX Duo yerine) ve **GPL**. Bunlar zayıflık değil; ama "boşluğu dolduran tek stack" iddiası artık doğru değil — README'deki "Why tolunnet?" bölümü güncellenmeli.

## 1. Eksikler — öncelik sırasıyla

### A. Ölçülebilir uyumluluk kanıtı yok (en kritik)

| | tolunnet | AmiNetXDuo |
|---|---|---|
| bsdsocktest | hiç yok (depoda tek referans yok) | 142/142, `tests/conformance/` içinde otomatik |
| Kendi conformance | `SocketConformance` 34 test, emülatör | 127 host test + Amiga tarafı harness'lar, `tests/HARNESSES` indeksi |
| Gerçek donanım | yok (STATUS "EMULATOR-PROVEN") | A3000 + X-Surf-100 |
| Oracle karşılaştırma | `compat.md`: "Nothing below is probe-verified against a Roadshow oracle yet" | Roadshow 1.15 = 138/142 referans |

**Yapılacak:** bsdsocktest'i (tbdye, GPLv3 — lisans uyumlu) `ci/` bench'ine ekle, her bench çıktısına `N/142` yaz. Bench `.uae` dosyalarına açıkça `bsdsocket_emu=false` koy (şu an satır yok, varsayılana güveniyorsun; birisi WinUAE'de açarsa testin tolunnet'i değil UAE'yi ölçer). Sende üç gerçek Amiga var — en az birinde Ethernet ile bir "hardware bench" koş ve STATUS'a "HARDWARE-PROVEN" satırı ekle.

### B. `compat.md` ile README çelişiyor

`compat.md` (5 Eylül) `bind/listen/accept/shutdown` için **BROKEN (TNET-077)** diyor; README ve STOP-REPORT (`tc_listen_accept_loopback` geçiyor) düzeltildiğini söylüyor. Aynı depoda iki farklı gerçek olmaz. AmiNetXDuo'da bu tablo (`docs/CONFORMANCE.md`, `aminet-survey`) **üretilir**, elle yazılmaz. `gen_lvo_table.py` zaten var; compat tablosunu da oradan + test sonuçlarından üret, elle yazılan `compat.md`'yi kaldır.

### C. Vektör kapsamı: 61/139 uygulanmış

AmiNetXDuo: 100/139 uygulanmış ve **hangi vektörlerin gerçek yazılımlar tarafından kullanıldığını ölçmüş** (Aminet taraması: 143 vektörden 83'ü). tolunnet stub'ları arasında gerçek uygulamaların çağırdığı şunlar var:

- `getaddrinfo / freeaddrinfo / getnameinfo / gai_strerror` (-804…-822) — AmiSSL 5, yeni derlenen curl/wget portları, OS4'ten geri taşınan her şey bunu kullanır. **Tier 1 sayılmalı.**
- `GetNetworkStatistics`, `ObtainInterfaceList/QueryInterfaceTagList` — netstat/ifconfig benzeri araçlar ve Roadshow'un `ShowNetStatus` tipi araçları.
- `AddDomainNameServer` ailesi — DHCP istemcileri ve bazı PPP araçları.
- `mbuf_*` ve `ObtainRoadshowData` AmiNetXDuo'da da yok; bunları "asla" diye kapatmak makul.

**Yapılacak:** AmiNetXDuo'nun `aminet-survey` yöntemini (Aminet arşivlerinden LVO çağrı taraması) ödünç al; tolunnet'in stub listesini "kullanılıyor / kullanılmıyor" sütunuyla yeniden sınıflandır; `getaddrinfo` ailesini önce yap.

### D. Tek profil `-m68000`: hız kaybı

tolunnet tek binary `-m68000 -msoft-float`. AmiNetXDuo checksum/memcpy/crypto için `src/net68k` + `src/crypto68k`: `.S.in` şablonundan CPU başına asm üretir, çalışma anında `AttnFlags` ile seçer. 68030/040/060'ta belirgin fark. lwIP'de `LWIP_CHKSUM` makrosunu kendi asm'ine bağlayabilirsin; küçük ama ölçülebilir kazanç, tek binary korunur.

### E. Test altyapısı

| | tolunnet | AmiNetXDuo |
|---|---|---|
| Host testi | 20 dosya, `mock_lwip.c` | Gerçek Amiga kaynaklarını Linux'ta derleyen shim, 127 test |
| Fuzz | yok | ASan/UBSan fuzzer'lar (paket ayrıştırıcı, DNS, DHCP) |
| Bellek denetimi | MuForce (bench'te 0 hit) | Enforcer + MungWall, CI kapısı |
| Kapılar | `make test-host`, `python-checks` | ~30 `check-*.sh` (ALIGNMENT, FORBID, REENTRANCY, ARCHIVE-MANIFEST…) |
| Emülatör | WinUAE (Windows host) | Amiberry + WinUAE runner'ları, Linux CI |

**Yapılacak (ucuz olanlar):** (1) `check-forbid.sh` benzeri: `Forbid()`/`Permit()` eşleşmesi ve Forbid içinde `Wait/Delay/DoIO` yasağı — tolunnet'te STOP-REPORT'taki 68000 donmaları için ilk şüpheli sınıf bu. (2) `check-alignment`: 68000'de `ETH_PAD_SIZE` ile çözdüğün adres hatası sınıfını statik yakala (yapılarda `__attribute__((packed))`/tek baytlık alan sonrası `LONG`). (3) DHCP/DNS ayrıştırıcıları için libFuzzer hedefi — lwIP'nin kendi fuzz hedefleri var (`test/fuzz/`), doğrudan bağlanır.

### F. Kararlılık: 68000 donması açık

STOP-REPORT: 68000 profilinde `tc_connect_refused` civarında nondeterministik sistem donması, W4'ten önce de var (`c7c3c6d`). Bu, "release" demeden önce kapanması gereken tek gerçek engel. Öneri: ayrı TNET satırı aç, WinUAE debugger ile PC/fault adresi kaydet; Forbid/Disable süresi ve `timer.device` ticker'ın SANA-II tamamlama yolunda `Signal()` yarışını incele. AmiNetXDuo'nun `docs/REENTRANCY.md` ve `FORBID.md` belgeleri tam bu sınıf için kural seti — oku, kuralları kendi `docs/`'una uyarla.

### G. Kurulum ve birlikte yaşama

- Roadshow **tespit ediliyor** (`stack_detect.c` 718: `NetShutdown`), ama sadece kapatma yolunda; sihirbazın kullanıcıya "sende Roadshow var" demesi ve **devre dışı bırak / bırak / iptal** seçeneği (senin kararın) henüz yok. Genesis sadece `.info` üzerinden.
- AmiNetXDuo kuralları: `AmiTCP:` assign'ına asla dokunma, kendi dosyalarını `ActivateAmiNetXDuo` gibi özel çekmeceye koy, Roadshow config düzenini (`DEVS:NetInterfaces/*`, `DEVS:Internet/*`) **okuyabil** (var olan IP/DNS ayarlarını içe aktar). tolunnet `DEVS:tolunnet.config` kullanıyor; Roadshow/Miami ayarlarını içe aktarma sadece AmiTCP için var.
- Geri alma: devre dışı bırakırken `User-Startup`/`Startup-Sequence` satırlarını yorumla, yedeği `S:tolunnet-backup/` altına al, "Eski stack'i geri getir" düğmesi.
- `install/ARCHIVE-MANIFEST` benzeri: LhA içeriğini listeyle karşılaştıran kapı (eksik/fazla dosya = kırmızı).

### H. Protokol kapsamı

| | tolunnet | AmiNetXDuo |
|---|---|---|
| IPv6 | kapalı (`LWIP_IPV6 0`), tasarım dokümanı var | açık, RA/DHCPv6 |
| AutoIP (169.254) | yok | var |
| mDNS | yok | var (`.local` çözümleme) |
| TLS | yok | `tls.library` (bsdsocket üstü, stack bağımsız) |
| usergroup.library | yok | 39 vektör |
| Sürücü | sadece istemci (SANA-II) | kendi `anxnet.device` (lance/ne2000/el3) |
| ARexx | wizard'da `setup_rexx.c` | `AMITCP` portu (AmiTCP uyumlu komutlar) |
| TCP: handler | yok | var |
| Komutlar | ping, ifconfig, netstat, wget/curl, Prefs, Status | ~35 (httpd, ssh/scp, ftp, telnet…) |

Hedef kitle "evde çalışmayan stack'i olan hobici" olduğu için sıralama: **AutoIP** (DHCP yoksa yine adres alsın; lwIP'de bir bayrak), **mDNS** (lwIP `apps/mdns`, bayrak), **`usergroup.library`** (AmiTCP döneminden bazı araçlar `OpenLibrary` eder ve yoksa açılmaz — AmiNetXDuo'nunki MIT, alınabilir), **IPv6** (tasarımın hazır; `LWIP_IPV6 1` + `sockaddr_in6` marshaling). TLS ve ssh kapsam dışı kalabilir; AmiSSL zaten var.

### I. Bellek ve hedef makine

`MEM_SIZE 96K`, `PBUF_POOL_SIZE 32`, `TCP_WND 8*MSS`: 68000 + 2 MB için ağır olabilir; AmiNetXDuo'da "micro build yok" GAPS'te kendi eksiği olarak yazıyor. tolunnet için fırsat: `lwipopts/lwipopts-small.h` ile A600/A500+ profili (`MEM_SIZE 32K`, pencere 4*MSS), çalışma anında `AvailMem`'e göre seçim. Bu senin gerçek farkın olabilir — AmiNetXDuo'nun boş bıraktığı alan.

### J. Dokümantasyon

AmiNetXDuo'da olup tolunnet'te olmayan: `ALIGNMENT.md`, `ALLOCATIONS.md` (her ayırma nerede, kim serbest bırakır), `REENTRANCY.md`, `FORBID.md`, `GAPS.md` (kendi eksiklerinin dürüst listesi), `SECURITY.md`. tolunnet'te dokümanların çoğu prompt/audit geçmişi (`docs/history/`); bunlar değerli ama "kullanıcı ve katkıcı" dokümanı değil. `README.guide` iyi. Eksik: **GAPS.md** (bu dosya onun başlangıcı olabilir) ve `CONTRIBUTING`.

## 2. Ne almalı, ne almamalı

**Al (MIT, atıfla):** `usergroup.library` kaynağı; `tools/gen_vectors.py` yaklaşımı (yoğun LVO tablosu + `bsd_enosys`) ile kendi `gen_lvo_table.py`'ını kıyasla; `aminet-survey` betiği; `check-forbid`/`check-alignment` betikleri; `docs/REENTRANCY.md` ve `FORBID.md` kuralları (dokümanı değil, kuralları); Installer'daki tespit/birlikte yaşama mantığı; Roadshow config okuyucu.

**Alma:** NetX Duo'ya özgü her şey (mbuf emülasyonu, ThreadX baton), kendi sürücü katmanı (SANA-II istemcisi olarak kalmak doğru, sürücü yazmak kapsam dışı), httpd/ssh.

## 3. Önerilen sıra (her biri tek prompt)

1. `.uae`'lere `bsdsocket_emu=false`; bsdsocktest'i bench'e ekle; ilk `N/142` sayısını STATUS'a yaz.
2. `compat.md`'yi üretilen tabloyla değiştir (README ile tek gerçek).
3. 68000 donması: ayrı TNET, debugger kaydı, Forbid/Signal denetimi; `check-forbid.sh`.
4. `getaddrinfo` ailesi.
5. Wizard: Roadshow/Genesis dahil tespit → bilgilendir → devre dışı/bırak/iptal + geri al (önceki prompt taslağı).
6. AutoIP + mDNS bayrakları.
7. Gerçek donanım bench'i (senin Amigalarından biri) → "HARDWARE-PROVEN".
8. `usergroup.library` (AmiNetXDuo'dan), IPv6 açma.
9. Küçük bellek profili.
10. `GAPS.md`, `CONTRIBUTING.md`, README "Why" bölümü güncellemesi (AmiNetXDuo'yu dürüstçe an; farkın: lwIP, GPL, küçük bellek, sihirbaz).
