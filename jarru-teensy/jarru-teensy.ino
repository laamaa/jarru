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
#define DEBUG 0

/* Maximum allowed time between tap tempo presses */
#define MAX_TAP_TIME 3000

/* Time to press the tap tempo switch in order to turn the feature on/off */
#define TAP_SW_ON_OFF_TIME 3000

/* Switch debouncing time in ms */
#define DEBOUNCE_TIME 200

/* MIDI channel setting */
#define MIDI_CHANNEL 16

/* MIDI clock buffer reset threshold (in microsec, 1000 = 1ms) */
#define MIDI_CLOCK_DEVIATION_THRESHOLD 1000

/* MIDI clock buffer size */
#define MIDI_CLOCK_BUFFER_SIZE 96

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
uint16_t hold_time_ms = 10;
uint16_t release_time_ms = 0;
uint16_t ducking_amount = 0;
uint16_t val_time = 0;
unsigned long tap_timer = 0; //timer for tap function
unsigned long tap_interval = 0; //pumper interval in micros
bool enabled = true; //is the effect enabled
bool sw_enable_down = false; //is enable switch pressed
bool sw_taptempo_down = false; //is tap tempo switch pressed
bool midi_sync = false; //do we have midi clock sync

/* CV envelope processing */
void update_cv()
{
  if (state == ENV_DONE) return;
  
  unsigned long current_time_ms = millis();

  /* Trigger new envelope */
  if (state == ENV_START && trigger_start_time_ms == 0)
  {
    if (enabled) analogWrite(PIN_CV_OUT, int(4095.0 - ducking_amount));
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
    if (enabled) analogWrite(PIN_CV_OUT, value);
  }

  /* if we're done, let's just chill */
  if (state == ENV_RELEASE && (current_time_ms - trigger_start_time_ms - hold_time_ms) > release_time_ms)
  {
    if (enabled) analogWrite(PIN_CV_OUT, 4095);
    state = ENV_DONE;
  }
}

inline void trigger_envelope()
{
  trigger_start_time_ms = 0;
  state = ENV_START;
}

/* adjust time parameter */
void set_envelope_timing(uint16_t val)
{
  if (val == 0) val = val_time;
    if (tap_interval > 0) {
      /* If there is midi sync or tap tempo defined, we'll use that as the max range for time setting */
      uint16_t range = tap_interval/1000; //tap_interval is in microsec
      hold_time_ms = 10+((double)range/1023)*(val/2);
      release_time_ms = (double)(val+1)/1024*(range-(hold_time_ms/2));
      if (hold_time_ms + release_time_ms > range) release_time_ms = range-hold_time_ms;
      #if DEBUG > 1
      Serial.printf("Time val: %d, range: %d, release: %d, hold: %d\n",val,range,release_time_ms,hold_time_ms);
      #endif
    } else {
      release_time_ms  = val;
    }
}

/* Read & process hardware controls */
void read_controls()
{
  uint16_t val;
  static unsigned long tap_start_time = 0;
  static unsigned long tap_press_time = 0;
  static unsigned long tmr_sw_enable_debounce = 0;
  static unsigned long tmr_sw_taptempo_debounce = 0;
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
            tap_interval = (millis() - tap_start_time)*1000;
            tap_start_time = 0;
            #if DEBUG > 0
              Serial.printf("Interval: %d bpm: %d\n", tap_interval/1000, 60000000/tap_interval);
            #endif
            set_envelope_timing(val_time);
          }
        }
        /* Trigger the envelope */
        tap_timer = 0;
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
    set_envelope_timing(val);
    /* TODO: Adjust hold time also in accordion with release time */
  }
}

/* This checks if the tap tempo interval has passed and triggers the envelope */
void process_tap_tempo()
{
  if (tap_interval == 0) return;

  if (tap_timer == 0)
    tap_timer = micros();
  else if (micros() - tap_timer > tap_interval) {
    tap_timer = 0;
    leds[LED_TAPTEMPO].state = LED_STATE_START;
    if (enabled == true) {
      #if DEBUG > 1
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
  analogWrite(PIN_LED_ENABLE, 255);
  leds[LED_ENABLE].state = LED_STATE_ON;  
  #if DEBUG > 0
    Serial.begin(9600);
    delay(1000);
    Serial.println("Meizzel Machine - Start");
    Serial.printf("Initial time: %d\n",release_time_ms);
    Serial.printf("Initial depth: %d\n",ducking_amount);
    analogWrite(PIN_LED_TAPTEMPO, 255); 
    leds[LED_TAPTEMPO].state = LED_STATE_ON;  
  #endif
}


void loop()
{
  usbMIDI.read();
  update_cv();
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

/*  MIDI clock messages are handled here
 *  Clock messages are buffered to an array of 96 intervals and an average from those is used to calculate the tempo
 *  If an incoming clock message's interval from the last one is lower than higher than our threshold (default 1ms),
 *  the buffer is reset and the average will be calculated from zero again. This way we'll catch tempo changes better
 *  than by just letting the average slide slowly to a new value.
 */
void RealTimeSystem(uint8_t realtimebyte, uint32_t timestamp)
{
  static uint8_t clock_count = 0;
  static unsigned long tmr_midi_clock = 0;
  static uint16_t avg_interval = 0;
  static bool buffer_is_ready = false;
  static uint16_t clock_buffer[MIDI_CLOCK_BUFFER_SIZE] = {0};
  static uint8_t deviating_intervals_count = 0;
  
  switch (realtimebyte) {
    /* MIDI clock pulse */
    case 0xF8:
    {
      uint16_t clockinterval = timestamp - tmr_midi_clock;
      if (buffer_is_ready) {
        /* If our clock buffer is ready, we keep things steady and just check for deviating intervals
           (defined by MIDI_CLOCK_DEVIATION_THRESHOLD, default 1ms) from usual and then reset the buffer so we catch tempo changes better */        
        if (clockinterval > (uint16_t)(avg_interval + MIDI_CLOCK_DEVIATION_THRESHOLD) || clockinterval < (uint16_t)(avg_interval - MIDI_CLOCK_DEVIATION_THRESHOLD)) {
          deviating_intervals_count++;
          #if DEBUG > 1
            Serial.printf("Deviating interval! count:%d interval: %d avg: %d\n",deviating_intervals_count,clockinterval,avg_interval);
          #endif
          tmr_midi_clock = timestamp;
          if (deviating_intervals_count > 2) {
            /* Flush MIDI clock buffer, calculated averages and timer */
            memset(clock_buffer,0,sizeof clock_buffer);
            clock_count = 0;
            avg_interval = 0;
            buffer_is_ready = false;
            deviating_intervals_count = 0;
            tmr_midi_clock = 0;
            midi_sync = false;
            #if DEBUG > 1
              Serial.println("MIDI clock buffer flushed");
            #endif
          }
          return;
        } else
          deviating_intervals_count = 0;
      } else {
        /* Initialize clock timer on first pulse and do nothing else*/
        if (clock_count == 0) {
          tmr_midi_clock = timestamp;
          clock_count++;
          return;
        }
      }

      uint32_t interval_sum = 0;

      clock_buffer[clock_count-1] = clockinterval;
      for (uint8_t i=0; i < MIDI_CLOCK_BUFFER_SIZE; i++)
        interval_sum += clock_buffer[i];
      if (buffer_is_ready) avg_interval = interval_sum / MIDI_CLOCK_BUFFER_SIZE;
      else avg_interval = interval_sum / clock_count;
      #if DEBUG > 1
        double bpm = 60000000/(avg_interval * 24);
        Serial.printf("Clock count: %d avg: %ld bpm: ",clock_count,avg_interval);
        Serial.println(bpm);
      #endif
      tap_interval = avg_interval * 24;
      set_envelope_timing(0);
      tmr_midi_clock = timestamp;

      if (clock_count < MIDI_CLOCK_BUFFER_SIZE)
        clock_count++;
      else {
        clock_count = 1;
        if (!buffer_is_ready) {
          buffer_is_ready = true; // We have our buffer full, burp.
          if (!midi_sync) midi_sync = true;
          #if DEBUG > 1
            Serial.println("Midi clock buffer full");
          #endif        
        }
      }
      break;
    }
    /* MIDI Start / continue message */
    case 0xFA:
    case 0xFB:
      /* Reset tap timer and trigger envelope so we're in da beat */
      tap_timer = 0;
      clock_count = 0;
      trigger_envelope();
      break;
  }
}
