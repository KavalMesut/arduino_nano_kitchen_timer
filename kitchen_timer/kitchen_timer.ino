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
#define EEPROM_ADDR_MAGIC        10  // Belleğin daha önce yazılıp yazılmadığını kontrol etmek için sihirli bayt
#define EEPROM_ADDR_TIME         0   // Sürenin kaydedileceği adres (unsigned long veya int)
#define EEPROM_MAGIC_VAL         0xA6
#define EEPROM_ADDR_VOLUME       1   // Ses seviyesinin kaydedileceği adres [YENİ]
#define EEPROM_ADDR_VOLUME_MAGIC 11  // Ses seviyesi geçerlilik magic adresi [YENİ]
#define EEPROM_VOLUME_MAGIC_VAL  0x5A

// Sınır Değerler (Süre dakika cinsinden ayarlandığı için 0-99 dk arası)
#define MAX_MINUTES 99
#define MIN_MINUTES 0
#define DEFAULT_TIME_SECONDS 0 // Varsayılan başlangıç süresi 0 dakika (0 saniye)

// Sistem Durumları (State Machine)
enum SystemState {
  STATE_STANDBY,    // Bekleme Modu (Son süre gösterilir, noktalar yavaşça yanıp söner)
  STATE_ADJUSTING,  // Süre Ayarlama Modu (Döndürüldüğünde girilir, süre ayarlanır)
  STATE_COUNTDOWN,  // Geri Sayım Aktif
  STATE_PAUSED,     // Geri Sayım Duraklatıldı
  STATE_ALARM,      // Alarm Aktif (Süre bitti, hoparlör ötüyor)
  STATE_VOLUME_SETTING // Ses Seviyesi Ayarlama Modu [YENİ]
};

// Enkoder Buton Olayları
enum ButtonEvent {
  BTN_NONE,
  BTN_SHORT,
  BTN_LONG
};

// Global Değişkenler
SystemState currentState = STATE_STANDBY;
SystemState previousState = STATE_STANDBY; // [GÜNCELLENDİ] Ses ayarından geri dönüş için
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

// --- GÜNCEL SES AYARI DEĞİŞKENLERİ ---
int volumeLevel = 5; // Varsayılan ses seviyesi (1-10 arası, 5 = orta) [YENİ]

// --- FONKSİYON PROTOTİPLERİ ---
void encoderISR();
ButtonEvent checkButton();
void updateDisplay();
void handleAlarmSpeaker();
void saveTimeToEEPROM();
void loadTimeFromEEPROM();
void saveVolumeToEEPROM(); // [YENİ]
void loadVolumeFromEEPROM(); // [YENİ]
void startTonePWM(unsigned int frequency, int volumeLevel); // [YENİ]
void stopTonePWM(); // [YENİ]

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
  loadVolumeFromEEPROM(); // [YENİ]
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

      if (currentState == STATE_VOLUME_SETTING) {
        volumeLevel += delta;
        if (volumeLevel > 10) volumeLevel = 10;
        if (volumeLevel < 1) volumeLevel = 1;

        // Ses seviyesi değiştiğinde bildirim sesi ver (50ms bip)
        startTonePWM(3000, volumeLevel);
        delay(50);
        stopTonePWM();
      } 
      else {
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

        // Dinamik adım boyutu: ilk adım 30 saniye, sonrasında 30 dk ve altında 1'er dk (60 sn), üstünde 5'er dk (300 sn)
        if (delta > 0) { // Artırma
          if (targetTimeSeconds < 30) {
            targetTimeSeconds = 30;
          } else if (targetTimeSeconds < 60) {
            targetTimeSeconds = 60;
          } else {
            long stepSeconds = (targetTimeSeconds >= 30 * 60) ? 300 : 60;
            targetTimeSeconds += stepSeconds;
          }
        } else { // Azaltma
          if (targetTimeSeconds <= 30) {
            targetTimeSeconds = 0;
          } else if (targetTimeSeconds <= 60) {
            targetTimeSeconds = 30;
          } else {
            long stepSeconds = (targetTimeSeconds > 30 * 60) ? 300 : 60;
            targetTimeSeconds -= stepSeconds;
          }
        }

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
          previousState = currentState;
          currentState = STATE_VOLUME_SETTING;
          startTonePWM(3000, volumeLevel);
          delay(100);
          stopTonePWM();
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
          previousState = currentState;
          currentState = STATE_VOLUME_SETTING;
          startTonePWM(3000, volumeLevel);
          delay(100);
          stopTonePWM();
        }
        break;

      case STATE_COUNTDOWN:
        if (btn == BTN_SHORT) {
          // Duraklatma olmasın, doğrudan durdurup başa (hedef süreye) dönsün
          remainingSeconds = targetTimeSeconds;
          currentState = STATE_STANDBY;
        } else if (btn == BTN_LONG) {
          previousState = currentState;
          currentState = STATE_VOLUME_SETTING;
          startTonePWM(3000, volumeLevel);
          delay(100);
          stopTonePWM();
        }
        break;

      case STATE_PAUSED:
        if (btn == BTN_SHORT) {
          // Kaldığı yerden devam et
          currentState = STATE_COUNTDOWN;
          lastCountdownTick = currentMillis;
        } else if (btn == BTN_LONG) {
          previousState = currentState;
          currentState = STATE_VOLUME_SETTING;
          startTonePWM(3000, volumeLevel);
          delay(100);
          stopTonePWM();
        }
        break;

      case STATE_ALARM:
        // Herhangi bir tuş basışı alarmı susturur ve son süreyle standby'a geçer
        currentState = STATE_STANDBY;
        remainingSeconds = targetTimeSeconds;
        break;

      case STATE_VOLUME_SETTING:
        // Herhangi bir tuş basışı ses ayarını kaydeder ve çıkar
        saveVolumeToEEPROM();
        startTonePWM(3500, volumeLevel);
        delay(150);
        stopTonePWM();
        currentState = previousState; // [GÜNCELLENDİ] Kaldığı yerden devam etmesi için
        if (currentState == STATE_COUNTDOWN) {
          lastCountdownTick = millis(); // Sayacın kaldığı yerden hassas sayması için
        }
        break;
    }
  }

  // 3. Durum Yönetimi ve Zamanlayıcı Mantığı
  switch (currentState) {
    case STATE_ADJUSTING:
      // Süre ayarlama modunda 30 saniye boyunca işlem yapılmazsa otomatik olarak Standby moduna geç
      if (currentMillis - lastStateChangeTime > 30000) {
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

    case STATE_VOLUME_SETTING:
      // Ses ayarlama modunda 30 saniye boyunca işlem yapılmazsa otomatik kaydet ve çık
      if (currentMillis - lastStateChangeTime > 30000) {
        saveVolumeToEEPROM();
        startTonePWM(3500, volumeLevel);
        delay(150);
        stopTonePWM();
        currentState = previousState;
        if (currentState == STATE_COUNTDOWN) {
          lastCountdownTick = millis(); // Sayacın kaldığı yerden hassas sayması için
        }
      }
      break;

    case STATE_STANDBY:
      // Bu durumda arka plan sayımı yok
      break;

    case STATE_PAUSED:
      // Duraklatma modunda 30 saniye boyunca işlem yapılmazsa otomatik olarak iptal et ve Standby'a dön
      if (currentMillis - lastStateChangeTime > 30000) {
        remainingSeconds = targetTimeSeconds;
        currentState = STATE_STANDBY;
      }
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
  
  // Ekranın gereksiz yere sürekli yazılmasını önlemek için statik takip değişkenleri
  static SystemState lastState = (SystemState)-1;
  static bool lastColonState = false;
  static bool lastAlarmToggle = false;
  static int lastVol = -1;
  static long lastRemSecs = -1;
  static long lastTarSecs = -1;

  // Durum değişikliğinde ekranı anında temizlemek/güncellemek için tetikleyici
  bool stateChanged = (currentState != lastState);
  if (stateChanged) {
    lastState = currentState;
    display.clear(); // Durum geçişinde ekranı temizle (kalıntıları önler)
  }

  switch (currentState) {
    case STATE_STANDBY: {
      display.setBrightness(1);
      
      if (currentMillis - lastColonToggle >= 800) {
        lastColonToggle = currentMillis;
        colonState = !colonState;
      }
      
      int mins = targetTimeSeconds / 60;
      int secs = targetTimeSeconds % 60;
      
      // Sadece değerler, iki nokta durumu veya durum değiştiğinde ekrana yaz
      if (stateChanged || colonState != lastColonState || targetTimeSeconds != lastTarSecs) {
        lastColonState = colonState;
        lastTarSecs = targetTimeSeconds;
        display.showNumberDecEx(mins * 100 + secs, colonState ? 0b01000000 : 0, true);
      }
      break;
    }
    
    case STATE_ADJUSTING: {
      display.setBrightness(4);
      int mins = targetTimeSeconds / 60;
      int secs = targetTimeSeconds % 60;
      
      if (stateChanged || targetTimeSeconds != lastTarSecs) {
        lastTarSecs = targetTimeSeconds;
        display.showNumberDecEx(mins * 100 + secs, 0b01000000, true);
      }
      break;
    }
    
    case STATE_COUNTDOWN: {
      display.setBrightness(7);
      
      if (currentMillis - lastColonToggle >= 500) {
        lastColonToggle = currentMillis;
        colonState = !colonState;
      }
      
      int mins = remainingSeconds / 60;
      int secs = remainingSeconds % 60;
      
      if (stateChanged || colonState != lastColonState || remainingSeconds != lastRemSecs) {
        lastColonState = colonState;
        lastRemSecs = remainingSeconds;
        display.showNumberDecEx(mins * 100 + secs, colonState ? 0b01000000 : 0, true);
      }
      break;
    }
    
    case STATE_PAUSED: {
      // Duraklatma Modu: Ekran flaşör (yanıp sönme) yapmaz, son kalan süreyi sabit gösterir.
      display.setBrightness(7);
      int mins = remainingSeconds / 60;
      int secs = remainingSeconds % 60;
      
      if (stateChanged || remainingSeconds != lastRemSecs) {
        lastRemSecs = remainingSeconds;
        display.showNumberDecEx(mins * 100 + secs, 0b01000000, true); // İki nokta sabit açık kalır
      }
      break;
    }
    
    case STATE_ALARM: {
      display.setBrightness(7);
      
      if (currentMillis - lastAlarmDisplayToggle >= 400) {
        lastAlarmDisplayToggle = currentMillis;
        alarmDisplayToggleState = !alarmDisplayToggleState;
      }
      
      if (stateChanged || alarmDisplayToggleState != lastAlarmToggle) {
        lastAlarmToggle = alarmDisplayToggleState;
        if (alarmDisplayToggleState) {
          display.showNumberDecEx(0, 0b01000000, true); // "00:00"
        } else {
          display.setSegments(SEG_END); 
        }
      }
      break;
    }
    
    case STATE_VOLUME_SETTING: {
      display.setBrightness(7);
      
      if (stateChanged || volumeLevel != lastVol) {
        lastVol = volumeLevel;
        uint8_t segments[4];
        segments[0] = 0x3E; // 'U'
        segments[1] = 0x40; // '-'
        if (volumeLevel < 10) {
          segments[2] = 0x00; // Boşluk
          segments[3] = display.encodeDigit(volumeLevel);
        } else {
          segments[2] = display.encodeDigit(1);
          segments[3] = display.encodeDigit(0);
        }
        display.setSegments(segments);
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
      stopTonePWM();
      alarmPatternState = 0;
      wasAlarmRunning = false;
    }
    return;
  }

  wasAlarmRunning = true;

  // 2. Alternatif: Çift Tonlu Polis Sireni Algoritması
  switch (alarmPatternState) {
    case 0: // 1. Ton Başlangıcı (2500 Hz)
      startTonePWM(2500, volumeLevel); 
      lastAlarmActionTime = currentMillis;
      alarmPatternState = 1;
      break;
      
    case 1: // 1. Ton Süresi Sonu (100ms geçince 3500 Hz'e geç)
      if (elapsed >= 100) {
        startTonePWM(3500, volumeLevel); // 2. Ton Başlangıcı
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
  // targetTimeSeconds saniye cinsindendir. 30 saniyelik adımlara bölüp saklamak EEPROM'da tek bayt (0-255) kaplar.
  // En fazla 99 dakika (5940 saniye) olabileceği için 5940 / 30 = 198 birim yapar ve 1 bayta rahatça sığar.
  byte unitsToSave = targetTimeSeconds / 30;
  
  if (EEPROM.read(EEPROM_ADDR_TIME) != unitsToSave) {
    EEPROM.update(EEPROM_ADDR_TIME, unitsToSave);
  }
  
  // Kalıcı hafızanın geçerli olduğunu belirtmek için sihirli baytı yaz
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_VAL) {
    EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VAL);
  }
}

/**
 * Cihaz açıldığında kalıcı hafızadan (EEPROM) son ayarlanan süreyi yükler.
 * Hafıza boşsa veya bozuksa varsayılan süreyi yükler.
 */
void loadTimeFromEEPROM() {
  byte magic = EEPROM.read(EEPROM_ADDR_MAGIC);
  
  if (magic == EEPROM_MAGIC_VAL) {
    byte loadedUnits = EEPROM.read(EEPROM_ADDR_TIME);
    // Maksimum 99 dakika = 198 birim
    if (loadedUnits <= 198) {
      targetTimeSeconds = (long)loadedUnits * 30;
      return;
    }
  }
  
  // Hafıza geçersizse varsayılan süreyi kullan
  targetTimeSeconds = DEFAULT_TIME_SECONDS;
}

/**
 * Son ayarlanan ses seviyesini kalıcı hafızaya (EEPROM) kaydeder.
 */
void saveVolumeToEEPROM() {
  if (EEPROM.read(EEPROM_ADDR_VOLUME) != volumeLevel) {
    EEPROM.update(EEPROM_ADDR_VOLUME, volumeLevel);
  }
  if (EEPROM.read(EEPROM_ADDR_VOLUME_MAGIC) != EEPROM_VOLUME_MAGIC_VAL) {
    EEPROM.update(EEPROM_ADDR_VOLUME_MAGIC, EEPROM_VOLUME_MAGIC_VAL);
  }
}

/**
 * Cihaz açıldığında kalıcı hafızadan ses seviyesini yükler.
 */
void loadVolumeFromEEPROM() {
  byte magic = EEPROM.read(EEPROM_ADDR_VOLUME_MAGIC);
  if (magic == EEPROM_VOLUME_MAGIC_VAL) {
    byte vol = EEPROM.read(EEPROM_ADDR_VOLUME);
    if (vol >= 1 && vol <= 10) {
      volumeLevel = vol;
      return;
    }
  }
  volumeLevel = 5; // Varsayılan orta ses seviyesi
}

/**
 * Timer 1 kullanarak MOSFET çıkışı üzerinden donanımsal olarak ses üretir.
 * volumeLevel (1-10) arası ayarlanabilir ve PWM doluluk oranı (duty cycle) ile ses şiddetini belirler.
 */
void startTonePWM(unsigned int frequency, int volumeLevel) {
  // Pin modunu çıkış yap
  pinMode(PIN_MOSFET, OUTPUT);
  
  // 16 MHz ana saat frekansı ve Prescaler = 8 ile Timer 1 sayma hızı = 2,000,000 Hz'dir.
  unsigned long top = 2000000UL / frequency;
  
  // Eğer yeni frekans eskisiyle aynıysa hiçbir şey yapma (Gereksiz sıfırlamaları önler)
  if (ICR1 != top) {
    ICR1 = top;
    TCNT1 = 0; // Donanımsal Çözüm: Sayacı sıfırlayarak TCNT1 > ICR1 aşım hatasını (32ms kesinti) engeller!
  }
  
  // Logaritmik/Kuadratik işitme eğrisi kullanarak doluluk oranını belirle
  // OCR1A = top * (volumeLevel^2) / 200 (Level 10 için %50, Level 1 için %0.5 doluluk)
  unsigned long ocr = (top * (unsigned long)(volumeLevel * volumeLevel)) / 200UL;
  if (ocr == 0 && volumeLevel > 0) ocr = 1; // Ses seviyesi 0'dan büyükse en azından 1 adım tetikleme ver
  OCR1A = ocr;
  
  // Timer 1 zaten çalışıyorsa kontrol kayıtçılarını (TCCR1A/B) tekrar yazıp PWM'i sıfırlama!
  // Bu sayede siren ton geçişleri milisaniyelik kesinti (pıtırtı/klik) olmadan tamamen pürüzsüz ve faz-kesintisiz olur.
  if ((TCCR1A & _BV(COM1A1)) == 0) {
    TCCR1A = _BV(COM1A1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11); // Mod 14, Prescaler = 8
  }
}

/**
 * Hoparlör sesini kapatır ve MOSFET pinini tam emniyetli şekilde LOW seviyesine çeker.
 */
void stopTonePWM() {
  TCCR1A = 0; // Donanımsal bağlantıyı kes
  TCCR1B = 0;
  digitalWrite(PIN_MOSFET, LOW); // Emniyet için pini LOW yap (DC akımı keser)
}
