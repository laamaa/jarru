#include <Bounce.h>

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
#define MAX_TAP_TIME 2000

/* Time to press the tap tempo switch in order to turn the feature on/off */
#define TAP_SW_ON_OFF_TIME 3000

/* Switch debouncing time in ms */
#define DEBOUNCE_TIME 100

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
uint16_t hold_time_ms = 10;
uint16_t release_time_ms = 0;
uint16_t ducking_amount = 0;
uint16_t val_time = 0;
unsigned long tap_timer = 0; //timer for tap function
unsigned long tap_interval = 500000; //pumper interval in micros
unsigned long trigger_start_time_ms = 0; //envelope start time millis
bool enabled = true; //is the effect enabled
bool midi_sync = false; //do we have midi clock sync
uint8_t clock_counter = 0; //midi clock counter
uint32_t last_midi_clock_message_time = 0;
Bounce sw_enable(PIN_SW_ENABLE,DEBOUNCE_TIME);
Bounce sw_taptempo(PIN_SW_TAPTEMPO,DEBOUNCE_TIME);

/* CV envelope processing */
void update_cv()
{
  if (state == ENV_DONE) return;
  
  unsigned long current_time_ms = millis();

  /* Trigger new envelope */
  if (state == ENV_START && trigger_start_time_ms == 0) {
    if (enabled) analogWrite(PIN_CV_OUT, int(4095.0 - ducking_amount));
    trigger_start_time_ms = millis();
  }

  /* If hold time is done, go to release */
  if (state == ENV_START && (current_time_ms - trigger_start_time_ms) > hold_time_ms)
    state = ENV_RELEASE;

  /* release */
  if (state == ENV_RELEASE && trigger_start_time_ms != 0) {
    double step_ms = ducking_amount / release_time_ms;
    uint16_t value;
    value = 4095.0 - ducking_amount + ((current_time_ms - trigger_start_time_ms - hold_time_ms) * step_ms);
    if (value > 4095)
      value = 4095;
    if (enabled) analogWrite(PIN_CV_OUT, value);
  }

  /* if we're done, let's just chill */
  if (state == ENV_RELEASE && (current_time_ms - trigger_start_time_ms - hold_time_ms) > release_time_ms) {
    if (enabled) analogWrite(PIN_CV_OUT, 4095);
    state = ENV_DONE;
    trigger_start_time_ms = 0;
  }
}

inline void trigger_envelope()
{
  trigger_start_time_ms = 0;
  tap_timer = 0;
  leds[LED_TAPTEMPO].state = LED_STATE_START;
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
  static uint16_t val_depth;
  
  /* Enable FX switch */
  if (sw_enable.update() && sw_enable.fallingEdge()) {
    #if DEBUG > 0
      Serial.println("SW Enable down");
    #endif
    enabled = !enabled;
    if (!enabled) {
      analogWrite(PIN_CV_OUT,4095); //full volume when fx not enabled
      analogWrite(PIN_LED_ENABLE,0);
    } else
      analogWrite(PIN_LED_ENABLE,255);
  }

  /*  Tap Tempo Switch */
  if (sw_taptempo.update()) {
    if (sw_taptempo.fallingEdge()) {
        #if DEBUG > 0
          Serial.println("SW Tap tempo down");
        #endif
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
        leds[LED_TAPTEMPO].state = LED_STATE_START;
        trigger_envelope();
    }
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

void check_midi_sync() {
  if (midi_sync == true) {
    uint8_t interval = (micros() - last_midi_clock_message_time) / 1000000;
    if (interval > 0) {
      midi_sync = false;
      clock_counter = 0;
      #if DEBUG > 0
      Serial.println("Midi sync lost");
      #endif
    }
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
  check_midi_sync();
}

void OnNoteOn(byte channel, byte pitch, byte velocity)
{
  if (channel != MIDI_CHANNEL) return;
  /* Trigger the envelope on any note on message */
  /* TODO: set ducking amount with velocity */
  trigger_envelope();
}

/* MIDI clock messages are handled here */
void RealTimeSystem(uint8_t realtimebyte, uint32_t timestamp)
{
  static uint32_t first_timestamp = 0;
  static uint8_t play_flag = 0;
  
  switch (realtimebyte) {
    /* MIDI clock pulse */
    case 0xF8:
    {
      if (play_flag == 0) return;
      switch (clock_counter) {
        case 0:
          first_timestamp = timestamp;
          break;
/*          
        case 1:
        case 6:
        case 12:
        case 18:
          if (!midi_sync)
            tap_interval = (timestamp - first_timestamp) * (25-clock_counter);
            
            set_envelope_timing(val_time);
          break;
*/          
        case 24:
          tap_interval = timestamp - first_timestamp;
          set_envelope_timing(val_time);
          #if DEBUG > 1
            double bpm = 60000000/(double)tap_interval;
            Serial.print("Midi BPM: ");
            Serial.println(bpm);
          #endif
          first_timestamp = timestamp;
          clock_counter = 0;
          midi_sync = true;
          break;
      }
      last_midi_clock_message_time = timestamp;
      clock_counter++;
      break;
    }
    /* MIDI Start / continue message */
    case 0xFA:
    case 0xFB:
      play_flag = 1;
      clock_counter = 0;
      /* Reset tap timer and trigger envelope so we're in da beat */
      set_envelope_timing(val_time);
      trigger_envelope();
      tap_timer = 0;
      break;
    /* MIDI Stop message */
    case 0xFC:
      play_flag = 0;
      midi_sync = false;
      clock_counter = 0;
      break;
  }
}
