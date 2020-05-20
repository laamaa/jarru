#define PIN_CV_OUT A12
#define PIN_LED_ENABLE A9
#define PIN_LED_TAPTEMPO A8
#define PIN_SW_ENABLE 11
#define PIN_SW_TAPTEMPO 12

/* CV envelope states */
enum env_state {
  ENV_START,
  ENV_RELEASE,
  ENV_DONE
};

enum env_state state = ENV_DONE;
unsigned long trigger_start_time_ms = 0;
uint16_t hold_time_ms = 200;
uint16_t release_time_ms = 200;
double ducking_amount = 4095.0;
unsigned long tap_start_time = 0;
uint16_t tap_interval = 0;
bool enabled = true;
bool sw_enable_down = false;
bool sw_taptempo_down = false;

void update_cv()
{
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
    double value;
    value = 4095.0 - ducking_amount + ((current_time_ms - trigger_start_time_ms - hold_time_ms) * step_ms);
    if (value > 4095.0)
      value = 4095.0;
    analogWrite(PIN_CV_OUT, int(value));
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
  /* Enable FX switch */
  if (digitalRead(PIN_SW_ENABLE)) {
    if (sw_enable_down == false) {
      sw_enable_down = true;
      enabled = !enabled;
      if (enabled) {} // TODO: Control led
    } 
  } else
    sw_enable_down = false;

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
  } else
    sw_taptempo_down = false;
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
}


void loop()
{
  if (enabled == 1) {
    usbMIDI.read();
    if (state > 0) update_cv();
  }
  read_controls();
}

void OnNoteOn(byte channel, byte pitch, byte velocity)
{
  //trigger the envelope on any note on message
  trigger_start_time_ms = 0;
  state = ENV_START;
}
