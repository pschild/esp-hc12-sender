#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Ticker.h>
#include <CircularBuffer.h>

const String CHIP_ID = "123456";

int ERROR_LED_PIN = 13;

// Gate 1
int GATE_1_PIN = 9;
int oldState1;
bool stateChanged1;
unsigned long stateChangedMillis1 = 0;

// Gate 2
int GATE_2_PIN = 10;
int oldState2;
bool stateChanged2;
unsigned long stateChangedMillis2 = 0;

namespace data {
  typedef struct {
    unsigned long code;
    char payload[80];
  } record;
}

SoftwareSerial hc12(6, 5);

CircularBuffer<data::record, 10> buffer;

bool pendingSend = false;
unsigned long pendingSendStart;
const int MAX_RETRIES = 20;
const int RETRY_TIMEOUT = 30000;
int retryCount = 0;

void handleStateChange1();
void handleStateChange2();
void enqueueStates();
void enqueueMessage(String switchId, int value, int pin, bool triggeredByChange);
void sendMessage(unsigned long code, char payload[80]);

Ticker enqueueStatesTimer(enqueueStates, 15 * 60 * 1000L);

void setup() {
  Serial.begin(9600);
  hc12.begin(9600);

  randomSeed(analogRead(0));

  pinMode(GATE_1_PIN, INPUT);
  pinMode(GATE_2_PIN, INPUT);
  pinMode(13, OUTPUT);

  enqueueStatesTimer.start();

  delay(1000);
}

void loop() {
  enqueueStatesTimer.update();

  handleStateChange1();
  handleStateChange2();

  if (!buffer.isEmpty()) {
    data::record firstItem = buffer.first();

    if (!pendingSend) {
      // Serial.println("sending item " + String(firstItem.code) + " " + firstItem.payload);
      sendMessage(firstItem.code, firstItem.payload);
      pendingSend = true;
      pendingSendStart = millis();
    }

    if (pendingSend && millis() - pendingSendStart >= RETRY_TIMEOUT) {
      if (retryCount <= MAX_RETRIES) {
        Serial.println("retrying item " + String(firstItem.code));
        retryCount++;
        sendMessage(firstItem.code, firstItem.payload);
        pendingSendStart = millis();
      } else {
        // Error-Handling: rote LED blinken lassen
        digitalWrite(ERROR_LED_PIN, HIGH);
        // Serial.println("max retries reached");
      }
    }
  }

  if (hc12.available()) {
    String message = hc12.readString();
    int delimeterIndex = message.indexOf('-');
    unsigned long receivedCode = message.substring(delimeterIndex + 1).toInt();
    // Serial.println("ack, code=" + String(receivedCode));
    if (!buffer.isEmpty()) {
      data::record firstItem1 = buffer.shift();
      if (firstItem1.code == receivedCode) {
        // Serial.println("ack for " + String(firstItem1->code()) + " => removing");
        pendingSend = false;
        retryCount = 0;
      }
    }
  }
}

void handleStateChange1() {
  int newState = digitalRead(GATE_1_PIN);
  unsigned long currentMillis = millis();
  bool statesAreEqual = newState == oldState1;

  if (!stateChanged1 && !statesAreEqual) {
    stateChanged1 = true;
    stateChangedMillis1 = currentMillis;
  }

  if (stateChanged1) {
    bool stateChangedLongEnough = currentMillis - stateChangedMillis1 >= 5000;

    if (stateChangedLongEnough) {
      enqueueMessage("gate1", newState, GATE_1_PIN, true);
    }

    if (stateChangedLongEnough || statesAreEqual) {
      stateChanged1 = false;
      stateChangedMillis1 = 0;
      oldState1 = newState;
    }
  }
}

void handleStateChange2() {
  int newState = digitalRead(GATE_2_PIN);
  unsigned long currentMillis = millis();
  bool statesAreEqual = newState == oldState2;

  if (!stateChanged2 && !statesAreEqual) {
    stateChanged2 = true;
    stateChangedMillis2 = currentMillis;
  }

  if (stateChanged2) {
    bool stateChangedLongEnough = currentMillis - stateChangedMillis2 >= 5000;

    if (stateChangedLongEnough) {
      enqueueMessage("gate2", newState, GATE_2_PIN, true);
    }

    if (stateChangedLongEnough || statesAreEqual) {
      stateChanged2 = false;
      stateChangedMillis2 = 0;
      oldState2 = newState;
    }
  }
}

void enqueueStates() {
  enqueueMessage("gate1", digitalRead(GATE_1_PIN), GATE_1_PIN, false);
  enqueueMessage("gate2", digitalRead(GATE_2_PIN), GATE_2_PIN, false);
}

void enqueueMessage(String switchId, int value, int pin, bool triggeredByChange) {
  unsigned long code = random(1, 10000);

  String payload = "chipId=" + CHIP_ID + ",topic=switch,switchId=" + switchId + ",value=" + value + ",pin=" + pin + ",triggeredByChange=" + triggeredByChange;
  char payloadAsCharArr[payload.length() + 1];
  payload.toCharArray(payloadAsCharArr, payload.length() + 1);
  data::record record = {code, ""};
  strcpy(record.payload, payloadAsCharArr);

  if (!buffer.isFull()) {
    buffer.push(record);
  }
}

void sendMessage(unsigned long code, char payload[80]) {
  // Serial.println("code=" + String(code) + "," + payload);
  
  /**
   * message format:
   * - Starts with <, ends with >
   * - contains mandatory keys `code`, `chipId`, `topic`
   * - key-value-pairs (key=value) are separated by comma
   * - may contain optional key-value-pairs (key=value)
   *
   * Example:
   * <code=...,chipId=...,topic=...[,key=value...]>
   */
  hc12.print("<code=" + String(code) + "," + payload + ">");
}