/**
 * KİTCHEN COUNTDOWN TIMER / MUTFAK GERİ SAYIM ALARMI (ŞARJLI / BATARYALI SÜRÜM)
 * Donanım: Arduino Nano, TM1637 Display, Rotary Encoder, MOSFET Sürücü & 4 Ohm 5W Hoparlör,
 *          18650 2200 mAh Li-ion Pil & LX-LCBST (TP4056 + DC-DC 5V Yükseltici Modülü)
 * 
 * ÖNEMLİ ELEKTRİKSEL NOT:
 * 4 Ohm 5W hoparlörün MOSFET ile 5V'ta güvenli çalışması için ~10 Ohm (1W) 
 * bir direnç seri bağlanmalıdır. Bu direnç hoparlörünüzü ve MOSFET'inizi güvenle korur.
 * 
 * BATARYA & ŞARJ YÖNETİMİ:
 * - A0 Pini: 18650 Pil (B+) Voltaj Okuma (3.0V - 4.2V -> %0 - %100)
 * - A1 Pini: Type-C Şarj Girişi Algılama (IN+)
 * - 2 saniye basılı tutulduğunda 1 saniye boyunca ekranda anlık pil yüzdesi gösterilir (Örn: b-85).
 * - Batarya %10'un altına düştüğünde her yeni zaman ayarlama girişiminde "Lo" yanıp söner ve uyarı bip sesi verir.
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
#define PIN_BATTERY_SENSE A0 // 18650 Batarya Voltaj Okuma Pini (B+) [YENİ]
#define PIN_CHARGER_SENSE A1 // Type-C Şarj Girişi Algılama Pini (IN+) [YENİ]

// EEPROM Bellek Adresleri
#define EEPROM_ADDR_MAGIC        10  // Belleğin daha önce yazılıp yazılmadığını kontrol etmek için sihirli bayt
#define EEPROM_ADDR_TIME         0   // Sürenin kaydedileceği adres (unsigned long veya int)
#define EEPROM_MAGIC_VAL         0xA6
#define EEPROM_ADDR_VOLUME       1   // Ses seviyesinin kaydedileceği adres
#define EEPROM_ADDR_VOLUME_MAGIC 11  // Ses seviyesi geçerlilik magic adresi
#define EEPROM_VOLUME_MAGIC_VAL  0x5A

// Sınır Değerler (Süre dakika cinsinden ayarlandığı için 0-99 dk arası)
#define MAX_MINUTES 99
#define MIN_MINUTES 0
#define DEFAULT_TIME_SECONDS 0 // Varsayılan başlangıç süresi 0 dakika (0 saniye)

// Sistem Durumları (State Machine)
enum SystemState {
  STATE_STANDBY,        // Bekleme Modu (Son süre gösterilir, noktalar yavaşça yanıp söner)
  STATE_ADJUSTING,      // Süre Ayarlama Modu (Döndürüldüğünde girilir, süre ayarlanır)
  STATE_COUNTDOWN,      // Geri Sayım Aktif
  STATE_PAUSED,         // Geri Sayım Duraklatıldı
  STATE_ALARM,          // Alarm Aktif (Süre bitti, hoparlör ötüyor)
  STATE_VOLUME_SETTING, // Ses Seviyesi Ayarlama Modu
  STATE_BATTERY_SHOW    // Batarya Yüzdesi Gösterim Modu (1 saniye sürer) [YENİ]
};

// Enkoder Buton Olayları
enum ButtonEvent {
  BTN_NONE,
  BTN_SHORT,     // Kısa basış (<1 sn)
  BTN_LONG,      // 1 saniye basış (Ses Ayarı)
  BTN_VERY_LONG  // 2 saniye basış (1 sn Batarya Yüzdesi Gösterimi) [YENİ]
};

// Global Değişkenler
SystemState currentState = STATE_STANDBY;
SystemState previousState = STATE_STANDBY; // Ses/Batarya ayarlarından geri dönüş için
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
unsigned long batteryShowStartTime = 0; // 1 saniyelik batarya ekranı sayacı [YENİ]
unsigned long lowBatteryAlertTime = 0;  // Düşük batarya Lo uyarısı sayacı [YENİ]
bool showingLowBatteryAlert = false;

// Alarm Ses Ritmi Değişkenleri
unsigned long lastAlarmActionTime = 0;
int alarmPatternState = 0; 

// Alarm Sırasında Ekran Efekti Değişkenleri
unsigned long lastAlarmDisplayToggle = 0;
bool alarmDisplayToggleState = false;

// TM1637 Segment Tanımları
const uint8_t SEG_END[] = { 0x79, 0x54, 0x5E, 0x00 }; // "End "
const uint8_t SEG_ALRT[] = { 0x77, 0x38, 0x50, 0x78 }; // "ALrt"
const uint8_t SEG_LO[]   = { 0x00, 0x38, 0x5C, 0x00 }; // " Lo " (Düşük Batarya) [YENİ]
const uint8_t SEG_CHRG[] = { 0x58, 0x74, 0x50, 0x6F }; // "chrg" (Şarj Oluyor) [YENİ]
const uint8_t SEG_FULL[] = { 0x71, 0x3E, 0x38, 0x38 }; // "FULL" (Tam Dolu) [YENİ]

// Ses Ayarı Değişkenleri
int volumeLevel = 5; // Varsayılan ses seviyesi (1-10 arası, 5 = orta)

// --- FONKSİYON PROTOTİPLERİ ---
void encoderISR();
ButtonEvent checkButton();
void updateDisplay();
void handleAlarmSpeaker();
void saveTimeToEEPROM();
void loadTimeFromEEPROM();
void saveVolumeToEEPROM();
void loadVolumeFromEEPROM();
void startTonePWM(unsigned int frequency, int volumeLevel);
void stopTonePWM();
int getBatteryPercentage();
bool isChargerPlugged();

void setup() {
  // Teşhis için dahili LED çıkış yapılıyor
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  // Pin modları ayarlanıyor
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  pinMode(PIN_BATTERY_SENSE, INPUT);
  pinMode(PIN_CHARGER_SENSE, INPUT);
  
  // Hoparlör pini çıkış yapılıyor ve LOW tutuluyor
  pinMode(PIN_MOSFET, OUTPUT);
  digitalWrite(PIN_MOSFET, LOW);

  // TM1637 Ekranı Başlatılıyor
  display.setBrightness(4);
  
  // Kalıcı hafızadan son süreyi ve ses seviyesini yükle
  loadTimeFromEEPROM();
  loadVolumeFromEEPROM();
  remainingSeconds = targetTimeSeconds;

  // Enkoder Kesmesi
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encoderISR, CHANGE);
  
  lastStateChangeTime = millis();
}

void loop() {
  digitalWrite(LED_BUILTIN, digitalRead(PIN_ENC_SW) == LOW ? HIGH : LOW);

  unsigned long currentMillis = millis();
  int batPct = getBatteryPercentage();

  // 1. Döner Enkoder Dönüş Hareketinin İşlenmesi
  if (encoderMoved) {
    noInterrupts();
    int delta = encoderDelta;
    encoderDelta = 0;
    encoderMoved = false;
    interrupts();

    if (delta != 0) {
      lastStateChangeTime = currentMillis;

      // Batarya %10'un altındaysa her zaman ayarlama girişiminde Lo uyarısı ve bip sesi ver [YENİ]
      if (batPct < 10 && !isChargerPlugged()) {
        showingLowBatteryAlert = true;
        lowBatteryAlertTime = currentMillis;
        startTonePWM(2000, volumeLevel);
        delay(80);
        stopTonePWM();
      }

      if (currentState == STATE_VOLUME_SETTING) {
        volumeLevel += delta;
        if (volumeLevel > 10) volumeLevel = 10;
        if (volumeLevel < 1) volumeLevel = 1;

        startTonePWM(3000, volumeLevel);
        delay(50);
        stopTonePWM();
      } 
      else {
        if (currentState == STATE_ALARM) {
          currentState = STATE_STANDBY;
          remainingSeconds = targetTimeSeconds;
        } 
        else if (currentState == STATE_COUNTDOWN || currentState == STATE_PAUSED) {
          currentState = STATE_ADJUSTING;
        }
        else if (currentState == STATE_STANDBY) {
          currentState = STATE_ADJUSTING;
        }

        // Dinamik adım boyutu
        if (delta > 0) {
          if (targetTimeSeconds < 30) {
            targetTimeSeconds = 30;
          } else if (targetTimeSeconds < 60) {
            targetTimeSeconds = 60;
          } else {
            long stepSeconds = (targetTimeSeconds >= 30 * 60) ? 300 : 60;
            targetTimeSeconds += stepSeconds;
          }
        } else {
          if (targetTimeSeconds <= 30) {
            targetTimeSeconds = 0;
          } else if (targetTimeSeconds <= 60) {
            targetTimeSeconds = 30;
          } else {
            long stepSeconds = (targetTimeSeconds > 30 * 60) ? 300 : 60;
            targetTimeSeconds -= stepSeconds;
          }
        }

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

    // Batarya %10'un altındaysa buton basışında da uyarı ver
    if (batPct < 10 && !isChargerPlugged() && btn == BTN_SHORT) {
      showingLowBatteryAlert = true;
      lowBatteryAlertTime = currentMillis;
      startTonePWM(2000, volumeLevel);
      delay(80);
      stopTonePWM();
    }

    if (btn == BTN_VERY_LONG) {
      // 2 Saniye Uzun Basış: 1 saniye boyunca anlık batarya yüzdesini göster [YENİ]
      if (currentState != STATE_BATTERY_SHOW) {
        previousState = currentState;
        currentState = STATE_BATTERY_SHOW;
        batteryShowStartTime = currentMillis;
        startTonePWM(2500, volumeLevel);
        delay(60);
        stopTonePWM();
      }
    }
    else {
      switch (currentState) {
        case STATE_STANDBY:
          if (btn == BTN_SHORT) {
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

        case STATE_ADJUSTING:
          if (btn == BTN_SHORT) {
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
          currentState = STATE_STANDBY;
          remainingSeconds = targetTimeSeconds;
          break;

        case STATE_VOLUME_SETTING:
          saveVolumeToEEPROM();
          startTonePWM(3500, volumeLevel);
          delay(150);
          stopTonePWM();
          currentState = previousState;
          if (currentState == STATE_COUNTDOWN) {
            lastCountdownTick = millis();
          }
          break;

        case STATE_BATTERY_SHOW:
          // Butona tekrar basılırsa batarya ekranından erken çık
          currentState = previousState;
          break;
      }
    }
  }

  // 3. Durum Yönetimi ve Zamanlayıcı Mantığı
  switch (currentState) {
    case STATE_BATTERY_SHOW:
      // 1 saniye (1000 ms) geçince otomatik olarak önceki duruma dön [YENİ]
      if (currentMillis - batteryShowStartTime >= 1000) {
        currentState = previousState;
      }
      break;

    case STATE_ADJUSTING:
      if (currentMillis - lastStateChangeTime > 30000) {
        currentState = STATE_STANDBY;
      }
      break;

    case STATE_COUNTDOWN:
      if (currentMillis - lastCountdownTick >= 1000) {
        lastCountdownTick += 1000;
        if (remainingSeconds > 0) {
          remainingSeconds--;
        }
        
        if (remainingSeconds <= 0) {
          currentState = STATE_ALARM;
          alarmPatternState = 0;
          lastAlarmActionTime = currentMillis;
          lastAlarmDisplayToggle = currentMillis;
        }
      }
      break;

    case STATE_VOLUME_SETTING:
      if (currentMillis - lastStateChangeTime > 30000) {
        saveVolumeToEEPROM();
        startTonePWM(3500, volumeLevel);
        delay(150);
        stopTonePWM();
        currentState = previousState;
        if (currentState == STATE_COUNTDOWN) {
          lastCountdownTick = millis();
        }
      }
      break;

    case STATE_STANDBY:
      break;

    case STATE_PAUSED:
      if (currentMillis - lastStateChangeTime > 30000) {
        remainingSeconds = targetTimeSeconds;
        currentState = STATE_STANDBY;
      }
      break;

    case STATE_ALARM:
      break;
  }

  // Low battery Lo uyarısının 600ms sonra kendiliğinden kapanması
  if (showingLowBatteryAlert && (currentMillis - lowBatteryAlertTime >= 600)) {
    showingLowBatteryAlert = false;
  }

  // Alarm ses rutinini sürekli çalıştır
  handleAlarmSpeaker();

  // 4. Ekranın Güncellenmesi
  updateDisplay();
}

/**
 * Donanımsal Kesme Servisi (ISR)
 */
void encoderISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();
  
  if (interruptTime - lastInterruptTime > 30) {
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

/**
 * Enkoder Butonunun Okunması ve Debounce Edilmesi
 * BTN_SHORT (<1s), BTN_LONG (1s), BTN_VERY_LONG (2s)
 */
ButtonEvent checkButton() {
  static bool lastStableState = HIGH;
  static bool lastRawState = HIGH;
  static unsigned long lastDebounceTime = 0;
  static unsigned long buttonPressTime = 0;
  static int pressPhase = 0; // 0: nötr, 1: Long (1s) verildi, 2: VeryLong (2s) verildi

  bool rawState = digitalRead(PIN_ENC_SW);
  ButtonEvent event = BTN_NONE;
  unsigned long currentMillis = millis();

  if (rawState != lastRawState) {
    lastDebounceTime = currentMillis;
  }
  lastRawState = rawState;

  if ((currentMillis - lastDebounceTime) > 50) {
    if (rawState != lastStableState) {
      lastStableState = rawState;

      if (lastStableState == LOW) {
        buttonPressTime = currentMillis;
        pressPhase = 0;
      } 
      else {
        if (pressPhase == 0) {
          unsigned long pressDuration = currentMillis - buttonPressTime;
          if (pressDuration < 1000) {
            event = BTN_SHORT;
          }
        }
      }
    }
  }

  if (lastStableState == LOW) {
    unsigned long pressDuration = currentMillis - buttonPressTime;
    if (pressPhase == 0 && pressDuration >= 1000 && pressDuration < 2000) {
      pressPhase = 1;
      event = BTN_LONG;
    }
    else if (pressPhase == 1 && pressDuration >= 2000) {
      pressPhase = 2;
      event = BTN_VERY_LONG;
    }
  }

  return event;
}

/**
 * Batarya Voltajından Yüzdeye (%0 - %100) Dönüşüm
 */
int getBatteryPercentage() {
  int rawADC = analogRead(PIN_BATTERY_SENSE);
  float voltage = (rawADC * 5.0) / 1023.0;
  
  if (voltage >= 4.20) return 100;
  if (voltage <= 3.00) return 0;
  
  int percent = (int)((voltage - 3.00) * 100.0 / (4.20 - 3.00));
  if (percent > 100) percent = 100;
  if (percent < 0) percent = 0;
  return percent;
}

/**
 * Şarj Kablosu Takılı mı Kontrol Et
 */
bool isChargerPlugged() {
  int rawADC = analogRead(PIN_CHARGER_SENSE);
  return (rawADC > 600); // ~3.0V üzeri okuma şarj takılı olduğunu gösterir
}

/**
 * Sistem Durumuna Göre TM1637 Ekranının Güncellenmesi
 */
void updateDisplay() {
  unsigned long currentMillis = millis();
  
  static SystemState lastState = (SystemState)-1;
  static bool lastColonState = false;
  static bool lastAlarmToggle = false;
  static int lastVol = -1;
  static long lastRemSecs = -1;
  static long lastTarSecs = -1;
  static bool lastLowAlertState = false;

  bool stateChanged = (currentState != lastState);
  if (stateChanged) {
    lastState = currentState;
    display.clear();
  }

  // Düşük batarya uyarısı öncelikli gösterimi (Lo uyarısı aktifken)
  if (showingLowBatteryAlert) {
    if (!lastLowAlertState) {
      lastLowAlertState = true;
      display.setSegments(SEG_LO); // " Lo "
    }
    return;
  }
  lastLowAlertState = false;

  switch (currentState) {
    case STATE_BATTERY_SHOW: {
      // 2 saniye basıldığında 1 saniye boyunca ekranda anlık pil yüzdesini göster (Örn: b-85 veya b100)
      display.setBrightness(7);
      int pct = getBatteryPercentage();
      uint8_t segs[4];
      segs[0] = 0x7C; // 'b'
      segs[1] = 0x40; // '-'
      if (pct >= 100) {
        segs[1] = display.encodeDigit(1);
        segs[2] = display.encodeDigit(0);
        segs[3] = display.encodeDigit(0);
      } else if (pct < 10) {
        segs[2] = 0x00;
        segs[3] = display.encodeDigit(pct);
      } else {
        segs[2] = display.encodeDigit(pct / 10);
        segs[3] = display.encodeDigit(pct % 10);
      }
      display.setSegments(segs);
      break;
    }

    case STATE_STANDBY: {
      display.setBrightness(1);
      
      if (currentMillis - lastColonToggle >= 800) {
        lastColonToggle = currentMillis;
        colonState = !colonState;
      }
      
      int mins = targetTimeSeconds / 60;
      int secs = targetTimeSeconds % 60;
      
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
      display.setBrightness(7);
      int mins = remainingSeconds / 60;
      int secs = remainingSeconds % 60;
      
      if (stateChanged || remainingSeconds != lastRemSecs) {
        lastRemSecs = remainingSeconds;
        display.showNumberDecEx(mins * 100 + secs, 0b01000000, true);
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
          display.showNumberDecEx(0, 0b01000000, true);
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
          segments[2] = 0x00;
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
 * MOSFET Hoparlör Sürücü Rutini
 */
void handleAlarmSpeaker() {
  static bool wasAlarmRunning = false;
  unsigned long currentMillis = millis();
  unsigned long elapsed = currentMillis - lastAlarmActionTime;

  if (currentState != STATE_ALARM) {
    if (wasAlarmRunning) {
      stopTonePWM();
      alarmPatternState = 0;
      wasAlarmRunning = false;
    }
    return;
  }

  wasAlarmRunning = true;

  switch (alarmPatternState) {
    case 0:
      startTonePWM(2500, volumeLevel); 
      lastAlarmActionTime = currentMillis;
      alarmPatternState = 1;
      break;
      
    case 1:
      if (elapsed >= 100) {
        startTonePWM(3500, volumeLevel);
        lastAlarmActionTime = currentMillis;
        alarmPatternState = 2;
      }
      break;
      
    case 2:
      if (elapsed >= 100) {
        alarmPatternState = 0;
      }
      break;
  }
}

void saveTimeToEEPROM() {
  byte unitsToSave = targetTimeSeconds / 30;
  if (EEPROM.read(EEPROM_ADDR_TIME) != unitsToSave) {
    EEPROM.update(EEPROM_ADDR_TIME, unitsToSave);
  }
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC_VAL) {
    EEPROM.update(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_VAL);
  }
}

void loadTimeFromEEPROM() {
  byte magic = EEPROM.read(EEPROM_ADDR_MAGIC);
  if (magic == EEPROM_MAGIC_VAL) {
    byte loadedUnits = EEPROM.read(EEPROM_ADDR_TIME);
    if (loadedUnits <= 198) {
      targetTimeSeconds = (long)loadedUnits * 30;
      return;
    }
  }
  targetTimeSeconds = DEFAULT_TIME_SECONDS;
}

void saveVolumeToEEPROM() {
  if (EEPROM.read(EEPROM_ADDR_VOLUME) != volumeLevel) {
    EEPROM.update(EEPROM_ADDR_VOLUME, volumeLevel);
  }
  if (EEPROM.read(EEPROM_ADDR_VOLUME_MAGIC) != EEPROM_VOLUME_MAGIC_VAL) {
    EEPROM.update(EEPROM_ADDR_VOLUME_MAGIC, EEPROM_VOLUME_MAGIC_VAL);
  }
}

void loadVolumeFromEEPROM() {
  byte magic = EEPROM.read(EEPROM_ADDR_VOLUME_MAGIC);
  if (magic == EEPROM_VOLUME_MAGIC_VAL) {
    byte vol = EEPROM.read(EEPROM_ADDR_VOLUME);
    if (vol >= 1 && vol <= 10) {
      volumeLevel = vol;
      return;
    }
  }
  volumeLevel = 5;
}

void startTonePWM(unsigned int frequency, int volumeLevel) {
  pinMode(PIN_MOSFET, OUTPUT);
  unsigned long top = 2000000UL / frequency;
  
  if (ICR1 != top) {
    ICR1 = top;
    TCNT1 = 0;
  }
  
  unsigned long ocr = (top * (unsigned long)(volumeLevel * volumeLevel)) / 200UL;
  if (ocr == 0 && volumeLevel > 0) ocr = 1;
  OCR1A = ocr;
  
  if ((TCCR1A & _BV(COM1A1)) == 0) {
    TCCR1A = _BV(COM1A1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);
  }
}

void stopTonePWM() {
  TCCR1A = 0;
  TCCR1B = 0;
  digitalWrite(PIN_MOSFET, LOW);
}
