#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include "i2s_mck.h"
#include "TA_SA100WR_I2C_SNIFING_seq_from_pulseview.h"

// ============================================================
// USER PINS
// ============================================================
static const int I2C_SDA = 21;
static const int I2C_SCL = 22;
static const uint32_t I2C_HZ = 100000;

static const int PIN_RESN        = 4;   // ESP32 -> EZW-RT10A reset (active low)
static const int GPIO1_ASIC_LINK = 34;  // EZW-RT10A -> ESP32 LINK
static const int GPIO2_ASIC_INT  = 35;  // EZW-RT10A -> ESP32 INT

// ============================================================
// TIMING
// ============================================================
static const uint32_t POLL_PERIOD_MS    = 20;
static const uint8_t  BURST_POLLS       = 3;
static const uint16_t BURST_DELAY_MS    = 2;
static const uint32_t KEEPALIVE_MS      = 250;
static const uint32_t REPAIR_INTERVALMS = 3000;

// ============================================================
// GLOBAL STATE
// ============================================================
struct PollDef {
  uint8_t ptr;
  uint8_t len;
  const char* name;
};

static const PollDef pollList[] = {
  { 0x13, 2,  "reg13" },
  { 0x41, 2,  "reg41" },
  { 0x50, 17, "reg50" },
  { 0x74, 2,  "reg74" },
  { 0x76, 2,  "reg76" },
};

struct PollCache {
  bool valid;
  uint8_t data[17];
};

static PollCache lastPoll[sizeof(pollList) / sizeof(pollList[0])];

static bool linkedOnce = false;
static bool autoKeepAlive = true;
static uint32_t lastKeepAliveMs = 0;
static uint32_t lastRepairMs = 0;

// ============================================================
// HELPERS
// ============================================================
static void printHex2(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

static void dumpBytes(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    printHex2(p[i]);
    if (i + 1 < n) Serial.print(' ');
  }
}

static void printStamp() {
  Serial.print("[");
  Serial.print(millis());
  Serial.print(" ms] ");
}

static void print_link_status() {
  Serial.print("LINK=");
  Serial.println(digitalRead(GPIO1_ASIC_LINK) ? "HIGH" : "LOW");
}

static void print_int_status() {
  Serial.print("INT=");
  Serial.println(digitalRead(GPIO2_ASIC_INT) ? "HIGH" : "LOW");
}

static void ezw_reset_pulse() {
  digitalWrite(PIN_RESN, LOW);
  delay(30);
  digitalWrite(PIN_RESN, HIGH);
  delay(200);
}

// ============================================================
// I2C
// ============================================================
static bool i2cWrite(uint8_t addr7, const uint8_t* data, size_t len) {
  Wire.beginTransmission(addr7);
  Wire.write(data, len);
  uint8_t rc = Wire.endTransmission(true);

  if (rc != 0) {
    printStamp();
    Serial.print("I2C WRITE FAIL addr=0x");
    Serial.print(addr7, HEX);
    Serial.print(" rc=");
    Serial.println(rc);
    return false;
  }
  return true;
}

static bool i2cRead(uint8_t addr7, uint8_t* out, size_t len) {
  size_t got = Wire.requestFrom((int)addr7, (int)len, (int)true);
  for (size_t i = 0; i < got; i++) out[i] = Wire.read();

  if (got != len) {
    printStamp();
    Serial.print("I2C READ SHORT addr=0x");
    Serial.print(addr7, HEX);
    Serial.print(" got=");
    Serial.print(got);
    Serial.print(" expected=");
    Serial.println(len);
    return false;
  }
  return true;
}

static bool ptrRead40(uint8_t ptr, uint8_t* out, size_t len) {
  if (!i2cWrite(ADDR_40, &ptr, 1)) return false;
  return i2cRead(ADDR_40, out, len);
}

// ============================================================
// POLL / DIFF LOGGING
// ============================================================
static void printChangedBlock(const char* name, uint8_t ptr, const uint8_t* data, size_t len) {
  printStamp();
  Serial.print(name);
  Serial.print(" ptr=0x");
  printHex2(ptr);
  Serial.print(" -> ");
  dumpBytes(data, len);
  Serial.println();
}

static void printDiff50(const uint8_t* oldb, const uint8_t* newb, size_t len) {
  printStamp();
  Serial.print("reg50 diff: ");
  bool any = false;

  for (size_t i = 0; i < len; i++) {
    if (oldb[i] != newb[i]) {
      any = true;
      Serial.print("[");
      Serial.print(i);
      Serial.print("]=");
      printHex2(oldb[i]);
      Serial.print("->");
      printHex2(newb[i]);
      Serial.print(" ");
    }
  }

  if (!any) Serial.print("no change");
  Serial.println();
}

static void poll_once(bool printAll) {
  uint8_t buf[17];

  for (size_t i = 0; i < sizeof(pollList) / sizeof(pollList[0]); i++) {
    const PollDef& p = pollList[i];

    if (!ptrRead40(p.ptr, buf, p.len)) {
      printStamp();
      Serial.print("poll fail ptr=0x");
      printHex2(p.ptr);
      Serial.println();
      continue;
    }

    bool changed = !lastPoll[i].valid || (memcmp(lastPoll[i].data, buf, p.len) != 0);

    if (printAll || changed) {
      if (p.ptr == 0x50 && lastPoll[i].valid && !printAll) {
        printDiff50(lastPoll[i].data, buf, p.len);
      } else {
        printChangedBlock(p.name, p.ptr, buf, p.len);
      }
    }

    memcpy(lastPoll[i].data, buf, p.len);
    lastPoll[i].valid = true;
  }
}

static void poll_burst(uint8_t n, uint16_t dlyMs) {
  for (uint8_t i = 0; i < n; i++) {
    poll_once(false);
    delay(dlyMs);
  }
}

// ============================================================
// SMART SNIFF EXECUTION
// ============================================================

// Your sniff file has many single-byte pointer writes such as
// 0x13 / 0x41 / 0x50 / 0x74 / 0x76, and also many 17-byte blocks
// beginning with 0x50. Those 17-byte blocks look like captured readback
// payloads from the sniff, not something we should blindly write back.
// So:
//   - single-byte known ptr -> perform read
//   - 17-byte 0x50... block -> skip
//   - everything else -> normal write

static bool isPollPtr40(uint8_t ptr) {
  return (ptr == 0x13 || ptr == 0x41 || ptr == 0x50 || ptr == 0x74 || ptr == 0x76);
}

static size_t pollLen40(uint8_t ptr) {
  if (ptr == 0x50) return 17;
  return 2;
}

static bool doPtrRead40(uint8_t ptr, bool verbose = true) {
  uint8_t buf[17] = {0};

  if (!i2cWrite(ADDR_40, &ptr, 1)) {
    if (verbose) {
      printStamp();
      Serial.print("PTR WRITE FAIL 0x");
      printHex2(ptr);
      Serial.println();
    }
    return false;
  }

  size_t len = pollLen40(ptr);
  if (!i2cRead(ADDR_40, buf, len)) {
    if (verbose) {
      printStamp();
      Serial.print("PTR READ FAIL 0x");
      printHex2(ptr);
      Serial.println();
    }
    return false;
  }

  if (verbose) {
    printStamp();
    Serial.print("READ 0x40 ptr=0x");
    printHex2(ptr);
    Serial.print(" -> ");
    dumpBytes(buf, len);
    Serial.println();
  }

  return true;
}

static bool looksLikeCapturedReadback(const Cmd& c) {
  if (c.addr != ADDR_40) return false;
  if (c.len == 17 && c.data[0] == 0x50) return true;
  return false;
}

static void printFrame(const Cmd& c, size_t index) {
  printStamp();
  Serial.print("#");
  Serial.print(index);
  Serial.print(" W 0x");
  printHex2(c.addr);
  Serial.print(": ");
  dumpBytes(c.data, c.len);
  Serial.println();
}

static bool execute_sniff_cmd(const Cmd& c, size_t index, bool verbose = true) {
  if (verbose) {
    printFrame(c, index);
  }

  if (looksLikeCapturedReadback(c)) {
    if (verbose) {
      printStamp();
      Serial.println("skip captured 0x50 status/readback block");
    }
    return true;
  }

  if (c.addr == ADDR_40 && c.len == 1 && isPollPtr40(c.data[0])) {
    return doPtrRead40(c.data[0], verbose);
  }

  if (!i2cWrite(c.addr, c.data, c.len)) {
    printStamp();
    Serial.print("EXEC FAIL at index ");
    Serial.println(index);
    return false;
  }

  delay(2);
  return true;
}

static bool run_full_sequence_from_file() {
  printStamp();
  Serial.println("---- FULL SEQ START ----");

  for (size_t i = 0; i < seq_from_pulseview_count; i++) {
    if (!execute_sniff_cmd(seq_from_pulseview[i], i, true)) {
      printStamp();
      Serial.print("FULL SEQ FAILED at index ");
      Serial.println(i);
      return false;
    }
  }

  printStamp();
  Serial.println("---- FULL SEQ DONE ----");
  return true;
}

// ============================================================
// SMART PAIRING
// ============================================================

// Based on your newer sniff:
//   10 85
//   74
//   13
//   46 00
//   50 / 41 / 13 repeated
// and later 76 appears too.
//
// So we do a smart wake phase using the same rhythm.

static bool send_10_85_wake() {
  const uint8_t d1[] = {0x10, 0x85};
  const uint8_t d2[] = {0x46, 0x00};

  printStamp();
  Serial.println("WAKE: send 10 85");
  if (!i2cWrite(ADDR_40, d1, sizeof(d1))) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: poll 74");
  if (!doPtrRead40(0x74, true)) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: poll 13");
  if (!doPtrRead40(0x13, true)) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: send 46 00");
  if (!i2cWrite(ADDR_40, d2, sizeof(d2))) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: poll 50");
  if (!doPtrRead40(0x50, true)) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: poll 41");
  if (!doPtrRead40(0x41, true)) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: poll 13");
  if (!doPtrRead40(0x13, true)) return false;
  delay(2);

  printStamp();
  Serial.println("WAKE: poll 76");
  doPtrRead40(0x76, true); // optional, do not fail hard

  return true;
}

static bool smart_pair_try() {
  printStamp();
  Serial.println("==== SMART PAIR TRY START ====");

  // replay the whole sequence, but intelligently:
  // reads for pointer writes, skip captured 0x50 blocks
  if (!run_full_sequence_from_file()) {
    printStamp();
    Serial.println("smart pair: full sequence failed");
    return false;
  }

  delay(20);

  // reinforce the pairing/keepalive flow
  if (!send_10_85_wake()) {
    printStamp();
    Serial.println("smart pair: wake phase failed");
    return false;
  }

  delay(20);

  // poll snapshot
  poll_once(true);

  bool linkHigh = digitalRead(GPIO1_ASIC_LINK);
  printStamp();
  Serial.print("==== SMART PAIR TRY END, LINK=");
  Serial.println(linkHigh ? "HIGH" : "LOW");

  return linkHigh;
}

static bool smart_pair_until_link(uint8_t tries) {
  for (uint8_t i = 0; i < tries; i++) {
    printStamp();
    Serial.print("PAIR ATTEMPT ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.println(tries);

    if (smart_pair_try()) {
      linkedOnce = true;
      return true;
    }

    delay(150);
  }

  return false;
}

// ============================================================
// SERIAL COMMANDS
// ============================================================
static void print_help() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  pair      -> run smart pairing");
  Serial.println("  seq       -> run full sniff sequence");
  Serial.println("  wake      -> send smart wake / keepalive");
  Serial.println("  poll      -> one full poll snapshot");
  Serial.println("  burst     -> burst poll");
  Serial.println("  reset     -> pulse RESN");
  Serial.println("  keep on   -> enable auto keepalive");
  Serial.println("  keep off  -> disable auto keepalive");
  Serial.println("  help      -> show help");
  Serial.println();
}

static void handle_serial_command(String cmd) {
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "pair") {
    bool ok = smart_pair_until_link(3);
    printStamp();
    Serial.println(ok ? "PAIR OK" : "PAIR FAIL");
  }
  else if (cmd == "seq") {
    bool ok = run_full_sequence_from_file();
    printStamp();
    Serial.println(ok ? "SEQ OK" : "SEQ FAIL");
  }
  else if (cmd == "wake") {
    bool ok = send_10_85_wake();
    printStamp();
    Serial.println(ok ? "WAKE OK" : "WAKE FAIL");
  }
  else if (cmd == "poll") {
    poll_once(true);
  }
  else if (cmd == "burst") {
    poll_burst(BURST_POLLS, BURST_DELAY_MS);
  }
  else if (cmd == "reset") {
    ezw_reset_pulse();
    printStamp();
    Serial.println("RESET DONE");
  }
  else if (cmd == "keep on") {
    autoKeepAlive = true;
    printStamp();
    Serial.println("AUTO KEEPALIVE ON");
  }
  else if (cmd == "keep off") {
    autoKeepAlive = false;
    printStamp();
    Serial.println("AUTO KEEPALIVE OFF");
  }
  else if (cmd == "help" || cmd.length() == 0) {
    print_help();
  }
  else {
    printStamp();
    Serial.print("UNKNOWN CMD: ");
    Serial.println(cmd);
    print_help();
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  i2s_start_mclk_12288k();
  delay(100);

  pinMode(PIN_RESN, OUTPUT);
  digitalWrite(PIN_RESN, HIGH);

  pinMode(GPIO1_ASIC_LINK, INPUT);
  pinMode(GPIO2_ASIC_INT, INPUT);

  ezw_reset_pulse();
  delay(100);

  Wire.begin(I2C_SDA, I2C_SCL, I2C_HZ);
  Wire.setClock(I2C_HZ);

  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP32 Sony S-AIR smart pairing start");
  Serial.println("======================================");

  Serial.println("Ping 0x40 / 0x41");
  Wire.beginTransmission(ADDR_40);
  Serial.print("Ping 0x40 rc="); Serial.println(Wire.endTransmission());
  Wire.beginTransmission(ADDR_41);
  Serial.print("Ping 0x41 rc="); Serial.println(Wire.endTransmission());

  print_link_status();
  print_int_status();

  Serial.println("---- FIRST SMART PAIR ----");
  bool ok = smart_pair_until_link(3);
  Serial.println(ok ? "FIRST PAIR OK" : "FIRST PAIR FAIL");

  delay(50);

  print_link_status();
  print_int_status();

  Serial.println("---- FIRST POLL SNAPSHOT ----");
  poll_once(true);

  print_help();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  static uint32_t lastPollMs = 0;
  static int lastLink = -1;
  static int lastInt  = -1;
  static String rxLine;

  const uint32_t now = millis();

  // periodic polling
  if (now - lastPollMs >= POLL_PERIOD_MS) {
    lastPollMs = now;
    poll_once(false);
  }

  // monitor LINK / INT
  int linkNow = digitalRead(GPIO1_ASIC_LINK);
  int intNow  = digitalRead(GPIO2_ASIC_INT);

  if (lastLink == -1) lastLink = linkNow;
  if (lastInt  == -1) lastInt  = intNow;

  if (linkNow != lastLink) {
    lastLink = linkNow;
    printStamp();
    Serial.print("LINK changed -> ");
    Serial.println(linkNow ? "HIGH (linked)" : "LOW (not linked)");
    if (linkNow) linkedOnce = true;
    poll_once(true);
  }

  if (intNow != lastInt) {
    lastInt = intNow;
    printStamp();
    Serial.print("INT changed -> ");
    Serial.println(intNow ? "HIGH" : "LOW");
    poll_burst(BURST_POLLS, BURST_DELAY_MS);
  }

  // auto keepalive
  if (autoKeepAlive && (now - lastKeepAliveMs >= KEEPALIVE_MS)) {
    send_10_85_wake();
    lastKeepAliveMs = now;
  }

  // auto repair if not linked
  if (!digitalRead(GPIO1_ASIC_LINK) && (now - lastRepairMs >= REPAIR_INTERVALMS)) {
    lastRepairMs = now;
    printStamp();
    Serial.println("AUTO REPAIR: LINK LOW -> retry smart pair");
    smart_pair_until_link(1);
  }

  // serial commands
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      handle_serial_command(rxLine);
      rxLine = "";
    } else {
      rxLine += c;
      if (rxLine.length() > 100) rxLine.remove(0, rxLine.length() - 100);
    }
  }

  delay(1);
}