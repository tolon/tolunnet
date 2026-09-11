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
