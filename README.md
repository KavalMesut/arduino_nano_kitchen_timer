# ⏳Mutfak Geri Sayım Alarmı (Arduino Nano & 18650 Li-ion Şarjlı Sürüm)

Bu proje, mutfakta çay demleme, yemek pişirme veya zaman gerektiren diğer hassas işler için tasarlanmış, döner enkoder (rotary encoder) kontrollü, TM1637 4 haneli 7 segment göstergeli, **18650 2200 mAh Li-ion Pil & LX-LCBST (TP4056 + DC-DC 5V Yükseltici)** şarj sistemli ve emniyetli MOSFET sürücülü 4 Ohm 5W güçlü hoparlör barındıran üst segment bir mutfak geri sayım alarmıdır.

Tüm sistem, taşınabilir şarjlı yapısı ve donanımsal/yazılımsal emniyet tedbirleriyle yüksek ses seviyesinde dahi kilitlenmemesi veya sıfırlanmaması için optimize edilmiştir.

---

## 🛠️ Kullanılan Malzemeler (BOM Listesi)

1. **Arduino Nano (CH340 veya Orijinal)**: Sistem durum makinesini (State Machine) yöneten ana kontrolcü.
2. **EC11 Bare Rotary Encoder (Döner Enkoder)**: Entegre mekanik butonu olan, süre ayarlamak ve komut vermek için kullanılan 360 derece dönebilen seçici.
3. **TM1637 4 Haneli 7 Segment Gösterge Modülü**: Zamanı `DAKİKA : SANİYE` (MM:SS) formatında ve batarya durumunu (`b-85`, `Lo`) gösteren ekran.
4. **4 Ohm 5W Güçlü Hoparlör**: Mutfaktaki gürültüde dahi çok net ve yüksek sesle duyulabilen kaliteli hoparlör.
5. **D4184 Dual MOSFET Anahtar Modülü**: Hoparlörü doğrudan Arduino pininden çekilemeyecek yüksek akımla, 5V hattından güvenle ve yüksek sesle tetiklemek için kullanılan mosfet kartı.
6. **18650 2200 mAh Li-ion Pil**: Şarj edilebilir ana güç kaynağı.
7. **LX-LCBST (TP4056 + DC-DC 5V Step-Up Yükseltici Modülü)**: Type-C şarj girişli, korumalı ve voltajı 5.0V'a yükseltilmiş şarj devresi.
8. **Sürgülü Güç Anahtarı (ON/OFF Switch)**: Cihazı tamamen açıp kapatan anahtar (LX-LCBST VO+ çıkışına seri bağlı).
9. **Emniyet Direnci**: Hoparlör bobinini ve MOSFET'i yüksek akımdan korumak için kullanılan **10 Ohm 2W metal film koruma direnci**.
10. **A0 & A1 Koruma Dirençleri**: A0 (pil okuma) ve A1 (şarj algılama) pinleri öncesinde parazit beslemeyi ve pin hasarını önleyen **10kΩ seri koruma dirençleri**.
10. **Özel Tasarım 3D Kutu**: İçten içe 80mm x 140mm (Dıştan dışa 84mm x 144mm x 30mm) boyutlarında, baklava/diamond desenli hoparlör ızgaralı, Type-C şarj soketli ve M3 pirinç inserts (ısı kovanı) uyumlu gövde.

---

## ⚠️ KRİTİK TRİMPOT KALİBRASYON UYARISI

**LX-LCBST modülünün `VO+` / `VO-` çıkışlarını Arduino veya diğer kartlara bağlamadan önce;**
Modül üzerindeki minyatür trimpotu çevirerek `VO+` çıkış voltajını bir multimetre (avometre) ile ölçüp **tam 5.0 Volt'a** ayarlamalısınız! Aksi takdirde modül yüksek voltaj (9V/12V) vererek Arduino'ya zarar verebilir.

---

## 📐 3D Kutu ve Mekanik Özellikler

Kutu tasarımı OpenSCAD yardımıyla tamamen parametrik ve sağlam olarak geliştirilmiştir:
* **Gövde (`kutu_govde.stl`)**: Solid ön yüzey, 50mm baklava desenli hoparlör ızgarası, Type-C şarj soket yuvası, sağ yanda güç anahtarı yuvası ve köşelere tamamen gömülerek mukavemeti artırılmış **M3 pirinç ısı kovanı (heat-set inserts)** yuvaları barındırır.
* **Kapak (`kutu_kapak.stl`)**: Solid arka kapak, havşa başlı M3 vidaların sıfıra sıfır oturması için tasarlanmış delik yapıları.

---

## 🔋 Batarya & Şarj Ekran Özellikleri (TM1637)

* **2 Saniye Basılı Tutma (Batarya Kontrolü)**: Butona 2 saniye basılı tuttuğunuzda ekranda **tam 1 saniye boyunca** anlık batarya yüzdesi gösterilir (Örn: **`b-85`** = %85 pil), 1 saniye sonra ekran eski durumuna döner.
* **Düşük Batarya Uyarısı (%10 Altında `Lo` Uyarısı)**: Batarya seviyesi **%10'un altına** düştüğünde, her yeni zaman ayarlama girişiminde (enkoder her çevrildiğinde) ekranda **`Lo`** yazısı yanıp söner ve kısa bir **uyarı bip sesi** verilir.
* **Şarj Girişi Algılama**: Type-C takıldığında Arduino şarj modunu otomatik algılar.

---

## ⚡ Donanım Bağlantı Tablosu

| Kaynak Modül | Pin / Bağlantı | Hedef Pin | İşlev |
| :--- | :--- | :--- | :--- |
| **18650 Li-ion Pil** | Artı (+) / B+ | LX-LCBST B+ / **Arduino A0** | Batarya Besleme & Voltaj Okuma |
| **18650 Li-ion Pil** | Eksi (-) / B- | LX-LCBST B- | Batarya Şasi |
| **LX-LCBST Modülü** | VO+ (5V Yükseltici) | ON/OFF Anahtarı $\rightarrow$ **Ortak +5V** | 5.0V Sistem Besleme |
| **LX-LCBST Modülü** | VO- (GND) | **Ortak GND Rayı** | Sistem Şasi |
| **LX-LCBST Modülü** | IN+ (Type-C 5V) | 10k Direnç $\rightarrow$ **Arduino A1** | Şarj Girişi Algılama |
| **EC11 Enkoder** | Out A (3'lü Grup) | **Arduino D2** | Kesme 0 Sinyali |
| **EC11 Enkoder** | Out B (3'lü Grup) | **Arduino D3** | Kesme 1 Sinyali |
| **EC11 Enkoder** | Switch (2'li Grup) | **Arduino D4** | Buton Girişi |
| **TM1637 Ekran** | CLK / DIO | **Arduino D5 / D6** | Ekran Saat & Veri Hattı |
| **D4184 MOSFET** | (PWM) + Sinyal | **Arduino D9** | Timer 1 Donanımsal PWM Sinyali |

---

## 🚀 Derleme ve Kurulum

1. Bilgisayarınıza **Arduino IDE** kurun.
2. Arduino IDE içerisindeki **Kütüphane Yöneticisi (Library Manager)** menüsünden **TM1637** (Avishay Orpaz) kütüphanesini kurun.
3. [kitchen_timer.ino](file:///c:/Users/kaval/OneDrive/Desktop/geri%20say%C4%B1m%20nano/kitchen_timer/kitchen_timer.ino) dosyasını Arduino IDE ile açın.
4. Kart tipini **Arduino Nano**, işlemciyi ise klon kartlar için **ATmega328P (Old Bootloader)** seçin.
5. Uygun COM portunu belirleyip kodu kartınıza yükleyin.

---

## 📁 Proje Dosyaları Yapısı

* 📁 **`kitchen_timer/`**: Arduino kaynak dosyalarının bulunduğu klasör.
  * 📄 **`kitchen_timer.ino`**: Durum makinesi, kesme tabanlı enkoder okuma, MOSFET alarm ve A0/A1 batarya kodları.
* 📄 **`kutu_tasarimi.scad`**: OpenSCAD kutu tasarımının kaynak parametrik dosyası.
* 📄 **`kutu_govde.stl`**: Kutu gövdesinin 3D baskıya hazır modeli.
* 📄 **`kutu_kapak.stl`**: Arka kapağın 3D baskıya hazır modeli.
* 📄 **`baglanti_semasi.pdf` / `.tex`**: Detaylı donanım bağlantı kılavuzu ve şemaları.
* 📄 **`README.md`**: Proje açıklama ve kurulum dokümanı.
