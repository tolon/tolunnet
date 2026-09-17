# ANX-08 — Roadshow / AmiTCP / Miami ayarlarını içe aktarma
Kurallar (değişmez): (1) yalnız bu dosyadaki iş; başka dosyaya, başka TNET'e dokunma. (2) Tek commit; iş bitmeden ara commit yok, iş bittikten sonra bekleyen değişiklik yok. (3) Kanıt = `ci/bench.sh` log dizini + host test çıktısı; kendi ifaden kanıt değildir. (4) DUR kuralı: bench üst üste 2 kez kırmızı → dur; 4 saat içinde commit yoksa → dur; durunca `STOP-REPORT.md` yaz (ne yapıldı, ne kırmızı, son yeşil commit) ve bitir. (5) Yeşil kalması gereken taban: mevcut core testleri iki profilde (a1200, 68000) — biri düşerse bu iş kırmızıdır. Salt okuma; eski dosyalar değişmez.
1. `src/setup/import_config.c`: kaynak öncelik sırası Roadshow → AmiTCP → Miami, ilk bulunan kazanır, hepsi wizard "Adres" sayfasına **öneri** olarak (kullanıcı görür, değiştirir).
 - Roadshow: `DEVS:NetInterfaces/*` (her dosya bir arayüz; `DEVICE=`, `UNIT=`, `ADDRESS=`/`DHCP`, `NETMASK=`, `GATEWAY=`... anahtarlar Roadshow SDK `doc/` ve kurulu sistemdeki örnek dosyadan; değerleri dosyadan al, örnek dosyayı `docs/probes/`'a kopyala), `DEVS:Internet/name_resolution` (`nameserver`, `domain`), `DEVS:Internet/hosts`, `ENV:HostName`.
 - AmiTCP: `AmiTCP:db/interfaces`, `AmiTCP:db/netdb` (`NAMESERVER`, `DOMAIN`, `HOST`), `AmiTCP:bin/startnet` içindeki `ifconfig`/`route` satırları (mevcut import kodu; `pr_WindowPtr=-1` altında).
 - Miami: `Miami:MiamiDx.config` ikili — **yalnızca** aygıt adı/unit için `ENV:` ve `Miami:` altındaki metin dosyaları; ikiliyi ayrıştırma (kapsam dışı, "Miami ayarları elle" mesajı).
2. Ayrıştırıcı `src/common/`'a taşınır, host testi `tests/host/test_import_config.c` (örnek dosyalar `tests/host/fixtures/roadshow/`, `amitcp/`).
3. Wizard adres sayfası: "Ayarlar <kaynak>tan alındı" satırı; DHCP bulunduysa DHCP seçili.
4. `DEVS:tolunnet.config` içine `IMPORTED_FROM=<kaynak>` bilgi anahtarı (daemon yok sayar).
Kabul: host test yeşil, bench yeşil, commit `setup: import Roadshow/AmiTCP settings as wizard defaults`.
