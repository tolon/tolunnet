# Tolunnet Bugtrack — tam durum raporu (2026-09-12)

> Kaynak: `ISSUES.md` (125 satır) + bu oturumun kanıt dizinleri ve gözlemleri.
> Kısaltmalar: WP-green = çift-profil (a1200+68000) yeşil bench. Tüm dizinler
> `docs/bench-logs/` altındadır.

## 0. Özet

| Kategori | Adet | Durum |
|---|---|---|
| Toplam kayıt | 125 | 96 RESOLVED, 2 APPROVED, **27 OPEN + 1 REOPENED** |
| Sahibin makinesinde açık çökme | **1** | TNET-139 (REOPENED) — tanı paketi sahibin testinde |
| Emülatör bench'ini kilitleyen aile | **1** | TNET-115 (6 olgu) — ANX-04 teşhis adayı |
| Aralıklı (eşik: 2 üst üste) | 1 | TNET-113 |
| bsdsocktest işlev açıkları | 25 | TNET-114…138 (102/142 taban) |
| Statik kapı bulgusu (ertelenmiş) | 1 | TNET-140 |

Son yeşil taban: `20260912-002745-5f1086c` (35/35 ×2 her iki profil, donmasız).

---

## 1. TNET-139 — sahibin makinesinde `#80000003` (REOPENED, en yüksek öncelik)

**Belirti.** A500 + PiStorm + OS 3.9 + AmiKit sınıfı dağıtım: kurulum
sonrası `Software Failure 80000003` (fotoğraf kanıtı; Task satırı çözülemedi)
ve bir "Cannot open ..." istek kutusu. PiStorm = 68ec020 sınıfı olduğundan
bu Address Error **tek adrese komut-atlaması** (odd PC fetch) anlamına gelir —
veri hizalaması 020'de sessiz geçer; dolayısıyla kalan şüpheli sınıf
**çarpık işlev göstericisi** ya da dağıtıma özgü bir bileşen etkileşimi.

**Kapatılan kısmı (sınıf düzeltmesi, `5f71f10`).** SANA-II hook'ları saf-ASM
bayt-kopya (yalnız d0 karalanır, a0/a1 8 bayt yığınla korunur); tüm
istemci-tamponu sockaddr/in_addr/hostent erişimleri bayt-bazlı yardımcılara
çevrildi (28 cast ihlali temizlendi); `-Werror=cast-align` + `make
align-check` kapıları eklendi; bench 68000 profiline Fast RAM eklendi.
WP-green: a1200 `20260910-235753-5f71f10`, 68000 `20260911-001150-5f71f10`.

**Neden açıldı.** Sınıf düzeltmesi sahibin çökmesini kesmedi. Emülasyon
matrisi (KS 2.04/2.05/3.1 × Fast/bogo × statik/DHCP/wizard × stripped)
sağlıklı kaldı; kalan fark AmiKit ortamı (dağıtımın kendi yığını, devasa
User-Startup, OS 3.9 bileşenleri). GUI'siz AmiKit repro'su hazırlandı
(`ci/.repro-amikit2.uae`, DH1'de dağıtım kopyası + atamalar) ancak sahibin
etkin tarayıcı kullanımı GUI sürüşünü engelledi.

**Yapılan tanı altyapısı (`5f1086c`, WP-green `20260912-002745`).**
Sihirbaza aşama günlüğü (`startup: opening libraries → scanning installed
stacks → scanning network hardware → opening screen → building gadgets`);
installer sihirbazı artık konsolla açar; aygıt istemi `wifipi.device` ile
başlar; `docs/PISTORM-INSTALL.md` §5'te 3 dakikalık test prosedürü.

**Sonraki adım (girdiye göre sıralı).**
1. Sahipten: son aşama satırı + Guru Task adı (yakın çekim) + varsa
   `TolunnetStatus` çıktısı.
2. Aşamaya göre kod bölgesi: `scanning installed stacks` →
   `src/setup/stack_detect.c` (AmiKit User-Startup/NetInterfaces ayrıştırması);
   `scanning network hardware` → `hw_detect.c` (DEVS:Networks taraması);
   `opening screen` → GadTools/intuition (OS 3.9 karışımı).
3. AmiKit repro'da `il 8` kurulu yeniden üretim → PC → `objdump` eşlemesi.
4. Guru Task adı `TolunnetSetup` değil de `Installer`/başka bir dağıtım
   bileşeni çıkarsa: etkileşim noktası installer'ın `(execute ...)`
   satırları — konsollu başlatma zaten riski azalttı.

**Gözlem (ayrı kayda değer).** Kurulum sırasındaki "Cannot open ..." istek
kutusunun metni fotoğrafta okunamadı; AUX:/eksik birim kalıbı şüphesi var.
Kutu metmi tekrar okunabilir olursa ayrı bir TNET olarak açılır.

---

## 2. TNET-115 — bsdsocktest #32 ailesi kilitlenmeler (6 olgu)

**Belirti.** (a) 68000: bsdsocktest test #32 (scatter-gather) sırasında test
süreci askıda; daemon görev günlüğü tam soket yaratımı ortasında kesilir
("socket() -> fd 1" — 143 satırlık log). (b) a1200: **yeni varyant** —
bsdsocktest 142/142 tamamlanır, kilitlenme wizard/RECONFIG aşamasından
sonra, conformance çıktısı UAE dosya-sistemine flush olamadan.

**Olgu zaman çizelgesi.**
| # | Dizin | Profil | Nokta |
|---|---|---|---|
| 1 | 20260909-165110-989eebf | 68000 | #32, 41 satır |
| 2 | 20260909-201118-9bca042 | 68000 | aynı |
| 3 | 20260909-202632-e4039fa | 68000 | aynı |
| 4 | 20260910-235753-5f71f10 | 68000 | aynı (TNET-139 düzeltmeli build) |
| 5-6 | 20260911-104254-202c92e | 68000 + a1200 | 68000 klasik; a1200 wizard-sonrası |

**Elenenler.** SANA-II hook sınıfı (olgu 4, düzeltmeli build'de yinelendi);
ANX-03 denetim build'i (ikili 5f71f10 ile bayt-özdeş, yineledi) → kod
tarafından tetiklenmiyor; makine reseti değil (görev listesinde Workbench
canlı, tek daemon açılışı).

**Kalan hipotezler.** (1) ipc_dispatch'in ertelenmiş-yanıt yollarında kayıp
uyanma yarışı (pending_connect/pending_accept); (2) wizard ARexx +
daemon yeniden başlatma (TNET-059/060) yarışı; (3) #32'nin çok-iovec
istemcisi ile daemon'un aynı belleği farklı hızlarda tükettiği bir pencere.
a1200 varyantı (2)'yi öne çıkarıyor.

**Sonraki adım (ANX-04).** Kilitlenme anında debugger'da `Tt` (görev+PC
dökümü) — altyapı hazır (`ci/debugger/`, `-conlogfile`); repro:
bsdsocktest döngüsü + `il 8`/anlık kırılma. Sağlık sayaçları (ANX-04'ün
özgün kapsamı) kilitlenme öncesi sondurumu görünür kılar.

---

## 3. TNET-113 — aralıklı tc_multicast_join (a1200)

Yalnız a1200'de "multicast loopback not ready in 2s". Sahip kuralı: **üst
üste 2 kırmızı** eşiği; sayım ISSUES'ta. WP-green koşularda görülmedi;
20260912-002745 a1200 çevrimlerinde yeşildi. Eylem: eşik tetiklenene dek
izleme (her bench koşusunda otomatik sayılıyor).

## 4. TNET-140 — Forbid altında tn_logf (ertelenmiş, düşük risk)

`tn_lib_destroy()` refcount kontrolü sırasında `Forbid()` altında
`tn_logf` (lib_init.c:52). ANX-03 kuralı gereği bulgu olarak kayıtlı,
kod değişmedi. Kapı `scripts/check-forbid-known.txt` üzerinden affediyor.
Kapatma koşulu: teardown yoluna dokunulduğu ilk işte log'u Permit sonrasına
taşımak.

---

## 5. bsdsocktest işlev açıkları — TNET-114…138 (25 satır, 102/142 taban)

Ailelere göre gruplama (aynı kök neden muhtemelen tek düzeltmeyi paylaşır):

| Aile | Satırlar (test no) | Muhtemel mekanizma |
|---|---|---|
| MSG_OOB teslimi | TNET-114 (#27), TNET-116 (#35), TNET-117 (#64) | OOB işaretli veri ayrı kanalda taşınmıyor; exceptfds hiç uyanmıyor |
| SO_EVENTMASK/GetSocketEvents | TNET-122–127 (#79,81,83,84,85,86) | Olay maskesi/sinyal altyapısı: kayıt + tüketim semantiği eksik (TNET-067'nin uzantısı) |
| errno/h_errno göstericileri | TNET-119,120,132–135 (#76,77,122,123,124,125) | SBTC_ERRNOLONGPTR/SetErrnoPtr ailesi: görev-başına gösterici tablosu yok |
| dtablesize/Dup2Socket | TNET-121,130,131,136–138 (#78,115,117,127,128,131) | Tanımlayıcı tablosu boyutlandırma + çoğaltma semantiği |
| WaitSelect/sinyal | TNET-118 (#66), TNET-128 (#87) | Amiga sinyal kesintisi sırasında dönüş değeri/errno |
| Scatter-gather | TNET-115 (#32) | a1200'de işlevsel yanlış veri (donmayla aynı test) |
| netdb | TNET-129 (#101) | getnetbyname tablosu yok |

**Not.** 20260912-002745 a1200 koşusunda #27 (TNET-114) ekstra kırmızı çıktı
(101/142): bu satır a1200'de **oynak** — kayıt aralıklı kategorisine
düşmeli, bench'i etkilemiyor (ANX-01 kuralı).

## 6. Bu oturumda kapatılan işler (özeti)

- TNET-139 sınıf düzeltmesi (`5f71f10`) + kanıt (`3acebc0`).
- ANX-03 check-forbid kapısı + FORBID.md + TNET-140 bulgusu (`202c92e`,
  kanıt `f7a0435`); STOP-REPORT kapanışı (`526d7b3`).
- PiStorm doğrulama profilleri (`31c377d`); sahibi tanı paketi (`5f1086c`,
  kanıt `0a898a9`).

## 7. Bilinen-davranış kayıtları (bug değil, belgelik)

- `Run >file cmd` altında çocuğun konsol çıktısı görünmez (daemon
  görev-günlüğüne bakın). Kurulum artık sihirbazı konsolla açtığı için
  sahibi tarafında görünür.
- a2065/uaenet S2_ONEVENT anında tamamlanır → DOWN/UP çalkantısı;
  TNET-109 fırtına savunması 3 bölüm sonrası izlemeyi kapatır (log
  gürültüsü dışında etkisiz).

## 8. Önerilen sıra

1. TNET-139: sahibin 3 dakikalık testi → fotoğraf → bölge eşlemesi → repro
   + PC (engelleyici, sahibin kullanımını kilitliyor).
2. TNET-115/ANX-04: canlı kilitlenme yakalama (debugger altyapısı hazır;
   bench güvenilirliğini de geri kazandırır).
3. bsdsocktest aileleri büyük-etkiden küçüğe: SO_EVENTMASK ailesi (6 satır)
   → errno-gösterici ailesi (6) → dtablesize/Dup2 (6) → OOB (3) → netdb (1).
4. TNET-113 izleme; TNET-140 ilk temasla kapanır.
