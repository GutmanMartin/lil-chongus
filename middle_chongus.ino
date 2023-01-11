#include <MIDI.h>  // by Francois Best
//#include <Debounce.h>
MIDI_CREATE_DEFAULT_INSTANCE();

class Button {
private:
  uint8_t btn;
  uint16_t state;
public:
  void begin(uint8_t button) {
    btn = button;
    state = 0;
    pinMode(btn, INPUT_PULLUP);
  }
  bool debounce() {
    state = (state << 1) | digitalRead(btn) | 0xfe00;
    return (state == 0xff00);
  }
};

/*
  MIDI CC
  127 = Tempo (continuo)
  126 = BPM monitor on/off (Digital)
    Capaz hay algo con bpm en midi

  red button: channel 16, note 0
  channel 15 is for the effects

  channels 1 y 2: modo loops
    pots van por 1
  channels 3 y 4: modo efectos
    pots van por 3

  pines:
    botones principales: 2 - 17
    pageLeft: 43
    pageDown: 41
    pageRight: 39
    effects: 37
    bpmmonitor: 35
    red one: 59

*/

//! consts to change behaivour
const bool DO_EFFECTS_ON_BUTTONS_SEND_NOTES = false;
const bool ARE_EFFECT_KNOBS_TIED_TO_PAGE_NUMBER = false;


// BUTTONS

const int EFFECTS_INTERRUPTOR = 37;
const int BPM_INTERRUPTOR = 35;
const int RED_BUTTON = 59;

const int NButtons = 16 + 2 + 1;
const int buttonPin[NButtons] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, EFFECTS_INTERRUPTOR, BPM_INTERRUPTOR, RED_BUTTON };

int buttonCState[NButtons] = {};  // stores the button current value
int buttonPState[NButtons] = {};  // stores the button previous value

// debounce
unsigned long lastDebounceTime[NButtons] = { 0 };  // the last time the output pin was toggled
unsigned long debounceDelay = 50;                  //* the debounce time; increase if the output flickers


// pages
Button pageLeft;
Button pageRight;
Button pageDown;

const int MAX_PAGES = 8;
// if this is more than 8 it overflows, and returns to 0

int page = 0;
int isPageDown = 1;



// EFFECTS
//int selectedEffects[MAX_PAGES * NPots - 1];
bool isEffectsOn = false;
int selectedEffectPerColumn[MAX_PAGES * 4] = {0};




// KNOBS
const int BPM_POT = A4;

const int NPots = 5;                               //*** total numbers of pots (slide & rotary)
const int potPin[NPots] = {A0, A1, A2, A3, BPM_POT};  //*** Analog pins of each pot connected straight to the Arduino i.e 4 pots, {A3, A2, A1, A0};
                                                   // have nothing in the array if 0 pots {}

int potCState[NPots] = { 0 };  // Current state of the pot; delete 0 if 0 pots
int potPState[NPots] = { 0 };  // Previous state of the pot; delete 0 if 0 pots
int potVar = 0;                // Difference between the current and previous state of the pot

int midiCState[NPots] = { 0 }; // Current state of the midi value; delete 0 if 0 pots
int midiPState[NPots] = { 0 }; // Previous state of the midi value; delete 0 if 0 pots

const int TIMEOUT = 300;             //** Amount of time the potentiometer will be read after it exceeds the varThreshold
const int varThreshold = 10;         //** Threshold for the potentiometer signal variation
boolean potMoving = true;            // If the potentiometer is moving
unsigned long PTime[NPots] = { 0 };  // Previously stored time; delete 0 if 0 pots
unsigned long timer[NPots] = { 0 };  // Stores the time that has elapsed since the timer was reset; delete 0 if 0 pots



void setup() {
  Serial.begin(115200);  //**  Baud Rate 31250 for MIDI class compliant jack | 115200 for Hairless MIDI
  for (int i = 0; i < NButtons; i++) {
    pinMode(buttonPin[i], INPUT_PULLUP);
  }
  pageLeft.begin(43);
  pageDown.begin(41);
  pageRight.begin(39);
}

void loop() {


  debouncePots();
  debounceButtons();
  pages();
}


void debounceButtons() {
  //Serial.println("debounce buttons called");
  for (int i = 0; i < NButtons; i++) {
    buttonCState[i] = digitalRead(buttonPin[i]);  // read pins from arduino
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (buttonPState[i] != buttonCState[i]) {
        lastDebounceTime[i] = millis();

        handleButtons(buttonPin[i], buttonCState[i]);  // creo que esto va a funcionar, pero si no seguro que el if de abajo funciona, aunque es redundante
    /*  if (buttonCState[i] == LOW) {
          handleButtons(i, true);
        } else {
          handleButtons(i, false); 
        }*/
        buttonPState[i] = buttonCState[i];
      }
    }
  }
}


void handleButtons(int pin, uint8_t value) {
  //Serial.println(pin);

  switch (pin) {
    case EFFECTS_INTERRUPTOR:
      isEffectsOn = !isEffectsOn;
    break;
    case RED_BUTTON:
      if (value == LOW) {
        MIDI.sendNoteOn(0, 127, 16);
      } else {
        MIDI.sendNoteOn(0, 0, 16);
      }
      break;
    case BPM_INTERRUPTOR:
      if (value == LOW) {
        MIDI.sendControlChange(126, 127, 1);
      } else {
        MIDI.sendControlChange(126, 0, 1);
      }
      break;
    default:
      if (isEffectsOn) {
        handleMainButtonsWithEffectsON(pin, value);
      } else {
        handleMainButtonsWithEffectsOFF(pin, value);
      }
  }

}


void handleMainButtonsWithEffectsOFF(int pin, uint8_t value) {
  if (value == LOW) {
    MIDI.sendNoteOn(pin - 2 + page * 16, 127, isPageDown);
    // note, velocity, channel
  } else {
    MIDI.sendNoteOn(pin - 2 + page * 16, 0, isPageDown);
  }
}


void handleMainButtonsWithEffectsON(int pin, uint8_t value) {
  pin = pin - 2;
  int column = pin % 4 + page * 4;
  int n = (pin - pin % 4) / 4;
  selectedEffectPerColumn[column] = n;

  if (DO_EFFECTS_ON_BUTTONS_SEND_NOTES) {
  // when effect is on, buttons still send notes (in another channel)
    if (value == LOW) {
      MIDI.sendNoteOn(column * 4 + n, 127, isPageDown+2);
    } else {
      MIDI.sendNoteOn(column * 4 + n, 0, isPageDown+2);
    }
  } // else, it doesn't do nothing
}
// selectedEffectPerColumn[MAX_PAGES * 4] = {0};

void debouncePots() {
  for (int i = 0; i < NPots; i++) {                      // Loops through all the potentiometers
    potCState[i] = analogRead(potPin[i]);                // reads the pins from arduino
    midiCState[i] = map(potCState[i], 0, 1023, 0, 127);  // Maps the reading of the potCState to a value usable in midi
    potVar = abs(potCState[i] - potPState[i]);           // Calculates the absolute value between the difference between the current and previous state of the pot
    if (potVar > varThreshold) {                         // Opens the gate if the potentiometer variation is greater than the threshold
      PTime[i] = millis();                               // Stores the previous time
    }
    timer[i] = millis() - PTime[i];  // Resets the timer 11000 - 11000 = 0ms

    if (timer[i] < TIMEOUT) {        // If the timer is less than the maximum allowed time it means that the potentiometer is still moving
      potMoving = true;
    } else {
      potMoving = false;
    }

    if (potMoving == true) {                // If the potentiometer is still moving, send the change control
      if (midiPState[i] != midiCState[i]) {
        
        handlePots(i, 127 - midiCState[i]); // sends to handler

        potPState[i] = potCState[i];        // Stores the current reading of the potentiometer to compare with the next
        midiPState[i] = midiCState[i];
      }
    }
  }
}


void handlePots(int pot, int value) {
    //Serial.println(pot);
    //MIDI.sendControlChange(/*pot + page * NPots - */1, value, isPageDown);
          // cc number, cc value, midi channel
  if (pot == NPots - 1) {
    /* so this is a mess, but it has to be NPots -1 and not BPM_POT
    because this function is being passed i, instead of potPin[i]
    (the array which holds the analog pin directions),
    because potPin[0] = A0, wich cannot be passed to sendControlChange because A0 isn't an int */ 
    MIDI.sendControlChange(127, value, 1);
  } else {
    if (isEffectsOn == LOW) {
      MIDI.sendControlChange(pot + page * (NPots-1), value, isPageDown);
    } else {
      handlePotsWithEffectsOn(pot, value);
    }
  }
}


void handlePotsWithEffectsOn(int pot, int value) {
  int column = pot + page * 4;
  int effect = selectedEffectPerColumn[column];

  if (ARE_EFFECT_KNOBS_TIED_TO_PAGE_NUMBER) {
    // for 4 effect knobs per page
    MIDI.sendControlChange(column*4 + effect, value, 3);
  } else {
    // for 4 effect knobs in general, to assign to all pages
    MIDI.sendControlChange(pot*4 + effect, value, 3);               
  }
}




// selectedEffectPerColumn[MAX_PAGES * 4] = {0};


void pages() {
  if (pageLeft.debounce()) {
    page = max(page - 1, 0);
    isPageDown = 1;
  }
  if (pageRight.debounce()) {
    
    page = min(page + 1, MAX_PAGES - 1);
    isPageDown = 1;
  }
  if (pageDown.debounce()) {
    isPageDown %= 2;
    isPageDown++;
  }
}