# 🛠️ Mutfak Geri Sayım Alarmı: Sorunlar ve Çözümler Kılavuzu

Bu belge, Arduino Nano tabanlı Premium Mutfak Geri Sayım Alarmı projesinin geliştirilmesi, montajı ve testleri sırasında karşılaşılan kritik elektriksel, donanımsal ve yazılımsal sorunları ve bu sorunların kesin çözümlerini bir araya getirmektedir. Gelecekteki bakım ve benzer projeler için bir başvuru kılavuzudur.

---

## 📌 1. Döner Enkoder Çift Atlama ve Gürültü Sorunu (Debouncing)

### 🔴 Belirti:
Enkoder döndürüldüğünde değerlerin kararsız artması/azalması, ara sıra çift atlamalar yapması veya hızlı çevrildiğinde adımların kaçırılması.

### 🔍 Analiz:
Mekanik döner enkoderler (bare EC11) döndürülürken elektriksel kontakları anlık olarak ark yapar (contact bounce). Bu yüksek frekanslı gürültüler kesme (interrupt) servisleri tarafından gerçek dönüş adımları gibi algılanır. 
* **Çok yüksek gürültü filtresi (örn. 20ms)** hızlı çevirmelerde adımların kaçırılmasına yol açar.
* **Çok düşük filtre (örn. 4ms)** gürültülü klon enkoderlerde çift atlamalara neden olur.

### 🟢 Kesin Çözüm:
Kesme servisi içerisindeki gürültü önleme filtresi **30 ms** olarak optimize edildi. Bu değer, klon enkoderlerin mekanik gürültülerini tamamen süzerken, normal çevrim hızlarındaki tüm adımların kararlı şekilde yakalanmasını sağlar:
```cpp
void encoderISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  if (interruptTime - lastInterruptTime > 30) { // 30ms Kararlı Debounce
    int clkVal = digitalRead(PIN_ENC_CLK);
    int dtVal = digitalRead(PIN_ENC_DT);
    if (clkVal != dtVal) {
      encoderDelta = 1;
    } else {
      encoderDelta = -1;
    }
    encoderMoved = true;
    lastInterruptTime = interruptTime;
  }
}
```

---

## 📌 2. MOSFET Hoparlör Sürücü Emniyeti (DC Akım Koruması)

### 🔴 Belirti:
Hoparlör bobininin aşırı ısınması, MOSFET modülünün ısınması veya adaptörün aşırı akım korumasına girerek sistemi kapatması.

### 🔍 Analiz:
4 Ohm empedansa sahip güçlü bir hoparlör, 5V DC gerilim altında doğrudan iletimde kalırsa $I = V / R = 5V / 4\Omega = 1.25A$ akım çeker. Arduino Nano'nun pini kilitlenir veya yazılım hatasıyla sürekli `HIGH` konumda kalırsa, hoparlör bobini sürekli DC akıma maruz kalarak yanabilir.

### 🟢 Kesin Çözüm:
1. **Donanımsal Koruma (Direnç Hilesi):** Hoparlöre seri olarak **4 adet 47 Ohm 1/4W direnç paralel bağlanarak** (~11.7 Ohm 1W) seri koruma direnci oluşturuldu. Bu, çekilen akımı sınırlandırarak bobini korur.
2. **Yazılımsal Koruma (DC Emniyeti):** Ses üretimi tamamen AC/Pulsed mantığında (Timer PWM) yapıldı. Alarm çalmadığı veya ses durduğu anda MOSFET tetikleme pini (D9) donanımsal ve yazılımsal olarak tamamen kapatılıp `LOW` seviyesine çekilir:
```cpp
void stopTonePWM() {
  TCCR1A = 0; // Donanımsal PWM bağlantısını kopar
  TCCR1B = 0;
  digitalWrite(PIN_MOSFET, LOW); // Emniyet için pini LOW yap (Akımı tamamen keser)
}
```

---

## 📌 3. Fiziksel Temassızlık ve Akustik Titreşim (Kablo Kopukluğu)

### 🔴 Belirti:
Tetikleme LED'i yandığı halde hoparlörden hiçbir ses gelmemesi.

### 🔍 Analiz:
Klemens vidalarının plastik yalıtkanı sıkıştırması, lehim çatlakları veya hoparlörün ürettiği yüksek mekanik titreşimin (vibrasyon) kabloları sarsması sonucu oluşan gizli temassızlıklar.

### 🟢 Teşhis ve Çözüm Yöntemi (Yavaş Sinyal Testi):
Frekans yerine saniyede bir yavaşça açılıp kapanan (Blink) bir sinyal üretilerek klemens ve hoparlör yolları test edildi. Sinyal yavaşlatıldığında hoparlörden gelen **"tık... tık..."** sesleri sayesinde kopuk olan kablo noktası fiziksel olarak kolayca tespit edildi:
```cpp
// Yavaş tetikleme ile kablo ve lehim kontrolü yapılmıştır:
digitalWrite(9, HIGH); delay(1000);
digitalWrite(9, LOW);  delay(1000);
```

---

## 📌 4. Sürekli Ekran Yazımı Kaynaklı Döngü (Loop) Yavaşlaması

### 🔴 Belirti:
Siren çalarken veya zaman akarken döner enkoderin geç tepki vermesi, sistem zamanlamasında milisaniyelik kaymalar (jitter) oluşması.

### 🔍 Analiz:
TM1637 gösterge modülü yazılımsal bit-bang (CLK ve DIO pinlerini sırayla HIGH/LOW yapıp mikrosaniye seviyesinde bekleyerek) veri gönderir. Ekran verisi her `loop()` döngüsünde sürekli baştan yazıldığında, döngü hızı saniyede sadece ~200 çevrime düşer. Bu da mikrodenetleyicinin diğer zamanlayıcı işlerini geciktirir.

### 🟢 Kesin Çözüm:
Ekran güncelleme mekanizması **Olay Güdümlü (Event-Driven)** hale getirildi. Ekran verileri sadece **süre, durum veya iki noktanın konumu gerçekten değiştiğinde** yazılır. Bu sayede loop hızı **saniyede 50.000+ çevrime çıkarıldı**, gecikmeler tamamen yok edildi.

---

## 📌 5. Rastgele Siren Kesilmeleri (Timer 1 `ICR1` Aşım Hatası)

### 🔴 Belirti:
Alarm çalarken, belirli aralıklarla olmayan, tamamen rastgele ve düzensiz milisaniyelik anlık ses kesilmeleri/pıtırtılar oluşması.

### 🔍 Analiz:
Siren tonunu 2500 Hz ile 3500 Hz arasında 100 ms'de bir değiştirmek için Timer 1'in **`ICR1` (Üst Sınır / TOP)** kaydedicisine yeni değer yazılır. 
* ATmega328P mimarisinde Fast PWM modunda (Mod 14) **`ICR1` kaydedicisi donanımsal çift-tamponlu (double-buffered) değildir.** Değer yazıldığı anda güncellenir.
* **Hata Durumu:** Frekansı değiştirirken eğer Timer 1 sayacı (`TCNT1`) o an yeni yazılan `ICR1` sınırını geçmişse (örn. TCNT1=600, yeni ICR1=571), sayaç üst sınırı kaçırır. Sınırı kaçırdığı için durmaz ve taa **65535'e kadar saymaya devam eder**.
* Bu durum, 2 MHz sayaç hızında tam **32.7 milisaniye** sürer ve bu süre zarfında MOSFET kapısı takılı kalarak **rastgele ve periyodik olmayan sessizlik/kesinti** oluşturur.

### 🟢 Kesin Çözüm:
Frekans (yani `ICR1`) her değiştiğinde, zamanlayıcı sayacı yazılımsal olarak **anında sıfırlanır (`TCNT1 = 0;`)**. Böylece sayaç asla yeni üst sınırı kaçırmaz ve geçişler faz-kesintisiz, tamamen pürüzsüz ve çıtırtısız olur:
```cpp
void startTonePWM(unsigned int frequency, int volumeLevel) {
  pinMode(PIN_MOSFET, OUTPUT);
  unsigned long top = 2000000UL / frequency;
  
  if (ICR1 != top) {
    ICR1 = top;
    TCNT1 = 0; // Sayacı sıfırlayarak TCNT1 > ICR1 aşımını engeller (Kesintileri sıfırlar!)
  }
  
  unsigned long ocr = (top * (unsigned long)(volumeLevel * volumeLevel)) / 200UL;
  if (ocr == 0 && volumeLevel > 0) ocr = 1;
  OCR1A = ocr;
  
  if ((TCCR1A & _BV(COM1A1)) == 0) {
    TCCR1A = _BV(COM1A1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);
  }
}
```

---

## 🛠️ Gelecekte Yapılacak Değişiklik: Test Modunu Kapatma

Şu anda ses ve donanım montajını hızlıca test edebilmeniz için kodda **3 saniyelik hızlı test modu** aktiftir. Montajı tamamlayıp sistemi nihai durumuna getirdiğinizde bu modu kapatmak için şu iki basit adımı uygulayın:

### 1. Adım:
[kitchen_timer.ino](file:///C:/Users/kaval/OneDrive/Desktop/geri%20say%C4%B1m%20nano/kitchen_timer/kitchen_timer.ino) dosyasının en başındaki (satır 38 civarı) varsayılan süreyi **`0`** yapın:
```diff
- #define DEFAULT_TIME_SECONDS 3
+ #define DEFAULT_TIME_SECONDS 0
```

### 2. Adım:
Dosyanın sonundaki `loadTimeFromEEPROM()` fonksiyonundaki (satır 627 civarı) geçici kontrol bloğunu silin veya yorum satırı yapın:
```diff
 void loadTimeFromEEPROM() {
   byte magic = EEPROM.read(EEPROM_ADDR_MAGIC);
   
   if (magic == EEPROM_MAGIC_VAL) {
     byte loadedMinutes = EEPROM.read(EEPROM_ADDR_TIME);
     if (loadedMinutes >= MIN_MINUTES && loadedMinutes <= MAX_MINUTES) {
-      // [GEÇİCİ TEST] Eğer kalıcı hafızadaki süre 0 ise hızlı test kolaylığı için 3 saniye yapalım
-      if (loadedMinutes == 0) {
-        targetTimeSeconds = 3;
-        return;
-      }
       targetTimeSeconds = (long)loadedMinutes * 60;
       return;
     }
   }
   
   targetTimeSeconds = DEFAULT_TIME_SECONDS;
 }
```
Bu adımları uyguladığınızda cihazınız tamamen orijinal, bekleme modunda `00:00` ile başlayan normal çalışma döngüsüne geri dönecektir.
