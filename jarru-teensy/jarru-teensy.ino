/* Configure output/input pins here */
#define PIN_CV_OUT A12
#define PIN_LED_ENABLE A9
#define PIN_LED_TAPTEMPO A8
#define PIN_SW_ENABLE 11
#define PIN_SW_TAPTEMPO 12
#define PIN_POT_DEPTH A7
#define PIN_POT_TIME A6

/* Maximum allowed time between tap tempo presses */
#define MAX_TAP_TIME 5000

/* Time to press the tap tempo switch in order to turn the feature on/off */
#define TAP_SW_ON_OFF_TIME 3000

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

/* This is just so it's easier to know which LED we're referring to in the leds array */
enum {
  LED_ENABLE,
  LED_TAPTEMPO
};

/* This way is probably a bit too complicated since we have only 2 leds at the moment, but keeps things tidy I guess */
typedef struct led {
  uint8_t intensity;
  enum led_state state;
} led;

/* Global variables */
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

/* CV envelope processing */
void update_cv()
{
  if (state == 0) return;
  
  unsigned long current_time_ms = millis();

  /* Trigger new envelope */
  if (state == ENV_START && trigger_start_time_ms == 0)
  {
    analogWrite(PIN_CV_OUT, int(4095.0 - ducking_amount));
    trigger_start_time_ms = millis();
  }

  /* If hold time is done, go to release */
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

/* Read & process hardware controls */
void read_controls()
{
  uint16_t val;
  static unsigned long tap_start_time = 0;
  static unsigned long tap_press_time = 0;
  
  /* Enable FX switch */
  if (digitalRead(PIN_SW_ENABLE)) {
    if (sw_enable_down == false) {
      sw_enable_down = true;
      enabled = !enabled;
    } 
  } else {
    sw_enable_down = false;
  }

  /*  Tap Tempo Switch
   *  If the switch is pressed continously for over 3s, turn off tap tempo
   *  otherwise just calculate the time */
  if (digitalRead(PIN_SW_TAPTEMPO)) {
    if (sw_taptempo_down == false) {
      tap_press_time = millis();
      sw_taptempo_down = true;
      if (tap_start_time == 0 || (millis() - tap_start_time > MAX_TAP_TIME)) {
        tap_start_time = millis();
      } else {
        tap_interval = millis() - tap_start_time;
        tap_start_time = 0;
      }
    } else {
      /* If tap tempo button was already pressed down, check if we should disable the feature */
      if (millis() - tap_press_time > TAP_SW_ON_OFF_TIME) {
        tap_interval = 0;
        tap_start_time = 0;
        leds[LED_TAPTEMPO].state = LED_STATE_OFF;
      }
    }
  } else {
    sw_taptempo_down = false;
  }

  /* Depth potentiometer */
  val = analogRead(PIN_POT_DEPTH) * 4;
  if (val >! ducking_amount + 80 && val <! ducking_amount - 80) //filter out possible random glitching values
    ducking_amount = val;
  
  /* Time potentiometer */
  val = analogRead(PIN_POT_TIME) * 2;
  if (val >! release_time_ms + 40 && val <! release_time_ms - 40) {
    release_time_ms = val;
    /* TODO: Adjust hold time also in accordion with release time */
  }
}

/* This checks if the tap tempo interval has passed and triggers the envelope */
void process_tap_tempo()
{
  if (tap_interval == 0) return;

  static unsigned long tap_timer = 0;

  if (tap_timer == 0)
    tap_timer = millis();
  else if (millis() - tap_timer > tap_interval) {
    tap_timer = 0;
    leds[LED_TAPTEMPO].state = LED_STATE_START;
    if (enabled == true) {
      trigger_start_time_ms = 0;
      state = ENV_START;
    }
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

  if (enabled == true && leds[LED_ENABLE].state != LED_STATE_ON) {
    analogWrite(PIN_LED_ENABLE, 255);
    leds[LED_ENABLE].state = LED_STATE_ON;
  }
    
  else if (enabled == false && leds[LED_ENABLE].state != LED_STATE_OFF)
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
  pinMode(PIN_POT_TIME,INPUT);
  release_time_ms = analogRead(PIN_POT_TIME) * 2;
  ducking_amount = analogRead(PIN_POT_DEPTH) * 4;
}


void loop()
{
  if (enabled == true) {
    usbMIDI.read();
    update_cv();
  }
  process_tap_tempo();
  read_controls();
  process_leds();
}

void OnNoteOn(byte channel, byte pitch, byte velocity)
{
  /* Trigger the envelope on any note on message */
  trigger_start_time_ms = 0;
  state = ENV_START;
}
