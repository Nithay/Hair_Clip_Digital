#include <TFT_eSPI.h>
#include <math.h>

TFT_eSPI tft = TFT_eSPI();

const int BUTTON_MODE_PIN    = 0;   // Pulsante destro (BOOT): cambia frase e colore
const int BUTTON_BATTERY_PIN = 14;  // Pulsante sinistro (GPIO 14): click = batteria, 3s = spegni/accendi
const int LCD_POWER_PIN      = 15;  // Alimentazione display in modalita batteria
const int BACKLIGHT_PIN      = 38;  // Retroilluminazione display
const int BATTERY_ADC_PIN    = 4;   // Pin ADC lettura batteria su T-Display S3

struct StatoScroll {
  const char* testo;
  uint16_t coloreTesto;
  uint16_t coloreSfondo;
  uint16_t coloreBordo;
};

StatoScroll stati[] = {
  {"I'M HUNGRY", TFT_BLACK, 0xFB2E, TFT_RED},
  {"I'M HAPPY",  TFT_BLACK, 0xFD20, TFT_DARKGREY}
};

const int numStati = sizeof(stati) / sizeof(stati[0]);
int indiceStato = 0;

bool schermoAcceso = true;
bool mostraFioreBatteria = false;
unsigned long momentoAperturaBatteria = 0;
const unsigned long durataVisualizzazioneMs = 3500;
int ultimiPetaliDisegnati = -1;

// Gestione pressione singola vs pressione lunga (3000 ms) sul pulsante sinistro
const unsigned long LONG_PRESS_MS = 3000;
const unsigned long debounceDelay = 50;

int ultimoLetturaBatt = HIGH;
int statoStabileBatt = HIGH;
unsigned long ultimoDebounceBatt = 0;
unsigned long inizioPressioneBatt = 0;
bool pressioneLungaEseguita = false;

// Debounce pulsante destro
int ultimoLetturaModo = HIGH;
int statoStabileModo = HIGH;
unsigned long ultimoDebounceModo = 0;

// Parametri font e scorrimento
const int TEXT_SIZE = 10;
const int SCROLL_SPEED_MS = 23;
const int STEP_PX = 3;

int posX = 0;
int posY = 0;
int larghezzaTesto = 0;
unsigned long ultimoPassoScroll = 0;

float leggiPercentualeBatteria() {
  uint32_t sommaMv = 0;
  const int campioni = 16;
  for (int i = 0; i < campioni; i++) {
    sommaMv += analogReadMilliVolts(BATTERY_ADC_PIN);
  }
  float vPin = (sommaMv / (float)campioni) / 1000.0f;
  float vBatt = vPin * 2.0f;

  float pct = ((vBatt - 3.30f) / (4.20f - 3.30f)) * 100.0f;
  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

int calcolaPetaliVisibili(float percentuale) {
  if (percentuale <= 0.0f) return 0;
  int petali = (int)floor(percentuale / 20.0f);
  if (petali > 5) petali = 5;
  if (petali < 0) petali = 0;
  return petali;
}

// Disegna un petalo stilizzato simile all'immagine (arrotondato con nervatura centrale)
void disegnaPetaloStile(int cx, int cy, float gradi, int raggioCentro, int distPetalo, int raggioPetalo, bool pieno) {
  float rad = gradi * (PI / 180.0f);
  float cosA = cosf(rad);
  float sinA = sinf(rad);

  int px = cx + (int)roundf(cosA * distPetalo);
  int py = cy + (int)roundf(sinA * distPetalo);

  // Corpo arrotondato del petalo
  if (pieno) {
    tft.fillCircle(px, py, raggioPetalo, TFT_WHITE);
    // Piccolo incavo/dettaglio interno per richiamare il disegno dell'immagine
    int innerX = cx + (int)roundf(cosA * (distPetalo + raggioPetalo * 0.18f));
    int innerY = cy + (int)roundf(sinA * (distPetalo + raggioPetalo * 0.18f));
    tft.fillCircle(innerX, innerY, max(2, (int)(raggioPetalo * 0.42f)), TFT_BLACK);
  } else {
    // Contorno quando il petalo non è ancora attivo (sotto soglia 20%)
    for (int t = -1; t <= 1; t++) {
      tft.drawCircle(px, py, raggioPetalo + t, TFT_WHITE);
    }
  }

  // Nervatura centrale che parte dal bordo del cerchio centrale verso il petalo
  int x1 = cx + (int)roundf(cosA * (raggioCentro - 1));
  int y1 = cy + (int)roundf(sinA * (raggioCentro - 1));
  int x2 = cx + (int)roundf(cosA * (distPetalo - raggioPetalo * 0.25f));
  int y2 = cy + (int)roundf(sinA * (distPetalo - raggioPetalo * 0.25f));
  tft.drawLine(x1, y1, x2, y2, TFT_WHITE);
}

void disegnaFioreBatteria(int petaliVisibili) {
  tft.fillScreen(TFT_BLACK);

  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  int minDim = min(tft.width(), tft.height());

  int raggioCentro = (int)(minDim * 0.11f);
  int distPetalo   = (int)(minDim * 0.27f);
  int raggioPetalo = (int)(minDim * 0.16f);

  // 5 petali simmetrici ogni 72 gradi, partendo dall'alto (-90°)
  for (int i = 0; i < 5; i++) {
    float gradi = -90.0f + (i * 72.0f);
    bool pieno = (i < petaliVisibili);
    disegnaPetaloStile(cx, cy, gradi, raggioCentro, distPetalo, raggioPetalo, pieno);
  }

  // Bottone centrale bianco pieno come nell'immagine
  tft.fillCircle(cx, cy, raggioCentro, TFT_WHITE);
}

void preparaStato(int nuovoIndice) {
  indiceStato = nuovoIndice % numStati;
  StatoScroll s = stati[indiceStato];

  tft.setTextSize(TEXT_SIZE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(s.coloreTesto, s.coloreSfondo);

  larghezzaTesto = tft.textWidth(s.testo);
  posX = tft.width();
  posY = tft.height() / 2;

  if (schermoAcceso && !mostraFioreBatteria) {
    tft.fillScreen(s.coloreSfondo);
    tft.drawFastHLine(0, 2, tft.width(), s.coloreBordo);
    tft.drawFastHLine(0, tft.height() - 3, tft.width(), s.coloreBordo);
  }
}

void impostaSchermo(bool accendi) {
  schermoAcceso = accendi;
  if (schermoAcceso) {
    mostraFioreBatteria = false;
    ultimiPetaliDisegnati = -1;
    digitalWrite(LCD_POWER_PIN, HIGH);
    delay(20);
    tft.writecommand(0x11); // SLPOUT
    delay(120);
    tft.writecommand(0x29); // DISPON
    preparaStato(indiceStato);
    digitalWrite(BACKLIGHT_PIN, HIGH);
  } else {
    mostraFioreBatteria = false;
    ultimiPetaliDisegnati = -1;
    digitalWrite(BACKLIGHT_PIN, LOW);
    tft.writecommand(0x28); // DISPOFF
    tft.writecommand(0x10); // SLPIN
    digitalWrite(LCD_POWER_PIN, LOW);
  }
}

void aggiornaScroll() {
  if (!schermoAcceso || mostraFioreBatteria) {
    return;
  }

  unsigned long ora = millis();
  if (ora - ultimoPassoScroll < SCROLL_SPEED_MS) {
    return;
  }
  ultimoPassoScroll = ora;

  StatoScroll s = stati[indiceStato];
  tft.setTextSize(TEXT_SIZE);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(s.coloreTesto, s.coloreSfondo);

  int margineCancellazione = STEP_PX + 2;
  int xCancellazione = posX + larghezzaTesto;
  if (xCancellazione >= 0 && xCancellazione < tft.width()) {
    int wCanc = min(margineCancellazione, tft.width() - xCancellazione);
    tft.fillRect(xCancellazione, 4, wCanc, tft.height() - 8, s.coloreSfondo);
  }

  posX -= STEP_PX;

  if (posX < -larghezzaTesto) {
    tft.fillRect(0, 4, tft.width(), tft.height() - 8, s.coloreSfondo);
    posX = tft.width();
  }

  tft.drawString(s.testo, posX, posY);
}

void aggiornaVistaBatteria() {
  if (!schermoAcceso || !mostraFioreBatteria) {
    return;
  }

  unsigned long ora = millis();
  if (durataVisualizzazioneMs > 0 && (ora - momentoAperturaBatteria >= durataVisualizzazioneMs)) {
    mostraFioreBatteria = false;
    ultimiPetaliDisegnati = -1;
    preparaStato(indiceStato);
    return;
  }

  float pct = leggiPercentualeBatteria();
  int petali = calcolaPetaliVisibili(pct);
  if (petali != ultimiPetaliDisegnati) {
    ultimiPetaliDisegnati = petali;
    disegnaFioreBatteria(petali);
  }
}

void gestisciPulsanteSinistro() {
  unsigned long ora = millis();
  int lettura = digitalRead(BUTTON_BATTERY_PIN);

  if (lettura != ultimoLetturaBatt) {
    ultimoDebounceBatt = ora;
  }

  if ((ora - ultimoDebounceBatt) > debounceDelay) {
    if (lettura != statoStabileBatt) {
      statoStabileBatt = lettura;

      if (statoStabileBatt == LOW) {
        // Appena premuto
        inizioPressioneBatt = ora;
        pressioneLungaEseguita = false;
      } else {
        // Appena rilasciato
        if (!pressioneLungaEseguita) {
          // Click breve (< 3 secondi)
          if (!schermoAcceso) {
            impostaSchermo(true);
          } else {
            mostraFioreBatteria = !mostraFioreBatteria;
            if (mostraFioreBatteria) {
              momentoAperturaBatteria = ora;
              ultimiPetaliDisegnati = -1;
              aggiornaVistaBatteria();
            } else {
              ultimiPetaliDisegnati = -1;
              preparaStato(indiceStato);
            }
          }
        }
      }
    }

    // Se è tenuto premuto per 3 secondi mentre è acceso -> spegne il display
    if (statoStabileBatt == LOW && !pressioneLungaEseguita && schermoAcceso) {
      if ((ora - inizioPressioneBatt) >= LONG_PRESS_MS) {
        pressioneLungaEseguita = true;
        impostaSchermo(false);
      }
    }
  }

  ultimoLetturaBatt = lettura;
}

void gestisciPulsanteDestro() {
  unsigned long ora = millis();
  int lettura = digitalRead(BUTTON_MODE_PIN);

  if (lettura != ultimoLetturaModo) {
    ultimoDebounceModo = ora;
  }

  if ((ora - ultimoDebounceModo) > debounceDelay) {
    if (lettura != statoStabileModo) {
      statoStabileModo = lettura;
      if (statoStabileModo == LOW && schermoAcceso) {
        mostraFioreBatteria = false;
        ultimiPetaliDisegnati = -1;
        preparaStato(indiceStato + 1);
      }
    }
  }

  ultimoLetturaModo = lettura;
}

void setup() {
  pinMode(LCD_POWER_PIN, OUTPUT);
  digitalWrite(LCD_POWER_PIN, HIGH);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  pinMode(BUTTON_MODE_PIN, INPUT_PULLUP);
  pinMode(BUTTON_BATTERY_PIN, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);

  preparaStato(0);
}

void loop() {
  gestisciPulsanteSinistro();
  gestisciPulsanteDestro();

  if (mostraFioreBatteria) {
    aggiornaVistaBatteria();
  } else {
    aggiornaScroll();
  }
}