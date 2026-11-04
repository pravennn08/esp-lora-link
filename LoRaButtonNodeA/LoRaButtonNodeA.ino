// LoRaButtonNodeA: ESP-WROOM-32 / ESP32 Dev Module, COM6.
// Button: GPIO16 to GND (INPUT_PULLUP). LED: GPIO21 -> resistor -> LED -> GND.
// Keep LoRaButtonLink.h in the same folder as this sketch.
#include <SPI.h>
#include <LoRa.h>
#include <esp_system.h>
#include <esp_random.h>
#include "LoRaButtonLink.h"

using namespace ButtonLink;
constexpr uint8_t NODE_ID = 1;
constexpr uint8_t PEER_ID = (NODE_ID == 1) ? 2 : 1;
constexpr int BUTTON_PIN = 16;
constexpr int LED_PIN = 21;
constexpr int RADIO_SCK = 18;
constexpr int RADIO_MISO = 19;
constexpr int RADIO_MOSI = 23;
constexpr int RADIO_NSS = 5;
constexpr int RADIO_RESET = 4;
constexpr int RADIO_DIO0 = 2;
constexpr long RADIO_FREQUENCY = 433000000L;
constexpr int TX_POWER_DBM = 14;
constexpr uint32_t DEBOUNCE_MS = 30;
constexpr uint32_t HELD_REFRESH_MS = 750;
constexpr uint32_t IDLE_REFRESH_MS = 3000;
constexpr uint32_t ACK_TIMEOUT_MS = 500;
constexpr uint32_t TX_TIMEOUT_MS = 2000;

RemoteState remote;
bool radioReady = false;
bool lastRawPressed = false;
bool localPressed = false;
uint32_t rawChangedAt = 0;
uint32_t nextStateAt = 0;
uint32_t nextStatusAt = 0;
uint32_t bootSession = 0;
uint32_t nextSequence = 1;
bool txBusy = false;
Packet txPacket = {};
uint32_t txStartedAt = 0;
bool ackQueued = false;
Packet queuedAck = {};
uint32_t ackDueAt = 0;
bool awaitingAck = false;
uint32_t waitingSequence = 0;
uint32_t waitingSentAt = 0;
uint32_t ackDeadline = 0;

uint32_t jitter(uint32_t minimum, uint32_t maximum) {
  return minimum + esp_random() % (maximum - minimum + 1);
}

uint8_t readRadioRegister(uint8_t address) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(RADIO_NSS, LOW);
  SPI.transfer(address & 0x7F);
  uint8_t value = SPI.transfer(0);
  digitalWrite(RADIO_NSS, HIGH);
  SPI.endTransaction();
  return value;
}

void clearRadioFlags() {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(RADIO_NSS, LOW);
  SPI.transfer(0x12 | 0x80);
  SPI.transfer(0xFF);
  digitalWrite(RADIO_NSS, HIGH);
  SPI.endTransaction();
}

void serviceButton(uint32_t now) {
  bool rawPressed = digitalRead(BUTTON_PIN) == LOW;
  if (rawPressed != lastRawPressed) {
    lastRawPressed = rawPressed;
    rawChangedAt = now;
  }
  if (rawPressed != localPressed && uint32_t(now - rawChangedAt) >= DEBOUNCE_MS) {
    localPressed = rawPressed;
    nextStateAt = now + jitter(5, 30);
    Serial.printf("[BUTTON] %s -> peer LED %s\n",
                  localPressed ? "PRESSED" : "RELEASED", localPressed ? "ON" : "OFF");
  }
}

bool startTransmission(const Packet &packet, uint32_t now) {
  uint8_t wire[WIRE_SIZE];
  encode(packet, wire);
  LoRa.idle();
  if (!LoRa.beginPacket()) {
    Serial.println("[RADIO] Could not begin packet.");
    LoRa.receive();
    return false;
  }
  LoRa.write(wire, sizeof(wire));
  LoRa.endPacket(true);  // Asynchronous: check TX_DONE in later loop iterations.
  txBusy = true;
  txPacket = packet;
  txStartedAt = now;
  if (packet.kind == Kind::State) {
    Serial.printf("[STATE TX] to=%u seq=%lu button=%s\n", unsigned(PEER_ID),
                  static_cast<unsigned long>(packet.sequence), packet.pressed ? "DOWN" : "UP");
  }
  return true;
}

void serviceTransmission(uint32_t now) {
  if (!txBusy) return;
  if (readRadioRegister(0x12) & 0x08) {
    clearRadioFlags();
    txBusy = false;
    if (txPacket.kind == Kind::State) {
      // Only the latest state needs confirmation; new button states supersede old ones.
      awaitingAck = true;
      waitingSequence = txPacket.sequence;
      waitingSentAt = txStartedAt;
      ackDeadline = now + ACK_TIMEOUT_MS;
    }
    LoRa.receive();
  } else if (uint32_t(now - txStartedAt) >= TX_TIMEOUT_MS) {
    LoRa.idle();
    clearRadioFlags();
    txBusy = false;
    LoRa.receive();
    nextStateAt = now + jitter(150, 350);
    Serial.println("[RADIO FAIL] TX_DONE timeout; scheduling another state update.");
  }
}

void serviceReceive(uint32_t now) {
  if (txBusy) return;
  int length = LoRa.parsePacket();
  if (length <= 0) return;
  uint8_t wire[WIRE_SIZE];
  size_t copied = 0;
  while (LoRa.available()) {
    uint8_t value = uint8_t(LoRa.read());
    if (copied < WIRE_SIZE) wire[copied++] = value;
  }
  int rssi = LoRa.packetRssi();
  float snr = LoRa.packetSnr();
  Packet packet;
  if (copied != size_t(length) || !decode(wire, size_t(length), packet) ||
      packet.to != NODE_ID || packet.from != PEER_ID) {
    LoRa.receive();
    return;
  }

  if (packet.kind == Kind::State) {
    Result result = remote.accept(packet, now);
    if (result == Result::Updated) {
      digitalWrite(LED_PIN, remote.pressed ? HIGH : LOW);
      Serial.printf("[STATE RX] from=%u seq=%lu localLED=%s RSSI=%d dBm SNR=%.2f dB\n",
                    unsigned(packet.from), static_cast<unsigned long>(packet.sequence),
                    remote.pressed ? "ON" : "OFF", rssi, snr);
    }
    if (result != Result::Rejected) {
      // An ACK reports the state actually applied to this board's LED.
      queuedAck = {Kind::Ack, NODE_ID, PEER_ID, remote.pressed, packet.session, packet.sequence};
      ackQueued = true;
      ackDueAt = now + jitter(10, 35);
    }
  } else if (packet.session == bootSession && awaitingAck && packet.sequence == waitingSequence) {
    awaitingAck = false;
    Serial.printf("[ACK] seq=%lu peerLED=%s RTT=%lu ms RSSI=%d dBm SNR=%.2f dB\n",
                  static_cast<unsigned long>(packet.sequence), packet.pressed ? "ON" : "OFF",
                  static_cast<unsigned long>(now - waitingSentAt), rssi, snr);
  }
  LoRa.receive();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  lastRawPressed = digitalRead(BUTTON_PIN) == LOW;
  rawChangedAt = millis();
  bootSession = esp_random() | 1U;
  Serial.printf("\n[BOOT] Node %u -> peer %u; reset_reason=%d\n",
                unsigned(NODE_ID), unsigned(PEER_ID), int(esp_reset_reason()));

  pinMode(RADIO_NSS, OUTPUT);
  digitalWrite(RADIO_NSS, HIGH);
  SPI.begin(RADIO_SCK, RADIO_MISO, RADIO_MOSI, RADIO_NSS);
  LoRa.setPins(RADIO_NSS, RADIO_RESET, RADIO_DIO0);
  LoRa.setSPIFrequency(1000000);
  radioReady = LoRa.begin(RADIO_FREQUENCY);
  if (!radioReady) {
    Serial.printf("[INIT FAIL] RegVersion=0x%02X; expected 0x12. Check radio power and SPI.\n",
                  unsigned(readRadioRegister(0x42)));
    nextStatusAt = millis() + 3000;
    return;
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125000);
  LoRa.setCodingRate4(5);
  LoRa.setPreambleLength(8);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.setTxPower(TX_POWER_DBM);
  clearRadioFlags();
  LoRa.receive();
  nextStateAt = millis() + NODE_ID * 100U + jitter(0, 90);
  nextStatusAt = millis() + 5000;
  Serial.printf("[READY] 433 MHz SF7 BW125k CRC=on TX=%d dBm button=16 LED=21\n", TX_POWER_DBM);
  Serial.println("[USE] Hold your button to light the OTHER board's LED; release to turn it off.");
}

void loop() {
  uint32_t now = millis();
  serviceButton(now);
  if (!radioReady) {
    if (due(now, nextStatusAt)) {
      nextStatusAt = now + 3000;
      Serial.println("[INIT FAIL] Check wiring, then press EN/RESET.");
    }
    return;
  }
  serviceTransmission(now);
  serviceReceive(now);
  if (remote.expire(now)) {
    digitalWrite(LED_PIN, LOW);
    Serial.println("[LINK TIMEOUT] No fresh peer state for 2500 ms; local LED forced OFF.");
  }
  if (awaitingAck && due(now, ackDeadline)) {
    awaitingAck = false;
    Serial.printf("[NO ACK] seq=%lu; delivery/return path not confirmed.\n",
                  static_cast<unsigned long>(waitingSequence));
  }
  if (!txBusy) {
    if (ackQueued) {
      if (due(now, ackDueAt)) {
        ackQueued = false;
        startTransmission(queuedAck, now);
      }
    } else if (due(now, nextStateAt)) {
      Packet state = {Kind::State, NODE_ID, PEER_ID, localPressed, bootSession, nextSequence++};
      if (startTransmission(state, now)) {
        nextStateAt = now + (localPressed ? HELD_REFRESH_MS : IDLE_REFRESH_MS) + jitter(0, 200);
      } else {
        nextStateAt = now + jitter(150, 350);
      }
    }
  }
  if (due(now, nextStatusAt)) {
    nextStatusAt = now + 5000;
    Serial.printf("[STATUS] node=%u button=%s localLED=%s uptime=%lu s\n", unsigned(NODE_ID),
                  localPressed ? "DOWN" : "UP", remote.pressed ? "ON" : "OFF",
                  static_cast<unsigned long>(now / 1000));
  }
}

