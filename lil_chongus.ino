#include <MIDI.h>  // by Francois Best
//#include <Debounce.h>
MIDI_CREATE_DEFAULT_INSTANCE();

class Button {
private:
  uint8_t btn;
  uint16_t globalState;
public:
  void begin(uint8_t button) {
    btn = button;
    globalState = 0;
    pinMode(btn, INPUT_PULLUP);
  }
  bool debounce() {
    globalState = (globalState << 1) | digitalRead(btn) | 0xfe00;
    return (globalState == 0xff00);
  }
};

//! right now, isEffectsOn and areDrumsOn depend on having all interruptors up at the start of the program
//? which, obviously, makes it incosnsistent
//? should be able to eliminate the variables and use only the globalState of the pins, but I'm to lazy to do it now


/*
  MIDI CC
  127 = Tempo (continuo)
  126 = BPM monitor on/off (Digital)
    Capaz hay algo con bpm en midi

  red button: channel 16, note 0

  channels 1 y 2: modo loops
    pots van por 1
  channels 3 y 4: modo efectos
    pots van por 3
  channel 5: drum notes

  channel 6 y 7: group notes

  pines:
    botones principales: 2 - 17
    pageLeft: 41
    pageDown: 49
    pageRight: 37
    effects: 35
    bpmmonitor: 33
    drums: 31
    red one: 59
    pdealcito: A6

    led: 29

*/

enum GlobalState {
  ClipLaunch,
  ClipEffects,
  Drums,
  Groups,
};

GlobalState globalState;

//! consts to change behaivour
const bool DO_EFFECTS_ON_BUTTONS_SEND_NOTES = false;
const bool ARE_EFFECT_KNOBS_TIED_TO_PAGE_NUMBER = false;
const bool ARE_GROUP_KNOBS_TIED_TO_PAGE_NUMBER = false;
const bool DO_GROUPS_ON_BUTTONS_SEND_NOTES = true;


// BUTTONS

const int EFFECTS_INTERRUPTOR = 35;
const int BPM_INTERRUPTOR = 33;
const int DRUMS_INTERRUPTOR = 31;
const int RED_BUTTON = 59;
const int PEDAL = A6;

const int NButtons = 16 + 3 + 1 + 1;
const int buttonPin[NButtons] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, EFFECTS_INTERRUPTOR, BPM_INTERRUPTOR, DRUMS_INTERRUPTOR, RED_BUTTON, PEDAL};

int buttonCurrentState[NButtons] = {};  // stores the button current value
int buttonPreviousState[NButtons] = {};  // stores the button previous value

int redButtonNote = 0;
int redButtonChannel = 1;
bool isRedButtonActive = false;

// debounce
unsigned long lastDebounceTime[NButtons] = { 0 };  // the last time the output pin was toggled
unsigned long debounceDelay = 5;                  //* the debounce time; increase if the output flickers


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
bool areDrumsOn = false;

int selectedGroupPerColumn[MAX_PAGES * 4] = {0};


// KNOBS
const int BPM_POT = A4;

const int NPots = 5;                               //*** total numbers of pots (slide & rotary)
const int potPin[NPots] = {A0, A1, A2, A3, BPM_POT};  //*** Analog pins of each pot connected straight to the Arduino i.e 4 pots, {A3, A2, A1, A0};
                                                   // have nothing in the array if 0 pots {}

int potCurrentState[NPots] = { 0 };  // Current globalState of the pot; delete 0 if 0 pots
int potPglobalState[NPots] = { 0 };  // Previous globalState of the pot; delete 0 if 0 pots
int potVar = 0;                // Difference between the current and previous globalState of the pot

int midiCurrentState[NPots] = { 0 }; // Current globalState of the midi value; delete 0 if 0 pots
int midiPreviousSatate[NPots] = { 0 }; // Previous globalState of the midi value; delete 0 if 0 pots

const int TIMEOUT = 300;             //** Amount of time the potentiometer will be read after it exceeds the varThreshold
const int varThreshold = 10;         //** Threshold for the potentiometer signal variation
boolean potMoving = true;            // If the potentiometer is moving
unsigned long PTime[NPots] = { 0 };  // Previously stored time; delete 0 if 0 pots
unsigned long timer[NPots] = { 0 };  // Stores the time that has elapsed since the timer was reset; delete 0 if 0 pots



// LED
const int LED = 29;
long ledDelay = 0;
const int SHORT_LED_DURATION = 50;
const int LONG_LED_DURATION = 150;

void setup() {
  Serial.begin(115200);  //**  Baud Rate 31250 for MIDI class compliant jack | 115200 for Hairless MIDI
  for (int i = 0; i < NButtons; i++) {
    pinMode(buttonPin[i], INPUT_PULLUP);
  }
  pageLeft.begin(41);
  pageDown.begin(39);
  pageRight.begin(37);

  longLed();
}

void loop() {
  updateglobalState();
  debouncePots();
  debounceButtons();
  pages();

  if (ledDelay < millis()) {
    // turns down the led
    digitalWrite(LED, LOW);
  }
}

// EFFECTS_INTERRUPTOR, DRUMS_INTERRUPTOR
void updateglobalState() {
  if (digitalRead(EFFECTS_INTERRUPTOR)) {
    if (digitalRead(DRUMS_INTERRUPTOR)) {

      globalState = Groups;
    } else {
      globalState = ClipEffects;
    }
  } else {
    if (digitalRead(DRUMS_INTERRUPTOR)) {
      globalState = Drums;
    } else {
      globalState = ClipLaunch;
    }
  }
}


void debounceButtons() {
  //Serial.println("debounce buttons called");
  for (int i = 0; i < NButtons; i++) {
    buttonCurrentState[i] = digitalRead(buttonPin[i]);  // read pins from arduino
    if ((millis() - lastDebounceTime[i]) > debounceDelay) {
      if (buttonPreviousState[i] != buttonCurrentState[i]) {
        lastDebounceTime[i] = millis();

        handleButtons(buttonPin[i], buttonCurrentState[i]);
        buttonPreviousState[i] = buttonCurrentState[i];
      }
    }
  }
}


void handleButtons(int pin, uint8_t value) {
  switch (pin) {
    case EFFECTS_INTERRUPTOR:
      isEffectsOn = !isEffectsOn;
    break;
    case RED_BUTTON:
      handleRedButton(value);
    break;
    case PEDAL:
      handlePedal(value);
    break;
    case DRUMS_INTERRUPTOR:
      areDrumsOn = !areDrumsOn;
      if (value == LOW) {
        MIDI.sendControlChange(125, 127, 1);
      } else {
        MIDI.sendControlChange(125, 0, 1);
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
    
      switch (globalState) {
        case Drums:
          handleMainButtonsWithDrumsOn(pin, value);
          break;
        case ClipEffects:
          handleMainButtonsWithEffectsOn(pin, value);
          break;
        case ClipLaunch:
          handleMainButtonsWithLaunchOn(pin, value);
          break;
        case Groups:
          handleMainButtonsWithGroupsOn(pin, value);
          break;
      }
  }
}




void handleRedButton(uint8_t value) {
  if (isRedButtonActive == false) {

    if (value == LOW) {
      MIDI.sendNoteOn(0, 127, 16);
    } else {
      MIDI.sendNoteOn(0, 0, 16);
    }
  } else {
    if (value == LOW) {
      MIDI.sendNoteOn(redButtonNote, 127, redButtonChannel);
    } else {
      MIDI.sendNoteOn(redButtonNote, 0, redButtonChannel);
      isRedButtonActive = false;
    }
  }
}

void handlePedal(uint8_t value) {
  if (areDrumsOn){
    if (value == LOW) {
      MIDI.sendNoteOn(1, 127, 16);
    } else {
      MIDI.sendNoteOn(1, 0, 16);
    }
  } else {
    if (value == LOW) {
      MIDI.sendNoteOn(redButtonNote, 127, redButtonChannel);
    } else {
      MIDI.sendNoteOn(redButtonNote, 0, redButtonChannel);
    }

  }
}


void handleMainButtonsWithLaunchOn(int pin, uint8_t value) {
  // some random stuff so that ableton's default keybindings for drums work
  if (value == LOW) {
    MIDI.sendNoteOn(pin - 2 + page * 16, 127, isPageDown);
    redButtonChannel = isPageDown;
    isRedButtonActive = true;
    redButtonNote = pin - 2 + page * 16;

    // note, velocity, channel
    shortLed();
  } else {
    MIDI.sendNoteOn(pin - 2 + page * 16, 0, isPageDown);
  }
}

void handleMainButtonsWithDrumsOn(int pin, uint8_t value) {
  if (value == LOW) {
    MIDI.sendNoteOn(pin - 2 + 36, 127, 5);
    // note, velocity, channel
    shortLed();
  } else {
    MIDI.sendNoteOn(pin - 2 + 36, 0, 5);
  }
}


void handleMainButtonsWithEffectsOn(int pin, uint8_t value) {
  shortLed();
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

void handleMainButtonsWithGroupsOn(int pin, uint8_t value) {
  int column;
  shortLed();
  pin = pin - 2;
  if (ARE_GROUP_KNOBS_TIED_TO_PAGE_NUMBER) {
    column = pin % 4 + page * 4;
  } else {
    column = pin % 4 /*+ page * 4*/;
  }
  int n = (pin - pin % 4) / 4;
  selectedGroupPerColumn[column] = n;

  if (DO_GROUPS_ON_BUTTONS_SEND_NOTES) {
  // when effect is on, buttons still send notes (in another channel)
    if (value == LOW) {
      MIDI.sendNoteOn(column * 4 + n, 127, isPageDown+5);
    } else {
      MIDI.sendNoteOn(column * 4 + n, 0, isPageDown+5);
    }
  } // else, it doesn't do nothing
}





void debouncePots() {
  for (int i = 0; i < NPots; i++) {                      // Loops through all the potentiometers
    potCurrentState[i] = analogRead(potPin[i]);                // reads the pins from arduino
    midiCurrentState[i] = map(potCurrentState[i], 0, 1023, 0, 127);  // Maps the reading of the potCurrentState to a value usable in midi
    potVar = abs(potCurrentState[i] - potPglobalState[i]);           // Calculates the absolute value between the difference between the current and previous state of the pot
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
      if (midiPreviousSatate[i] != midiCurrentState[i]) {
        
        handlePots(i, 127 - midiCurrentState[i]); // sends to handler

        potPglobalState[i] = potCurrentState[i];        // Stores the current reading of the potentiometer to compare with the next
        midiPreviousSatate[i] = midiCurrentState[i];
      }
    }
  }
}


void handlePots(int pot, int value) {
  shortLed();
  if (pot == NPots - 1) {
    /* so this is a mess, but it has to be NPots -1 and not BPM_POT
    because this function is being passed i, instead of potPin[i]
    (the array which holds the analog pin directions),
    because potPin[0] = A0, wich cannot be passed to sendControlChange because A0 isn't an int */ 
    MIDI.sendControlChange(127, value, 1);
  } else {

    switch (globalState) {
        case Drums:
          MIDI.sendControlChange(pot, value, 5);
          break;
        case ClipEffects:
          handlePotsWithEffectsOn(pot, value);
          break;
        case ClipLaunch:
          MIDI.sendControlChange(pot + page * (NPots-1), value, isPageDown);
          break;
        case Groups:
          handlePotsWithGroupsOn(pot, value);
          break;
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

void handlePotsWithGroupsOn(int pot, int value) {
  int column = pot + page * 4;
  int group;


  if (ARE_GROUP_KNOBS_TIED_TO_PAGE_NUMBER) {
    group = selectedGroupPerColumn[column];
    // for 4 effect knobs per page
    MIDI.sendControlChange(column*4 + group, value, 5 + isPageDown);
  } else {
    group = selectedGroupPerColumn[pot];
    // for 4 effect knobs in general, to assign to all pages
    MIDI.sendControlChange(pot*4 + group, value, 5 + isPageDown);
  }
}


void pages() {
  if (pageLeft.debounce()) {
    if (page != 0){
      page--;
      isPageDown = 1;
      if (page == 0){
        longLed();
      } else {
        shortLed();
      }
    }
  }

  if (pageRight.debounce()) {
    
     if (page != 15){
      page++;
      isPageDown = 1;
      if (page == 15){
        longLed();
      } else {
        shortLed();
      }
    }
  }
  if (pageDown.debounce()) {
    isPageDown %= 2;
    isPageDown++;
    shortLed();
  }
}

void shortLed() {
  ledDelay = millis() + SHORT_LED_DURATION;
  digitalWrite(LED, HIGH);
  // logic for turning the led off is in the loop function
}

void longLed() {
  ledDelay = millis() + LONG_LED_DURATION;
  digitalWrite(LED, HIGH);
  // logic for turning the led off is in the loop function
}