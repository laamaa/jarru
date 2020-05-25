#include <MIDI.h>

/* Configure output/input pins here */
#define PIN_CV_OUT A12
#define PIN_LED_ENABLE 18
#define PIN_LED_TAPTEMPO 19
#define PIN_SW_ENABLE 11
#define PIN_SW_TAPTEMPO 12
#define PIN_POT_DEPTH 21
#define PIN_POT_TIME A6

/* Whether to enable debugging serial console messages. 1=less stuff, 2=everything */
#define DEBUG 2

/* Maximum allowed time between tap tempo presses */
#define MAX_TAP_TIME 3000

/* Time to press the tap tempo switch in order to turn the feature on/off */
#define TAP_SW_ON_OFF_TIME 3000

/* Switch debouncing time in ms */
#define DEBOUNCE_TIME 200

/* MIDI channel setting */
#define MIDI_CHANNEL 16

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
unsigned long tap_timer = 0;
uint16_t tap_interval = 0;
bool enabled = true;
bool sw_enable_down = false;
bool sw_taptempo_down = false;

/* CV envelope processing */
void update_cv()
{
  if (state == ENV_DONE) return;
  
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

inline void trigger_envelope()
{
  trigger_start_time_ms = 0;
  state = ENV_START;
}

/* Read & process hardware controls */
void read_controls()
{
  uint16_t val;
  static unsigned long tap_start_time = 0;
  static unsigned long tap_press_time = 0;
  static unsigned long tmr_sw_enable_debounce = 0;
  static unsigned long tmr_sw_taptempo_debounce = 0;
  static uint16_t val_time;
  static uint16_t val_depth;
  
  /* Enable FX switch */
  if (digitalRead(PIN_SW_ENABLE)) {
    if (sw_enable_down == false) {
      /* Debounce timer to prevent duplicate events from happening when the switch is being pressed */
      if (tmr_sw_enable_debounce == 0 || millis() - tmr_sw_enable_debounce > DEBOUNCE_TIME) {
        #if DEBUG > 0
          Serial.println("SW Enable down");
        #endif
        sw_enable_down = true;
        enabled = !enabled;
        if (!enabled) {
          analogWrite(PIN_CV_OUT,4095);
          analogWrite(PIN_LED_ENABLE,0);
        } else
          analogWrite(PIN_LED_ENABLE,255);
        tmr_sw_enable_debounce = millis();
      }
    } 
  } else {
    sw_enable_down = false;
    if (tmr_sw_enable_debounce != 0 || millis() - tmr_sw_enable_debounce > DEBOUNCE_TIME) tmr_sw_enable_debounce = 0;
  }

  /*  Tap Tempo Switch
   *  If the switch is pressed continously for over 3s, turn off tap tempo
   *  otherwise just calculate the time */
  if (digitalRead(PIN_SW_TAPTEMPO)) {
    if (sw_taptempo_down == false) {
      /* Debounce timer to prevent duplicate events from happening when the switch is being pressed */
      if (tmr_sw_taptempo_debounce == 0 || millis() - tmr_sw_taptempo_debounce > DEBOUNCE_TIME) {
        #if DEBUG > 0
          Serial.println("SW Tap tempo down");
        #endif
        tmr_sw_taptempo_debounce = millis();
        tap_press_time = millis();
        sw_taptempo_down = true;
        if (tap_start_time == 0 || ((millis() - tap_start_time) > MAX_TAP_TIME)) {
          tap_start_time = millis();
        } else {
          if (millis() - tap_start_time > 200) {
            tap_interval = millis() - tap_start_time;
            tap_start_time = 0;
            #if DEBUG > 0
              Serial.printf("Interval: %d\n", tap_interval);
            #endif
          }
        }
        /* Trigger the envelope */
        trigger_envelope();
      }
    } else {
      /* If tap tempo button was already pressed down, check if we should disable the feature */
      if (millis() - tap_press_time > TAP_SW_ON_OFF_TIME) {
        tap_interval = 0;
        tap_start_time = 0;
        leds[LED_TAPTEMPO].state = LED_STATE_OFF;
        #if DEBUG > 0
          Serial.println("Tap tempo off");
        #endif
      }
    }
  } else {
    sw_taptempo_down = false;
    if (tmr_sw_taptempo_debounce != 0 && millis() - tmr_sw_taptempo_debounce > DEBOUNCE_TIME) tmr_sw_taptempo_debounce = 0;
  }

  /* Depth potentiometer */
  val = analogRead(PIN_POT_DEPTH);
  /* The pots are quite noisy when the different currents' grounds are shorted, let's do some filtering... */
  if (val != val_depth && (val > val_depth + 20 || val < val_depth - 20) && (val < val_depth + 30 || val > val_depth -30)) {
    val_depth = val;
    ducking_amount = val*4;
    #if DEBUG > 0
    Serial.printf("Depth: %d\n",val);
    #endif
  }
    
  
  /* Time potentiometer */
  val = analogRead(PIN_POT_TIME);
  /* The pots are quite noisy when the different currents' grounds are shorted, let's do some filtering... */
  if (val != val_time && (val > val_time + 20 || val < val_time - 20) && (val < val_time + 30 || val > val_time -30)) {
    val_time = val;
    hold_time_ms = val;
    release_time_ms = val * 2;
    #if DEBUG > 0
    Serial.printf("Time: %d\n",val);
    #endif
    /* TODO: Adjust hold time also in accordion with release time */
  }
}

/* This checks if the tap tempo interval has passed and triggers the envelope */
void process_tap_tempo()
{
  if (tap_interval == 0) return;

  if (tap_timer == 0)
    tap_timer = millis();
  else if (millis() - tap_timer > tap_interval) {
    tap_timer = 0;
    leds[LED_TAPTEMPO].state = LED_STATE_START;
    if (enabled == true) {
      #if DEBUG > 0
        Serial.println("Tap");
      #endif
      trigger_envelope();
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
}

void setup()
{
  analogWriteResolution(12);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  usbMIDI.setHandleRealTimeSystem(RealTimeSystem);
  analogWrite(PIN_CV_OUT,4095);
  pinMode(PIN_LED_ENABLE,OUTPUT);
  pinMode(PIN_LED_TAPTEMPO,OUTPUT);
  pinMode(PIN_SW_ENABLE,INPUT);
  pinMode(PIN_SW_TAPTEMPO,INPUT);
  pinMode(PIN_POT_DEPTH,INPUT);
  pinMode(PIN_POT_TIME,INPUT);
  read_controls();
  #if DEBUG > 0
    Serial.begin(9600);
    delay(1000);
    Serial.println("Meizzel Machine - Start");
    Serial.printf("Initial time: %d\n",release_time_ms);
    Serial.printf("Initial depth: %d\n",ducking_amount);
    analogWrite(PIN_LED_ENABLE, 255);
    leds[LED_ENABLE].state = LED_STATE_ON;
    analogWrite(PIN_LED_TAPTEMPO, 255); 
    leds[LED_TAPTEMPO].state = LED_STATE_ON;  
  #endif

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
  if (channel != MIDI_CHANNEL) return;
  /* Trigger the envelope on any note on message */
  trigger_envelope();
}

void RealTimeSystem(byte realtimebyte)
{
  static uint8_t clock_count = 0;
  static uint8_t deviating_intervals_count = 0;
  static uint16_t tmr_midi_clock = 0;
  static bool buffer_is_ready = false;
  static uint16_t clock_buffer[96] = {0};
  static uint16_t avg_interval = 0;
  
  /* Midi clock pulse, 24ppq */
  if (realtimebyte == 248) {
    /* Initialize clock timer on first pulse and do nothing else*/
    if (clock_count == 0) {
      tmr_midi_clock = (uint16_t)micros();
      tap_timer = 0;
      clock_count++;
      return;
    }
    if (buffer_is_ready) {
      /* If our clock buffer is ready, we keep things steady and just check for deviating intervals (tempo changes) and then reset the buffer */
    } else {
      uint32_t interval_sum = 0;
      /* If the clock buffer is not ready yet, store the interval between the current and last pulse and calculate the average interval from what we have */
      clock_buffer[clock_count-1] = (uint16_t)micros() - tmr_midi_clock;
      if (clock_count == 96) buffer_is_ready = true; // We have our buffer full, burp.
      for (uint8_t i=0; i < clock_count; i++) {
        interval_sum += clock_buffer[i];
      }
      avg_interval = interval_sum / clock_count;
      #if DEBUG > 1
        double bpm = 60000/(avg_interval/1000*24);
        Serial.printf("RT count: %d avg: %d bpm: %02d",clock_count,avg_interval);
        Serial.println(bpm);
      #endif
    }
    
    tap_interval = avg_interval / 1000 * 24;

    if (clock_count < 96) 
      clock_count++;
    else
      clock_count = 0;
    
    /*
      if (clock_count == 0) tmr_midi_clock_start = micros();
      clock_count++;
      if (clock_count == 24) {
        uint16_t interval_ms = (micros() - tmr_midi_clock_start)/1000;
        if (tap_interval != interval_ms) {
          tap_timer = 0;
          tap_interval = interval_ms;
        }
      }
    */
  }
}
