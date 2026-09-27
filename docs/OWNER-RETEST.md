# Sahibin yeniden test prosedürü — DIAG derlemesi (TNET-139, TN-bugtrack-2 madde 1)

Bu derleme, çökme anında **Guru çıkmadan önce** `RAM:tolunnet-crash.log`
yazar: PC, SR, hata adresi, yazmaçlar, kod bölümlerinin taban adresleri ve
son 16 log satırı. Guru yine görünür (normal davranış korunur) ama artık
kaynağı kayıtlı olur.

## Kurulum (3 komut)

1. `DEVS:tolunnet.config` içine şu satırı ekle (hangi editörle olursa):
   `DIAG=YES`
   (Config örneği: `DEVICE=wifipi.device`, `UNIT=0`, `DHCP=YES`,
   `LOG=RAM:tolunnet-diag.log` önerilir — adım günlükleri oraya akar.)
2. Yeniden başlat veya `Run NIL: C:tolunnet` çalıştır.
3. Çökme olursa (Guru göründüğünde fareye basmadan önce/sonra fark etmez):
   - `RAM:tolunnet-crash.log` dosyasını kopyala (örn. `Copy RAM:tolunnet-crash.log Work:`)
   - `RAM:tolunnet-diag.log` dosyasını da kopyala.

## Bana gönderilecekler

- `tolunnet-crash.log` (PC/hata adresi/yazmaçlar/bölüm tabanları)
- `tolunnet-diag.log` (başlangıç adımları: config yüklemesi, timer,
  `s2: OpenDevice`, `s2: DEVICEQUERY`, `s2: GETSTATIONADDRESS`,
  `s2: CONFIGINTERFACE`, `s2: ONLINE`, lwIP init, kütüphane kaydı)

PC eksi en yakın bölüm tabanı = `objdump` kaydırması; tek turda satır
seviyesinde teşhis.

## Notlar

- `DIAG=YES` yalnızca daemon'ı etkiler; performans üzerinde ihmal edilebilir
  (16 satırlık sabit ring + satır başına Flush).
- Çökme olmazsa bile ağ çalışmıyorsa `tolunnet-diag.log` yine gönderilsin:
  hangi S2 adımının takıldığını gösterir.
- Bench tarafında aynı mod `TN_DIAG=1 ./ci/bench.sh` ile
  `pistorm-68000` profiliyle koşulabilir (a2065, wifipi yerine vekil).

---

# rc2 yeniden test listesi (1.2.0-rc2, CLOSE §D.14)

Yukarıdaki DIAG prosedürü aynen geçerli (DIAG=YES). Ek olarak bu sürümde
şunları sıralı dene ve sonuçları (çıktı metni / crash-diag logları) gönder:

1. `TolunnetControl VERSION` → `tolunnet 1.2.0-rc2` basmalı.
2. Temel trafik: `ping aminet.net 5`, `wget http://aminet.net/recent.txt`,
   `nslookup aminet.net`, `traceroute aminet.net 10`.
3. Yeni komutlar: `route` (boş tablo + `route DEFAULT GATEWAY 10.0.2.1`
   gibi bir ekleme + `route` göster), `arp` (tablo dolmalı),
   `CheckNetConfig DEVS:tolunnet.config` (satır numaralı rapor),
   `Online` / `Offline` (link düşüp kalkmalı).
4. **TX havuzu (önemli — varsayılan kapalı):** config'e `TX_QUEUE=4`
   ekle ve yeniden başlat; logda `s2: TX pool: 4 slot(s)` görünecek.
   Sonra: ping/wget 5 dk sorunsuz mu? Link DOWN/UP fırtınası var mı?
   (Emüle sürücüde vardı; wifipi.device'da olup olmadığı bu testle
   belli olur ve varsayılan kararı verilir.) Sorun görürsen TX_QUEUE=0
   yapıp devam et ve bana logu gönder.
5. **iperf (gerçek kablo sayıları):** Amiga'da `iperf SERVER PORT 5001`,
   ev bilgisayarında `iperf -c <amiga-ip> -p 5001 -t 10` ve tersi
   (Amiga client). Her iki yönün KB/s değerini gönder — TX havuzunun
   gerçek kazancı bu sayıyla ölçülecek.
6. 1 saat dayanıklılık: `ping <gateway> INTERVAL=0 COUNT=2000` +
   30 sn'de bir `wget` döngüsü; başta ve sonda `AvailMem` değerleri
   (fark ≤ 8 KB olmalı).
7. Çökme olursa: `RAM:tolunnet-crash.log` + `RAM:tolunnet-diag.log`.
8. **PiStorm RTG HD Ekranında TolunnetSetup Doğrulaması (TN-note-AG4):**
   Bench ortamındaki HDF Picasso96 içermediğinden (`SKIP (P96 not in bench image)`),
   metrik tabanlı sihirbaz yerleşiminin HD çözünürlükte (800×600, 1024×768 veya üzeri)
   ve sistem yazı tipinde düzgün ölçeklendiğini doğrulamak için şu 5 sayfanın fotoğrafını çekip gönderin:
   - Sayfa 1 (Replace Stacks): Liste ve onay kutusu
   - Sayfa 2 (Hardware): Arayüz seçimi ve butonlar
   - Sayfa 3 (WiFi Setup): Ağ listesi, SSID, Passphrase ve Show kutusu
   - Sayfa 4 (IP Address): 2 sütunlu IP/Mask/GW/DNS/MTU/Host/Domain alanları ve Gelişmiş butonu (hem DHCP hem Static modunda)
   - Sayfa 5 (Test & Finish): 5 maddelik denetim listesi, butonlar ve onay kutuları
   (Her sayfada pencerenin ekranı ortaladığı, taşma olmadığı ve tüm kontrollerin okunabilir olduğu doğrulanacaktır.)

---

# Installer manuel prosedürü (10a-3 madde 4)

Bench, Commodore Installer'ı asla çalıştıramaz: Installer bir GUI
programıdır (welcome penceresi, her `(message)` ve novice açılış
ekranı tıklama bekler). Bu yüzden installer'ı otomatik test yerine şu
manuel adımlarla doğrula — önce WinUAE'de, sonra gerçek donanımda.

Ön koşul: **temiz** bir Workbench 3.x HDF/diski (eski tolunnet kurulumu
olmadan) ve sürüm paketinin (LhA) `WORK:tolunnet/` gibi bir çekmeceye
açılmış olması.

1. **Paketi aç** ve paket çekmecesi içine gir.
2. **Novice kurulum:** `Install_Tolunnet`'e çift tıkla (veya çekmece
   içinde bir shell'den `Installer Install_Tolunnet.script`
   çalıştır). Hiçbir soru sorulmadan bittiğini doğrula: tek etkileşim
   welcome sayfasıdır, başka hiçbir şey girdi beklemez ve kapanış
   mesajı ek tıklama olmadan gelir.
3. **Expert kurulum (ikinci, aynı paket kopyası üzerinde ya da 7.
   adımdan sonra):** `Installer Install_Tolunnet.script EXPERT`
   çalıştır. `SYS:Prefs/TolunnetSetup` sorusuna No (varsayılan) de.
   Diğer her şey varsayılan kalsın.
4. **SYS:C denetimi:** Shell'de `List SYS:C` çalıştır. SYS:C altında
   komut adlarını taşıyan **alt çekmece olmamalı** (orada bir
   `tolunnet` *çekmecesi* olması, eski bozuk script'in koştuğunu
   gösterir) ve komut binary'leri (`tolunnet`, `ping`, `TolunnetGet`,
   `hostname`, …) doğrudan SYS:C içinde düz dosya olarak durmalı.
5. **Config denetimi:** `Type DEVS:tolunnet.config` — temiz sistemde
   var olmalı ve `DEVICE=ethernet.device`, `UNIT=0`, `DHCP=YES`
   yazmalı. Önceden var olan bir config'in varsa içeriğine
   dokunulmamış olmalı.
6. **User-Startup denetimi:** `Type S:User-Startup` — sona eklenmiş
   tam olarak bir blok olmalı: `Stack 32768` ve
   `Run <NIL: >NIL: C:tolunnet`. Bir de
   `S:User-Startup.tolunnet-bak` yedeği var olmalı.
7. **Undo script denetimi:** `Type S:tolunnet-undo` — içinde
   `S:User-Startup.tolunnet-bak`,
   `LIBS:bsdsocket.library.pre-tolunnet` yedeği ve
   `DEVS:tolunnet.config` geçmeli.
8. **Yeniden başlat** ve stack'in geldiğini doğrula: `ping 127.0.0.1`
   cevap vermeli (kablolu SANA-II sürücü varsa gerçek arayüz de DHCP
   ile gelmeli).
9. **Geri alma:** `Execute S:tolunnet-undo` çalıştır ve eski durumun
   döndüğünü kontrol et: User-Startup bloğu gitmiş (yedekten geri
   gelmiş), `DEVS:tolunnet.config` silinmiş, yapılmışsa
   `LIBS:bsdsocket.library.pre-tolunnet` yedeği
   `LIBS:bsdsocket.library` olarak geri gelmiş ve yedek silinmiş
   olmalı. Bir kez daha yeniden başlat; `ping 127.0.0.1` artık
   başarısız olmalı (stack başlamıyor).

Sonucu (adım adım geçti/kaldı, WinUAE mi donanım mı olduğunu) sürüm
notlarına yaz.

---

# Ortak yaşam (coexistence) denetimi (10b madde 4)

tolunnet'in artık diğer stack'lerin SYS:C araçlarının YEDEĞİNİ aldığını
ve geri getirdiğini gerçek bir Roadshow kurulumuyla doğrula:

1. **Roadshow demo kur:** Temiz bir WB 3.x görüntüsüne Aminet'teki
   Roadshow demo sürümünü kur ve boot edip çalıştığını gör
   (`ShowNetStatus` Roadshow'un kendi aracı olmalı).
2. **tolunnet'i kur:** Aynı sisteme tolunnet installer'ını çalıştır
   (novice). Kurulum sonunda sihirbaz açılacak; tamamla ya da kapat.
3. **Yedek denetimi:** `List SYS:Storage/tolunnet-backup/C` —
   Roadshow'un araçları burada durmalı: özellikle `ping`, `route`,
   `Online`, `Offline`, `NetShutdown`, `AddNetInterface`,
   `AddNetRoute`, `DeleteNetRoute`, `ConfigureNetInterface`,
   `GetNetStatus`, `ShowNetStatus` (tolunnet'in bunların üzerine
   yazdığı isimler).
4. **Geri alma:** `Execute S:tolunnet-undo` çalıştır, yeniden boot et.
   Roadshow yeniden çalışmalı: `ping 127.0.0.1` Roadshow'un ping'i ile
   cevap vermeli ve `ShowNetStatus` Roadshow'un çıktısını vermeli
   (tolunnet'in user-startup satırı gitmiş, DEVS:tolunnet.config
   silinmiş, yedek dizin kaldırılmış olmalı).
5. **ethernet.device olmayan makinede novice kurulum:** Sürücüsü
   `ethernet.device` olmayan bir yapıda (PiStorm, A2065, PLIP…)
   novice kurulum yap — kurulumun sonunda SYS:Prefs/TolunnetSetup
   sihirbazının KENDİ KENDİNE açılması gerekir (artık
   DEVICE=ethernet.device placeholder config yazılmıyor); sihirbazdan
   gerçek donanımı seçip ağı ayağa kaldır.

Sonuçları (adım adım geçti/kaldı + `List SYS:Storage/tolunnet-backup/C`
çıktısı) sürüm notlarına ekle.


