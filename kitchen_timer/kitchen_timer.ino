/**
 * KİTCHEN COUNTDOWN TIMER / MUTFAK GERİ SAYIM ALARMI
 * Donanım: Arduino Nano, TM1637 Display, Rotary Encoder, MOSFET Sürücü & 4 Ohm 5W Hoparlör
 * Besleme: 5V 3A Adaptör
 * 
 * ÖNEMLİ ELEKTRİKSEL NOT (1/4W Direnç Birleştirme İpucu):
 * 4 Ohm 5W hoparlörün MOSFET ile 5V'ta güvenli çalışması için ~10-15 Ohm arası en az 1W güçte 
 * bir direnç seri bağlanmalıdır. Elinizde sadece 1/4W direnç varsa, paralel bağlama kuralını kullanarak
 * kendi 1W direncinizi üretebilirsiniz:
 * -> 4 adet 47 Ohm 1/4W direnci birbirine paralel lehimlerseniz: 47 / 4 = ~11.7 Ohm ve 4 * 0.25W = 1W gücünde direnç elde edersiniz!
 * -> Bu direnç kümesini hoparlöre seri bağlayarak hoparlörünüzü ve MOSFET'inizi güvenle koruyabilirsiniz.
 * 
 * Gerekli Kütüphaneler:
 * - TM1637Display (Avishay Orpaz): Arduino IDE Kütüphane Yöneticisinden "TM1637" yazarak kurabilirsiniz.
 * - EEPROM (Dahili)
 */

#include <Arduino.h>
#include <TM1637Display.h>
#include <EEPROM.h>

// PIN TANIMLAMALARI
#define PIN_ENC_CLK  2   // Kesme (Interrupt) pini - Döner Enkoder A Çıkışı
#define PIN_ENC_DT   3   // Kesme (Interrupt) pini - Döner Enkoder B Çıkışı
#define PIN_ENC_SW   4   // Döner Enkoder Entegre Buton Çıkışı
#define PIN_DISP_CLK 5   // TM1637 Clock Pini
#define PIN_DISP_DIO 6   // TM1637 Data Pini
#define PIN_MOSFET   9   // MOSFET Sinyal Pini (Hoparlör Tetikleyici)

// EEPROM Bellek Adresleri
#define EEPROM_ADDR_MAGIC   10  // Belleğin daha önce yazılıp yazılmadığını kontrol etmek için sihirli bayt
#define EEPROM_ADDR_TIME    0   // Sürenin kaydedileceği adres (unsigned long veya int)
#define EEPROM_MAGIC_VAL    0xA5

// Sınır Değerler (Süre dakika cinsinden ayarlandığı için 0-99 dk arası)
#define MAX_MINUTES 99
#define MIN_MINUTES 0
#define DEFAULT_TIME_SECONDS 0 // Varsayılan 0 dakika (0 saniye)

// Sistem Durumları (State Machine)
enum SystemState {
  STATE_STANDBY,    // Bekleme Modu (Son süre gösterilir, noktalar yavaşça yanıp söner)
  STATE_ADJUSTING,  // Süre Ayarlama Modu (Döndürüldüğünde girilir, süre ayarlanır)
  STATE_COUNTDOWN,  // Geri Sayım Aktif
  STATE_PAUSED,     // Geri Sayım Duraklatıldı
  STATE_ALARM       // Alarm Aktif (Süre bitti, hoparlör ötüyor)
};

// Enkoder Buton Olayları
enum ButtonEvent {
  BTN_NONE,
  BTN_SHORT,
  BTN_LONG
};

// Global Değişkenler
SystemState currentState = STATE_STANDBY;
long targetTimeSeconds = DEFAULT_TIME_SECONDS; // Ayarlanan hedef süre (saniye)
long remainingSeconds = DEFAULT_TIME_SECONDS;  // Kalan süre (saniye)

// TM1637 Ekran Nesnesi
TM1637Display display(PIN_DISP_CLK, PIN_DISP_DIO);

// Volatile Enkoder Değişkenleri (Kesme içerisinde değiştirildiği için)
volatile int encoderDelta = 0; 
volatile bool encoderMoved = false;

// Zamanlayıcı Takip Değişkenleri
unsigned long lastCountdownTick = 0;
unsigned long lastStateChangeTime = 0;
unsigned long lastColonToggle = 0;
bool colonState = false;

// Buton Debouncing Değişkenleri (Fonksiyon içinde static olarak tanımlanmıştır)

// Alarm Ses Ritmi Değişkenleri (Tamamen non-blocking / kilitlemesiz çalışan melodi yapısı)
unsigned long lastAlarmActionTime = 0;
int alarmPatternState = 0; 

// Alarm Sırasında Ekran Efekti Değişkenleri
unsigned long lastAlarmDisplayToggle = 0;
bool alarmDisplayToggleState = false;

// TM1637 için "End " (Son) ve "ALrt" (Alarm) Segment Tanımları
const uint8_t SEG_END[] = {
  0x79, // E (0b01111001)
  0x54, // n (0b01010100)
  0x5E, // d (0b01011110)
  0x00  // Boşluk
};

const uint8_t SEG_ALRT[] = {
  0x77, // A (0b01110111)
  0x38, // L (0b00111000)
  0x50, // r (0b01010000)
  0x78  // t (0b01111000)
};

// --- FONKSİYON PROTOTİPLERİ ---
void encoderISR();
ButtonEvent checkButton();
void updateDisplay();
void handleAlarmSpeaker();
void saveTimeToEEPROM();
void loadTimeFromEEPROM();

void setup() {
  // Teşhis (Diagnostic) için dahili LED çıkış yapılıyor
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Pin modları ayarlanıyor
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  
  // Hoparlör pini çıkış yapılıyor ve mutlaka başlangıçta LOW (kapalı) tutuluyor.
  // MOSFET'in sürekli açık kalıp akım çekmesini önleyen elektriksel güvenlik tedbiri.
  pinMode(PIN_MOSFET, OUTPUT);
  digitalWrite(PIN_MOSFET, LOW);

  // TM1637 Ekranı Başlatılıyor
  display.setBrightness(4); // Parlaklık seviyesi (0-7 arası, başlangıçta orta seviye)
  
  // Kalıcı hafızadan son ayarlanan başarılı süreyi yükle
  loadTimeFromEEPROM();
  remainingSeconds = targetTimeSeconds;

  // CLK pinindeki her değişimde (D2 pini HIGH/LOW geçişlerinde) encoderISR tetiklenecek (Durum Makinesi için CHANGE)
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, CHANGE);
  
  lastStateChangeTime = millis();
}

void loop() {
  // DAHİLİ LED İLE BUTON FİZİKSEL BAĞLANTI KONTROLÜ (TEŞHİS/DIAGNOSTIC)
  // Butona basıldığında Nano üzerindeki dahili "L" LED'i anında yanmalıdır.
  // Eğer basmanıza rağmen yanmıyorsa, butonun 2. pini GND'ye bağlanmamıştır veya kablolamada kopukluk vardır!
  digitalWrite(LED_BUILTIN, digitalRead(PIN_ENC_SW) == LOW ? HIGH : LOW);

  unsigned long currentMillis = millis();

  // 1. Döner Enkoder Dönüş Hareketinin İşlenmesi
  if (encoderMoved) {
    noInterrupts(); // Değişkenleri güvenle okumak için kesmeleri geçici olarak kapat
    int delta = encoderDelta;
    encoderDelta = 0;
    encoderMoved = false;
    interrupts(); // Kesmeleri tekrar aç

    if (delta != 0) {
      lastStateChangeTime = currentMillis;

      // Eğer alarm çalıyorsa veya geri sayım yapılıyorsa enkoder hareketi sistemi güvenli moda çeker
      if (currentState == STATE_ALARM) {
        currentState = STATE_STANDBY;
        remainingSeconds = targetTimeSeconds;
      } 
      else if (currentState == STATE_COUNTDOWN || currentState == STATE_PAUSED) {
        // Geri sayım veya duraklatma esnasında enkoder çevrilirse süre ayarlama moduna döner
        currentState = STATE_ADJUSTING;
      }
      else if (currentState == STATE_STANDBY) {
        currentState = STATE_ADJUSTING;
      }

      // Dinamik adım boyutu: 30 dk ve altında 1'er dk (60 sn), üstünde 5'er dk (300 sn)
      long stepSeconds = 60;
      if (delta > 0) { // Artırma
        if (targetTimeSeconds >= 30 * 60) {
          stepSeconds = 300; // 5 dakika
        } else {
          stepSeconds = 60;  // 1 dakika
        }
      } else { // Azaltma
        if (targetTimeSeconds > 30 * 60) {
          stepSeconds = 300; // 5 dakika
        } else {
          stepSeconds = 60;  // 1 dakika
        }
      }
      targetTimeSeconds += delta * stepSeconds;

      // Sınır kontrolleri (1 dakika ile 99 dakika arası)
      if (targetTimeSeconds > (MAX_MINUTES * 60)) {
        targetTimeSeconds = MAX_MINUTES * 60;
      }
      if (targetTimeSeconds < (MIN_MINUTES * 60)) {
        targetTimeSeconds = MIN_MINUTES * 60;
      }
      
      remainingSeconds = targetTimeSeconds;
    }
  }

  // 2. Buton Tıklamalarının İşlenmesi
  ButtonEvent btn = checkButton();
  if (btn != BTN_NONE) {
    lastStateChangeTime = currentMillis;

    switch (currentState) {
      case STATE_STANDBY:
        if (btn == BTN_SHORT) {
          // Geri sayımı başlat (Yalnızca süre 0'dan büyükse)
          if (targetTimeSeconds > 0) {
            saveTimeToEEPROM(); // Başlatılan süreyi kalıcı hafızaya kaydet
            currentState = STATE_COUNTDOWN;
            lastCountdownTick = currentMillis;
          }
        } else if (btn == BTN_LONG) {
          // Sıfırla ve hafızayı temizle (00:00 yap)
          targetTimeSeconds = DEFAULT_TIME_SECONDS;
          remainingSeconds = targetTimeSeconds;
          EEPROM.update(EEPROM_ADDR_TIME, 0);
        }
        break;

      case STATE_ADJUSTING:
        if (btn == BTN_SHORT) {
          // Ayarlanan süreyi onaylayıp geri sayımı başlat (Yalnızca süre 0'dan büyükse)
          if (targetTimeSeconds > 0) {
            saveTimeToEEPROM();
            currentState = STATE_COUNTDOWN;
            lastCountdownTick = currentMillis;
          }
        } else if (btn == BTN_LONG) {
          // Sıfırla ve hafızayı temizle (00:00 yap)
          targetTimeSeconds = DEFAULT_TIME_SECONDS;
          remainingSeconds = targetTimeSeconds;
          EEPROM.update(EEPROM_ADDR_TIME, 0);
          currentState = STATE_STANDBY;
        }
        break;

      case STATE_COUNTDOWN:
        if (btn == BTN_SHORT) {
          // Duraklat
          currentState = STATE_PAUSED;
        } else if (btn == BTN_LONG) {
          // Geri sayımı iptal et, başa dön
          remainingSeconds = targetTimeSeconds;
          currentState = STATE_STANDBY;
        }
        break;

      case STATE_PAUSED:
        if (btn == BTN_SHORT) {
          // Kaldığı yerden devam et
          currentState = STATE_COUNTDOWN;
          lastCountdownTick = currentMillis;
        } else if (btn == BTN_LONG) {
          // Geri sayımı iptal et, sıfırla
          remainingSeconds = targetTimeSeconds;
          currentState = STATE_STANDBY;
        }
        break;

      case STATE_ALARM:
        // Herhangi bir tuş basışı alarmı susturur ve son süreyle standby'a geçer
        currentState = STATE_STANDBY;
        remainingSeconds = targetTimeSeconds;
        break;
    }
  }

  // 3. Durum Yönetimi ve Zamanlayıcı Mantığı
  switch (currentState) {
    case STATE_ADJUSTING:
      // Süre ayarlama modunda 5 saniye boyunca işlem yapılmazsa otomatik olarak Standby moduna geç
      if (currentMillis - lastStateChangeTime > 5000) {
        currentState = STATE_STANDBY;
      }
      break;

    case STATE_COUNTDOWN:
      // Saniye takibi (Milisaniye tabanlı hassas geri sayım)
      if (currentMillis - lastCountdownTick >= 1000) {
        lastCountdownTick += 1000;
        if (remainingSeconds > 0) {
          remainingSeconds--;
        }
        
        // Süre dolduğunda alarm moduna geç
        if (remainingSeconds <= 0) {
          currentState = STATE_ALARM;
          alarmPatternState = 0;
          lastAlarmActionTime = currentMillis;
          lastAlarmDisplayToggle = currentMillis;
        }
      }
      break;

    case STATE_STANDBY:
    case STATE_PAUSED:
      // Bu durumlarda arka plan sayımı yok
      break;

    case STATE_ALARM:
      // Alarm modundaki işlemler (handleAlarmSpeaker artık her loop'ta çağrılmaktadır)
      break;
  }

  // Alarm modunda hoparlör ses rutinini sürekli çalıştır (Güvenli kapatma için her loop'ta çağrılır)
  handleAlarmSpeaker();

  // 4. Ekranın ve Göstergelerin Sürekli Güncellenmesi
  updateDisplay();
}

/**
 * Donanımsal Kesme Servisi (ISR)
 * Döner enkoder döndürüldüğünde çok hızlı biçimde tetiklenerek yön algılar.
 * 1'er dakika artış/azalış ana döngüde güvenli şekilde yapılacaktır.
 */
void encoderISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  // 30ms donanımsal gürültü (bounce) önleme filtresi (Klon enkoderlerin kararlılığı için 30ms yapıldı)
  if (interruptTime - lastInterruptTime > 30) {
    int clkVal = digitalRead(PIN_ENC_CLK);
    int dtVal = digitalRead(PIN_ENC_DT);
    
    // CLK ve DT okumalarına göre yön algılama
    if (clkVal != dtVal) {
      encoderDelta = 1;  // Saat yönü (Artır)
    } else {
      encoderDelta = -1; // Saat yönünün tersi (Azalt)
    }
    encoderMoved = true;
    lastInterruptTime = interruptTime;
  }
}

/**
 * Enkoder Butonunun Okunması ve Debounce Edilmesi
 * Kısa basış (Başlat/Durdur/Sustur) ve Uzun basış (1 sn - Sıfırla/İptal) ayrımı yapar.
 * Non-blocking (beklemesiz) olarak tasarlanmıştır.
 */
ButtonEvent checkButton() {
  static bool lastStableState = HIGH;
  static bool lastRawState = HIGH;
  static unsigned long lastDebounceTime = 0;
  static unsigned long buttonPressTime = 0;
  static bool longPressTriggered = false;

  bool rawState = digitalRead(PIN_ENC_SW);
  ButtonEvent event = BTN_NONE;
  unsigned long currentMillis = millis();

  // Durum değişikliğinde gürültüyü süzmek için debounce zamanlayıcısını sıfırla
  if (rawState != lastRawState) {
    lastDebounceTime = currentMillis;
  }
  lastRawState = rawState;

  // Sinyal kararlı kaldığı sürece debounce filtrelemesini uygula (50ms)
  if ((currentMillis - lastDebounceTime) > 50) {
    if (rawState != lastStableState) {
      lastStableState = rawState;

      if (lastStableState == LOW) {
        buttonPressTime = currentMillis;
        longPressTriggered = false;
      } 
      else {
        if (!longPressTriggered) {
          unsigned long pressDuration = currentMillis - buttonPressTime;
          if (pressDuration >= 1000) {
            event = BTN_LONG;
          } else {
            event = BTN_SHORT;
          }
        }
      }
    }
  }

  // Buton hala basılıyken 1 saniye geçtiyse bırakmasını beklemeden LONG_PRESS tetikle
  if (lastStableState == LOW && !longPressTriggered && (currentMillis - buttonPressTime >= 1000)) {
    longPressTriggered = true;
    event = BTN_LONG;
  }

  return event;
}

/**
 * Sistem Durumuna Göre TM1637 Ekranının Güncellenmesi
 */
void updateDisplay() {
  unsigned long currentMillis = millis();
  
  switch (currentState) {
    case STATE_STANDBY: {
      // Bekleme Modu: Ekran parlaklığı düşük (1), son ayarlanan süre gösterilir.
      // Ortadaki iki nokta (colon) yavaşça nefes alma (breathing) animasyonu yapar.
      display.setBrightness(1);
      
      if (currentMillis - lastColonToggle >= 800) {
        lastColonToggle = currentMillis;
        colonState = !colonState;
      }
      
      int mins = targetTimeSeconds / 60;
      int secs = targetTimeSeconds % 60;
      display.showNumberDecEx(mins * 100 + secs, colonState ? 0b01000000 : 0, true);
      break;
    }
    
    case STATE_ADJUSTING: {
      // Süre Ayarlama Modu: Ekran parlaklığı aktif (4), noktalar sabit açık kalır.
      display.setBrightness(4);
      int mins = targetTimeSeconds / 60;
      int secs = targetTimeSeconds % 60;
      display.showNumberDecEx(mins * 100 + secs, 0b01000000, true);
      break;
    }
    
    case STATE_COUNTDOWN: {
      // Geri Sayım Modu: Ekran parlaklığı maksimum (7), noktalar saniyede bir yanıp söner.
      display.setBrightness(7);
      
      if (currentMillis - lastColonToggle >= 500) {
        lastColonToggle = currentMillis;
        colonState = !colonState;
      }
      
      int mins = remainingSeconds / 60;
      int secs = remainingSeconds % 60;
      display.showNumberDecEx(mins * 100 + secs, colonState ? 0b01000000 : 0, true);
      break;
    }
    
    case STATE_PAUSED: {
      // Duraklatma Modu: Ekran ve süre komple yanıp söner (Flaşör efekti)
      display.setBrightness(7);
      
      if (currentMillis - lastColonToggle >= 400) {
        lastColonToggle = currentMillis;
        colonState = !colonState;
      }
      
      if (colonState) {
        int mins = remainingSeconds / 60;
        int secs = remainingSeconds % 60;
        display.showNumberDecEx(mins * 100 + secs, 0b01000000, true);
      } else {
        display.clear(); // Yanıp sönme efekti için ekranı temizle
      }
      break;
    }
    
    case STATE_ALARM: {
      // Alarm Modu: Ekranda dönüşümlü olarak "00:00" ve "End " ya da "ALrt" yazısı yanıp söner
      display.setBrightness(7);
      
      if (currentMillis - lastAlarmDisplayToggle >= 400) {
        lastAlarmDisplayToggle = currentMillis;
        alarmDisplayToggleState = !alarmDisplayToggleState;
      }
      
      if (alarmDisplayToggleState) {
        display.showNumberDecEx(0, 0b01000000, true); // "00:00"
      } else {
        // Dönüşümlü olarak "End" veya "ALrt" gösterimi yapabilirsiniz, burada "End" kullanıyoruz.
        display.setSegments(SEG_END); 
      }
      break;
    }
  }
}

/**
 * MOSFET üzerinden Hoparlörü Tetikleyen Ritmik Alarm Rutini
 * Güvenlik için tamamen non-blocking tasarlanmıştır ve hoparlörü asla sürekli DC HIGH durumunda bırakmaz!
 * Alarm Riti: Çift Tonlu Kesintisiz Polis Sireni (2500 Hz ve 3500 Hz her 100ms'de bir yer değiştirir)
 */
void handleAlarmSpeaker() {
  static bool wasAlarmRunning = false;
  unsigned long currentMillis = millis();
  unsigned long elapsed = currentMillis - lastAlarmActionTime;

  // Eğer alarm durumunda değilsek hoparlörü hemen kapatıp emniyete alıyoruz.
  if (currentState != STATE_ALARM) {
    if (wasAlarmRunning) {
      noTone(PIN_MOSFET);
      digitalWrite(PIN_MOSFET, LOW); // Emniyet için pini LOW yap (Sürekli akım çekimini önler)
      alarmPatternState = 0;
      wasAlarmRunning = false;
    }
    return;
  }

  wasAlarmRunning = true;

  // 2. Alternatif: Çift Tonlu Polis Sireni Algoritması
  switch (alarmPatternState) {
    case 0: // 1. Ton Başlangıcı (2500 Hz)
      tone(PIN_MOSFET, 2500); 
      lastAlarmActionTime = currentMillis;
      alarmPatternState = 1;
      break;
      
    case 1: // 1. Ton Süresi Sonu (100ms geçince 3500 Hz'e geç)
      if (elapsed >= 100) {
        tone(PIN_MOSFET, 3500); // 2. Ton Başlangıcı
        lastAlarmActionTime = currentMillis;
        alarmPatternState = 2;
      }
      break;
      
    case 2: // 2. Ton Süresi Sonu (100ms geçince döngüyü başa sar)
      if (elapsed >= 100) {
        alarmPatternState = 0; // Başa dönerek 2500 Hz'den tekrar başla
      }
      break;
  }
}

/**
 * Son ayarlanan ve başarıyla başlatılan süreyi kalıcı hafızaya (EEPROM) kaydeder.
 * EEPROM ömrünü korumak adına sadece süre değiştiğinde yazma yapar (update kullanır).
 */
void saveTimeToEEPROM() {
  // targetTimeSeconds saniye cinsindendir. Dakika cinsine çevirip saklamak EEPROM'da tek bayt (0-255) kaplar.
  // 1-99 dakika arası saklayacağımız için bu son derece verimlidir.
  byte minutesToSave = targetTimeSeconds / 60;
  
  if (EEPROM.read(EEPROM_ADDR_TIME) != minutesToSave) {
    EEPROM.update(EEPROM_ADDR_TIME, minutesToSave);
  }
  
  // Kalıcı hafızanın geçerli olduğunu belirtmek için sihirli baytı yaz
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_VAL) {
    EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VAL);
  }
}

/**
 * Cihaz açıldığında kalıcı hafızadan (EEPROM) son ayarlanan süreyi yükler.
 * Hafıza boşsa veya bozuksa varsayılan 5 dakikayı yükler.
 */
void loadTimeFromEEPROM() {
  byte magic = EEPROM.read(EEPROM_ADDR_MAGIC);
  
  if (magic == EEPROM_MAGIC_VAL) {
    byte loadedMinutes = EEPROM.read(EEPROM_ADDR_TIME);
    if (loadedMinutes >= MIN_MINUTES && loadedMinutes <= MAX_MINUTES) {
      targetTimeSeconds = (long)loadedMinutes * 60;
      return;
    }
  }
  
  // Hafıza geçersizse varsayılan süreyi kullan
  targetTimeSeconds = DEFAULT_TIME_SECONDS;
}
