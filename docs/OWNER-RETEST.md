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
