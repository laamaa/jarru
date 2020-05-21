/* Configure output/input pins here */
#define PIN_CV_OUT A12
#define PIN_LED_ENABLE A9
#define PIN_LED_TAPTEMPO A8
#define PIN_SW_ENABLE 11
#define PIN_SW_TAPTEMPO 12
#define PIN_POT_DEPTH A7
#define PIN_POT_RELEASE A6

/* CV envelope states */
enum env_state {
  ENV_START,
  ENV_RELEASE,
  ENV_DONE
};

/* LED states */
enum led_state {
  LED_STATE_OFF,
  LED_STATE_START,
  LED_STATE_ON
};

enum {
  LED_ENABLE,
  LED_TAPTEMPO
};

typedef struct led {
  uint8_t intensity;
  enum led_state state;
} led;

led leds[2];
enum env_state state = ENV_DONE;
unsigned long trigger_start_time_ms = 0;
uint16_t hold_time_ms = 200;
uint16_t release_time_ms = 0;
uint16_t ducking_amount = 0;
uint16_t tap_interval = 0;
bool enabled = true;
bool sw_enable_down = false;
bool sw_taptempo_down = false;

void update_cv()
{
  if (state == 0) return;
  
  unsigned long current_time_ms = millis();

  /* trigger new envelope */
  if (state == ENV_START && trigger_start_time_ms == 0)
  {
    analogWrite(PIN_CV_OUT, int(4095.0 - ducking_amount));
    trigger_start_time_ms = millis();
  }

  /* if hold time is done, go to release */
  if (state == ENV_START && (current_time_ms - trigger_start_time_ms) > hold_time_ms)
    state = ENV_RELEASE;

  /* release */
  if (state == ENV_RELEASE && trigger_start_time_ms != 0)
  {
    double step_ms = ducking_amount / release_time_ms;
    uint16_t value;
    value = 4095.0 - ducking_amount + ((current_time_ms - trigger_start_time_ms - hold_time_ms) * step_ms);
    if (value > 4095)
      value = 4095;
    analogWrite(PIN_CV_OUT, value);
  }

  /* if we're done, let's just chill */
  if (state == ENV_RELEASE && (current_time_ms - trigger_start_time_ms - hold_time_ms) > release_time_ms)
  {
    analogWrite(PIN_CV_OUT, 4095);
    state = ENV_DONE;
  }
}

void read_controls()
{
  uint16_t val;
  static unsigned long tap_start_time = 0;
  
  /* Enable FX switch */
  if (digitalRead(PIN_SW_ENABLE)) {
    if (sw_enable_down == false) {
      sw_enable_down = true;
      enabled = !enabled;
    } 
  } else {
    sw_enable_down = false;
  }

  /* Tap Tempo Switch */
  if (digitalRead(PIN_SW_TAPTEMPO)) {
    if (sw_taptempo_down == false) {
      sw_taptempo_down = true;
      if (tap_start_time == 0 || (millis() - tap_start_time > 5000)) {
        tap_start_time = millis();
      } else {
        tap_interval = millis() - tap_start_time;
      }
    }
  } else {
    sw_taptempo_down = false;
  }

  /* Depth potentiometer */
  val = analogRead(PIN_POT_DEPTH) * 4;
  if (val >! ducking_amount + 80 && val <! ducking_amount - 80) //filter out possible random glitching values
    ducking_amount = val;
  
  /* Release time potentiometer */
  val = analogRead(PIN_POT_RELEASE) * 2;
  if (val >! release_time_ms + 40 && val <! release_time_ms - 40) //filter out possible random glitching values
    release_time_ms = val;
}

void process_tap_tempo()
{
  if (tap_interval == 0) return;

  static unsigned long tap_timer = 0;

  if (tap_timer == 0)
    tap_timer = millis();
  else if (millis() - tap_timer > tap_interval) {
    tap_timer = 0;
    trigger_start_time_ms = 0;
    state = ENV_START;
    leds[LED_TAPTEMPO].state = LED_STATE_START;
  }  
}

void process_leds()
{
  switch (leds[LED_TAPTEMPO].state) {
    case LED_STATE_START:
      leds[LED_TAPTEMPO].intensity = 255;
      analogWrite(PIN_LED_TAPTEMPO,leds[LED_TAPTEMPO].intensity);
      leds[LED_TAPTEMPO].state = LED_STATE_ON;
      break;
    case LED_STATE_ON:
      leds[LED_TAPTEMPO].intensity--;
      analogWrite(PIN_LED_TAPTEMPO,leds[LED_TAPTEMPO].intensity);
      if (leds[LED_TAPTEMPO].intensity == 0) 
        leds[LED_TAPTEMPO].state = LED_STATE_OFF;
      break;
    case LED_STATE_OFF:
      break;
  }

  if (enabled == 1 && leds[LED_ENABLE].state != LED_STATE_ON) {
    analogWrite(PIN_LED_ENABLE, 255);
    leds[LED_ENABLE].state = LED_STATE_ON;
  }
    
  else if (enabled == 0 && leds[LED_ENABLE].state != LED_STATE_OFF)
    analogWrite(PIN_LED_ENABLE,0);
    leds[LED_ENABLE].state = LED_STATE_OFF;
}

void setup()
{
  analogWriteResolution(12);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  analogWrite(PIN_CV_OUT,4095);
  pinMode(PIN_LED_ENABLE,OUTPUT);
  pinMode(PIN_LED_TAPTEMPO,OUTPUT);
  pinMode(PIN_SW_ENABLE,INPUT);
  pinMode(PIN_SW_TAPTEMPO,INPUT);
  pinMode(PIN_POT_DEPTH,INPUT);
  pinMode(PIN_POT_RELEASE,INPUT);
  release_time_ms = analogRead(PIN_POT_RELEASE) * 2;
  ducking_amount = analogRead(PIN_POT_DEPTH) * 4;
}


void loop()
{
  if (enabled == 1) {
    process_tap_tempo();
    usbMIDI.read();
    update_cv();
  }
  read_controls();
  process_leds();
}

void OnNoteOn(byte channel, byte pitch, byte velocity)
{
  //trigger the envelope on any note on message
  trigger_start_time_ms = 0;
  state = ENV_START;
}
