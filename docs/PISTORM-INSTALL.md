# A500 + PiStorm kurulum notu (sahibin hedef makinesi)

Bu not, **Amiga 500 + PiStorm (Emu68, 68ec020 sınıfı çekirdek) + WiFiPi**
kurulumu için tolunnet 1.2.0-rc1 paketinin adımlarını ve beklentileri
özetler. Doğrulama profilleri: `ci/.repro-pistorm.uae` (NIC'siz temiz çıkış)
ve `ci/.repro-pistorm-nic.uae` (NIC'li sağlıklı çalışma).

## 1. WiFiPi tarafı (önce bu hazır olmalı)

1. `DEVS:Networks/wifipi.device` kurulu (Emu68 sürüm paketiyle gelir).
2. RPi tarafındaki b43 firmware klasörü SD kartta (`Devs/Firmware/...`).
3. `ENVARC:Sys/Wireless.prefs` içinde SSID/WPA bilgisi (WirelessManager
   ile veya sihirbazın WiFi sayfasından oluşturulur).
4. WifiPi ALPHA yazılımdır (v0.3.x); sürüm notlarına göz atın.

tolunnet bu sürücüyle tam uyumlu çalışacak şekilde doğrulandı:
hook'lar SANA-II register konvansiyonuyla (a0/a1/d0) çağrılıyor ve
tolunnet'in saf-assembly bayt-kopya hook'ları hizalama varsaymı
yapmadığından Address Error sınıfı (#80000003) kapalıdır (TNET-139).

## 2. tolunnet kurulumu

1. `tolunnet-1.2.0-rc1.lha` içeriğini istediğiniz yere açın
   (ör. `Work:tolunnet`), `Install_Tolunnet` ile kurulumu bitirin.
2. **TolunnetSetup** sihirbazını çalıştırın: donanım sayfasında
   `wifipi.device` görünür (DEVS:Networks altında taranır).
3. WiFi sayfası, ağınıza bağlanmak için WirelessManager yapılandırmasını
   yazar; Adres sayfasında DHCP önerilir.
4. Bitir deyince sihirbaz `DEVS:tolunnet.config` yazar:
   `DEVICE=wifipi.device UNIT=0 DHCP=YES ...`

## 3. Beklentiler ve normal davranışlar

- **Açılışta ağ hazır değilse**: WiFi ilişkilendirmesi birkaç saniye
  sürebilir; S2_ONLINE hemen döner, DHCP bu sürede yeniden denemeye
  devam eder. İlk DHCP denemesi boş geçebilir — normaldir.
- **Sürücü hiç yoksa** (ör. wifipi kurulmadan çalıştırıldı): daemon
  `OpenDevice(...) failed` + `SANA-II open failed` yazıp **temiz çıkar**
  (Guru yok); kurulumdan sonra yeniden başlatın.
- **Konsol çıktısı**: `LOG=` anahtarı tanımlıysa daemon çıktısını o
  dosyaya yazar (konsol sessiz kalır — hata değil).

## 4. Sorun görünce

- `TolunnetStatus` ile soket/arayüz durumuna bakın.
- Daemon logu (`LOG=` ile) ve varsa MuForce/enforcer kaydını saklayın.
- Guru alırsanız (beklenmiyor): Guru'daki görev adını ve hex kodu
  bildirin — `docs/history/TOLUNNET-FIX-guru-80000003.md` yöntemiyle
  teşhis için kullanılacak.

## 5. AmigaOS 3.9 / hazır dağıtım (AmiKit vb.) üzerinde

Popüler dağıtımlar (AmiKit for PiStorm gibi) çoğu zaman **kendi TCP/IP
yığınıyla** (Roadshow/Miami) gelir. Bunlar `bsdsocket.library`'yi LIBS:'te
taşır; tolunnet kendi kütüphanesini kurarken çakışma doğar. Kurulumda:

1. Installer, sihirbazı artık **konsol penceresiyle** başlatır — açılış
   aşamaları (`opening libraries`, `scanning installed stacks`,
   `scanning network hardware`, `opening screen`, `building gadgets`)
   konsola yazılır. Bir Software Failure görürseniz **konsoldaki son
   satırın + Guru kutusunun Task satırının yakın çekimini** fotoğraflayıp
   gönderin: hatayı tam satırına kadar izleriz.
2. Sihirbazın donanım sayfası mevcut yığını saptayıp devre dışı bırakmayı
   önerir (Stacks sayfası). Kabul edin; istereniz `TolunnetStatus` ile
   hangi yığının hizmet verdiğini doğrulayın.
3. Dağıtımın kendi ağ başlatma satırları (User-Startup içinde
   `addnetinterface` vb.) sihirbaz tarafından saptanır ve kapatılabilir.

### Beklenmeyen Guru alırsan (test prosedürü, ~3 dakika)

1. Bir Shell açın ve şunu çalıştırın: `SYS:Prefs/TolunnetSetup`
2. Konsoldaki son aşama satırını not edin.
3. Guru çıkarsa: Guru kutusunun **Task satırını yakın çekim**
   fotoğraflayın (görev adı tırnak içindedir).
4. `TolunnetStatus` çıktısını da ekleyin.

Bu üç veri (son aşama + görev adı + status) teşhisi tek turda bitirir.
