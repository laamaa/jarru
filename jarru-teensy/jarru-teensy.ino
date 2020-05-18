#define CV_OUT_PIN A12

uint8_t state;
unsigned long trigger_start_time_ms = 0;
unsigned int hold_time_ms = 200;
unsigned int release_time_ms = 200;
double ducking_amount = 4095.0;

void updateCv()
{
  unsigned long current_time_ms = millis();

  //trigger new envelope
  if (state == 1 && trigger_start_time_ms == 0)
  {
    analogWrite(CV_OUT_PIN, int(4095.0 - ducking_amount));
    trigger_start_time_ms = millis();
  }

  //if hold time is done, go to release
  if (state == 1 && (current_time_ms - trigger_start_time_ms) > hold_time_ms)
    state = 2;

  //release
  if (state == 2 && trigger_start_time_ms != 0)
  {
    double step_ms = ducking_amount / release_time_ms;
    double value;
    value = 4095.0 - ducking_amount + ((current_time_ms - trigger_start_time_ms - hold_time_ms) * step_ms);
    if (value > 4095.0)
      value = 4095.0;
    analogWrite(CV_OUT_PIN, int(value));
  }

  //if we're done, let's just chill
  if (state == 2 && (current_time_ms - trigger_start_time_ms - hold_time_ms) > release_time_ms)
  {
    analogWrite(CV_OUT_PIN, 4095);
    state = 3;
  }
}

void setup()
{
  analogWriteResolution(12);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  analogWrite(CV_OUT_PIN,4095);
}


void loop()
{
  usbMIDI.read();
  updateCv();
}

void OnNoteOn(byte channel, byte pitch, byte velocity)
{
  //trigger the envelope on any note on message
  trigger_start_time_ms = 0;
  state = 1;
}
