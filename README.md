# ⏳Mutfak Geri Sayım Alarmı (Arduino Nano & MOSFET Sürücülü)

Bu proje, mutfakta çay demleme, yemek pişirme veya zaman gerektiren diğer hassas işler için tasarlanmış, döner enkoder (rotary encoder) kontrollü, TM1637 4 haneli 7 segment göstergeli ve emniyetli MOSFET sürücülü 4 Ohm 5W güçlü hoparlör barındıran üst segment bir mutfak geri sayım alarmıdır.

Tüm sistem, harici **5V 3A DC adaptör** ile paralel beslenerek kararlı çalışması, yüksek ses seviyesinde dahi kilitlenmemesi veya sıfırlanmaması için donanımsal ve yazılımsal güvenlik önlemleriyle optimize edilmiştir.

---

## 🛠️ Kullanılan Malzemeler (BOM Listesi)

1. **Arduino Nano (CH340 veya Orijinal)**: Sistem durum makinesini (State Machine) yöneten ana kontrolcü.
2. **EC11 Bare Rotary Encoder (Döner Enkoder)**: Entegre mekanik butonu olan, süre ayarlamak ve komut vermek için kullanılan 360 derece dönebilen seçici.
3. **TM1637 4 Haneli 7 Segment Gösterge Modülü**: Zamanı `DAKİKA : SANİYE` (MM:SS) formatında gösteren, parlaklığı duruma göre otomatik ayarlanan ekran.
4. **4 Ohm 5W Güçlü Hoparlör**: Mutfaktaki gürültüde dahi çok net ve yüksek sesle duyulabilen kaliteli hoparlör.
5. **D4184 Dual MOSFET Anahtar Modülü**: Hoparlörü doğrudan Arduino pininden çekilemeyecek yüksek akımla, 5V hattından güvenle ve yüksek sesle tetiklemek için kullanılan mosfet kartı.
6. **5V 3A Kaliteli Harici Güç Adaptörü**: Sistemin güç kaynağı.
7. **Emniyet Direnci Kümesi (Direnç Hilesi)**: Hoparlör bobinini ve MOSFET'i yüksek akımdan korumak için 4 adet 47 Ohm 1/4W direncin birbirine paralel lehimlenmesiyle elde edilen **~11.7 Ohm 1W direnç grubu**.
8. **Özel Tasarım 3D Kutu**: 60mm x 80mm x 30mm boyutlarında, baklava/diamond desenli hoparlör ızgaralı, Type-C besleme soketli ve M3 pirinç inserts (ısı kovanı) uyumlu gövde.

---

## 📐 3D Kutu ve Mekanik Özellikler

Kutu tasarımı OpenSCAD yardımıyla tamamen parametrik ve sağlam olarak geliştirilmiştir:
* **Gövde (`kutu_govde.stl`)**: Solid (dolu) ön yüzey, 50mm baklava desenli hoparlör ızgarası, Type-C şarj soket yuvası, sağ yanda güç anahtarı yuvası ve köşelere tamamen gömülerek mukavemeti artırılmış **M3 pirinç ısı kovanı (heat-set inserts)** yuvaları barındırır.
* **Kapak (`kutu_kapak.stl`)**: Solid arka kapak, havşa başlı M3 vidaların sıfıra sıfır oturması için tasarlanmış delik yapıları.
* **OpenSCAD Kaynağı (`kutu_tasarimi.scad`)**: Minkowski köşeli non-manifold hataları yerine, `$fn=180` silindirleri birleştiren pürüzsüz `hull()` tekniğiyle optimize edilmiştir.

---

## ⚡ Donanım Bağlantı Şeması

Yüksek akım çeken hoparlörün Arduino Nano'yu kilitlememesi için 5V 3A hattı modüllere **paralel** dağıtılmıştır.

### 1. Arduino ve Kontrol Birimleri Bağlantıları

```
            +---------------------------------------------+
            |               5V 3A ADAPTÖR                 |
            +----------[ +5V ]-----------[ GND ]----------+
                         |                 |
        +----------------+                 +----------------+
        |                                                   |
        |  +---------------------------------------------+  |
        |  |                 ARDUINO NANO                |  |
        |  +---------------------------------------------+  |
        +->[ 5V ]                                 [ GND ]<-+
           [ D2 ]---+                             [ D9  ]------+
           [ D3 ]---|--+                          [ D5  ]----+ |
           [ D4 ]---|--|--+                       [ D6  ]--+ | |
           +--------|--|--|------------------------------+  | | |
                    |  |  |                                 | | |
       +------------+  |  |                                 | | |
       |   +-----------+  |                                 | | |
       |   |   +----------+                                 | | |
       |   |   |                                            | | |
   +---|---|---|-----------------+                  +-------|--|-|-----------------+
   | [OutA][GND][OutB]  [SW][GND] |                  |      [CLK][DIO][VCC][GND]     |
   |   (3'lü Pin Grubu) (2'li G.) |                  |                               |
   |       EC11 BARE ENCODER     |                  |         TM1637 DISPLAY        |
   +--------|------------|--------+                  +------------------|----|-------+
            |            |                                              |    |
            +------------|----------------------------------------------+    |
                         +---------------------------------------------------+
```

### 2. MOSFET Hoparlör Sürücü ve Emniyet Direnci Bağlantıları

```
                                5V 3A ADAPTÖR (+)
                                       |
                                       +--------------------+
                                       |                    |
   +-----------------------------------|--------------------+-----------------------+
   |                                   v                                            |
   |                        D4184 MOSFET TERMİNALLERİ                               |
   |                                                                                |
   |   [ Çıkış+ ] (Üst Sağ) --------------------------[ Hoparlör + ]                |
   |                                                                                |
   |   [ Çıkış- ] (Üst Sol) ----[ 4'lü Paralel Direnç ]--[ Hoparlör - ]             |
   |                                                                                |
   |   [  DC+   ] (Alt Sağ) -------------------------- 5V 3A Adaptör +5V             |
   |                                                                                |
   |   [  DC-   ] (Alt Sol)  ------------------------- 5V 3A Adaptör GND             |
   |                                                                                |
   +--------------------------------------------------------------------------------+
```
* **Arduino Kontrol Bağlantısı**: D9 pini MOSFET kartının `SIG` girişine bağlanırken, `GND` pini de Arduino şasisiyle birleştirilir.

---

## 🔒 Elektriksel Emniyet Tedbirleri

### 1. Donanımsal Koruma (Direnç Hilesi)
5V besleme altında 4 Ohm hoparlör doğrudan çalıştırılırsa anlık **1.25 Amper** akım çeker. Hoparlörün bobininin aşırı ısınıp yanmasını önlemek ve sesi dengeli seviyede tutmak için seri bir direnç gereklidir. 
Elinizde en az 1W gücünde direnç yoksa:
* **4 adet 47 Ohm 1/4W standart direnç paralel bağlanmıştır.** 
* Bu sayede dirençlerin güçleri birleşerek **1W güç kapasitesi** elde edilmiş ve direnç değeri mutfak için ideal ses seviyesi sunan **~11.7 Ohm** değerine çekilmiştir.

### 2. Yazılımsal Koruma (DC Emniyeti ve Donanımsal PWM)
Arduino pini yanlışlıkla `HIGH` konumda takılı kalırsa hoparlör bobini sürekli akım altında kalarak yanabilir. Bunu önlemek ve mükemmel ses seviyesi ayarlayabilmek için:
* Ses üretimi **Arduino Timer 1 donanımsal Fast PWM (Pin D9) üzerinden donanımsal olarak** yapılır. 
* Ses çalınmadığı bekleme durumlarında pin donanımsal ve yazılımsal olarak tamamen kapatılıp `LOW` konumuna çekilerek hoparlör hattı elektriksel olarak izole edilir.

---

## 🧠 Gelişmiş Yazılım ve Durum Makinesi (State Machine)

Yazılım, mutfakta pratik kullanım sunmak amacıyla kararlı bir **durum makinesi (State Machine)** üzerine kurulmuştur.

### 1. Durumlar
* **STANDBY (Bekleme)**: Cihaz aktiftir. Ekran parlaklığı güç tasarrufu ve loş ışıklar için **en düşük seviyededir (1)**. Ortadaki iki nokta (colon) yavaşça nefes alma animasyonu yapar. En son kaydedilen başarılı süre ekranda hazır gelir.
* **ADJUSTING (Ayar)**: Enkoder döndürüldüğünde bu moda girilir. Parlaklık orta seviyeye (4) gelir. 5 saniye boyunca enkoder çevrilmezse ayar modu sonlanır ve Standby'a dönülür.
* **COUNTDOWN (Geri Sayım)**: Geri sayım aktiftir. Ekran parlaklığı maksimumdur (7). İki nokta her saniye flaşör gibi yanıp söner.
* **PAUSED (Duraklama)**: Kısa basış artık doğrudan duraklatma yapmak yerine sayacı durdurup başa (hedef süreye) sardığı için bu durum doğrudan kullanılmaz. Yalnızca geri sayım sırasında ses ayarına girilip çıkıldığında durumun bozulmaması için arka planda kullanılır.
* **ALARM (Alarm)**: Süre bittiğinde devreye girer. Ekranda dönüşümlü olarak `00:00` ve `End ` (Son) ibaresi yanıp söner. Çift tonlu ritmik polis sireni çalar.
* **VOLUME_SETTING (Ses Seviyesi Ayarı)**: 1 ile 10 arasında ses şiddetini ayarlamayı sağlayan premium mod.

### 2. Donanımsal PWM Tabanlı Ses Seviyesi Kontrolü [YENİ]
Hoparlör bir MOSFET üzerinden sürüldüğü için standart analog sinyal ile ses seviyesi ayarlanamaz. Bunun yerine **Timer 1 donanımsal PWM frekans doluluk oranı (duty cycle)** kontrolü kullanılmıştır:
* **Hassas Kontrol**: Ses seviyesi 1-10 arasında ayarlanabilir. 
* **Logaritmik/Kuadratik Eğri**: İnsan kulağının işitme yapısına uyumlu olması için ses seviyesi kuadratik eğriyle kapı doluluk oranına dönüştürülür ($OCR1A = TOP \times V^2 / 200$). Level 1 için çok loş/hafif bir pıtırtı sesi verirken, Level 10'da %50 doluluk oranıyla tam 5W güçte çalar.
* **Kalıcı Hafıza**: En son ayarladığınız ses seviyesi Arduino'nun kalıcı hafızasına (EEPROM) kaydedilir ve her açılışta otomatik yüklenir.

### 3. Dinamik Süre Ayarı (Sayısal Adım Kontrolü)
Mutfakta uzun süreleri ayarlarken yüzlerce kez enkoder döndürmeyi önlemek adına enkoder adımları dinamikleştirilmiştir:
* **30 dakika ve altında**: Süre **1'er dakika** aralıklarla değişir (örn: 01:00, 02:00 ... 29:00, 30:00).
* **30 dakikadan sonra**: Süre **5'er dakika** aralıklarla değişir (örn: 30:00, 35:00, 40:00 ... 95:00, cap 99:00).
* **Kusursuz Simetri**: CW/CCW geçişleri tam simetriktir; `29:00`'dan artırınca `30:00` ve ardından `35:00` olurken, `35:00`'dan geri alındığında `30:00` ve ardından `29:00` olur.

### 4. Filtreleme ve Kararlılık
* **Enkoder Debounce (30ms)**: Klon enkoderlerde görülen ark gürültülerini en kararlı biçimde süzmek için donanımsal kesme (Interrupt) filtresi **30ms** olarak optimize edilmiştir.
* **Buton Debounce (50ms)**: Enkoder butonundaki basmama veya çift tıklama hataları 50ms'lik dijital filtre ile giderilmiştir.
* **Teşhis LED'i (Diagnostic)**: Donanımsal bağlantıyı test etmek için butona basıldığında Arduino üzerindeki dahili **L LED'i** anında yanar.
* **Uzun Basış Kontrolü (1 saniye) [GÜNCELLENDİ]**: Herhangi bir durumdayken (Bekleme, Ayarlama, Geri Sayım veya Duraklama) butona 1 saniye basılı tutulduğunda **Ses Seviyesi Ayar Moduna** girilir. Ekranda **`U- 5`** (seviye 5) veya **`U-10`** (seviye 10) gibi bir ibare belirir. Enkoder sağa/sola çevrilerek ses ayarlanır, her adımda yeni seviyede geri bildirim sesi duyulur. Kısa basıldığında veya 5 saniye boş bırakıldığında değer hafızaya kaydedilip sistem kaldığı yerden (örneğin sayım devam ediyorsa kaldığı saniyeden) aynen devam eder. 2 saniyelik çok uzun basış tamamen kaldırılmıştır.

---

## 🚀 Derleme ve Kurulum

1. Bilgisayarınıza **Arduino IDE** kurun.
2. Arduino IDE içerisindeki **Kütüphane Yöneticisi (Library Manager)** menüsünden **TM1637** (Avishay Orpaz) kütüphanesini aratarak kurun.
3. [kitchen_timer.ino](file:///c:/Users/kaval/OneDrive/Desktop/geri%20say%C4%B1m%20nano/kitchen_timer/kitchen_timer.ino) dosyasını Arduino IDE ile açın.
4. Kart tipini **Arduino Nano**, işlemciyi ise klon kartlar için genellikle **ATmega328P (Old Bootloader)** seçin.
5. Uygun COM portunu belirleyip kodu kartınıza yükleyin.

---

## 📁 Proje Dosyaları Yapısı

* 📁 **`kitchen_timer/`**: Arduino kaynak dosyalarının bulunduğu klasör.
  * 📄 **`kitchen_timer.ino`**: Durum makinesi, kesme tabanlı enkoder okuma ve MOSFET alarm kodları.
* 📄 **`kutu_tasarimi.scad`**: OpenSCAD kutu tasarımının kaynak parametrik dosyası.
* 📄 **`kutu_govde.stl`**: Kutu gövdesinin 3D baskıya hazır modeli.
* 📄 **`kutu_kapak.stl`**: Arka kapağın 3D baskıya hazır modeli.
* 📄 **`baglanti_semasi.pdf` / `.svg` / `.html` / `.tex`**: Detaylı donanım bağlantı kılavuzu ve şemaları.
* 📄 **`README.md`**: Proje açıklama ve kurulum dokümanı.
