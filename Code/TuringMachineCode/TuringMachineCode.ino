#include <Arduino.h>
#include <vector>

// Pins
#define LightPin 34
#define ActiveSwitchPin 35
#define NFC_IRQ_Pin 2
#define NFC_RST_Pin 4
// Motors
#define RightMotorPin4 32
#define RightMotorPin3 33
#define RightMotorPin2 25
#define RightMotorPin1 26
#define LeftMotorPin4 27
#define LeftMotorPin3 14
#define LeftMotorPin2 12
#define LeftMotorPin1 13
int RightMotorPins[] = {RightMotorPin1, RightMotorPin2, RightMotorPin3, RightMotorPin4};
int LeftMotorPins[] = {LeftMotorPin1, LeftMotorPin2, LeftMotorPin3, LeftMotorPin4};
// SPI
#include <SPI.h>
#define MOSI 23
#define MISO 19
#define SCK 18
#define NFC_CS 5
#define SD_CS 15
SPIClass spiBus(VSPI);

// SD Card
#include <SD.h>
#define DATA_FILE_NAME "program.json" // placeholder name, figure out a name and extention later

// NFC
#include <MFRC522.h>
MFRC522 mfrc522(NFC_CS, NFC_RST_Pin); // Create MFRC522 instance.
MFRC522::MIFARE_Key key;

// Turing Machine
// Using my virtual turing machine for working code (https://github.com/BoomBoomMushroom/TuringMachineWebsite/blob/main/index.html)
#define HALT_STATE_ID -1
#define ACCEPT_STATE_ID -2
#define REJECT_STATE_ID -3
#define LEFT_MOVE false
#define RIGHT_MOVE true
struct state {
  int id; // note multiple states can have the same id; it is used for the different read values
  uint8_t readValue;
  uint8_t writeValue;
  bool moveDirection; // false for left, right for true
  int nextId; // you can use the HALT, ACCEPT, or REJECT state ids here
}
std::vector<state> states;
int currentStateID = 0; // I need to figure out some way to allow for virtually infinite state IDs, since rn we're limited by the int limit, and it's harder to read than a string

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  pinInit();
  
  spiBus.begin(SCK, MISO, MOSI, NFC_CS);
  mfrc522.PCD_Init();
  mfrc522.PCD_Init(&spiBus, NFC_CS);

  // Init SD card
  initSDCard();
  // TODO: When I get the machine built I will then setup the SD Card reading and loading code.

  // load the 3 state 2 symbol busy beaver program as an example
  states.clear();
  states.push_back({ 0, 0, 1, RIGHT_MOVE, 1 });
  states.push_back({ 0, 1, 1, RIGHT_MOVE, HALT_STATE_ID });

  states.push_back({ 1, 0, 0, RIGHT_MOVE, 2 });
  states.push_back({ 1, 1, 1, RIGHT_MOVE, 1 });

  states.push_back({ 2, 0, 1, LEFT_MOVE, 2 });
  states.push_back({ 2, 1, 1, LEFT_MOVE, 1 });
}

void loop() {
  // put your main code here, to run repeatedly:
  // NFC_loop();

  bool isActive = digitalRead(ActiveSwitchPin) == HIGH;
  if(isActive == false){
    // The button isn't pressed down, so the machine is inactive
    setLEDStatus(false);
    return;
  }

  if(currentStateID == HALT_STATE_ID || currentStateID == ACCEPT_STATE_ID || currentStateID == REJECT_STATE_ID){
    // blinking codes: 1 blink for halt, 2 blinks for accept, and 3 blanks for reject
    int numberOfBlinks = 1; // default case for HALT
    if(currentStateID == ACCEPT_STATE_ID){ numberOfBlinks = 2; }
    if(currentStateID == REJECT_STATE_ID){ numberOfBlinks = 3; }
    blinkLED(numberOfBlinks, 150); // 150ms between each blink
    delay(1500); // wait 1.5 seconds between each blink set
    return; // Don't allow the machine to tick when in one of these states because it is the end of the program.
  }

  setLEDStatus(true);
  TuringMachineStep();
  delay(100); // wait 100ms before doing the next step
}

bool TuringMachineStep(){
  uint8_t nfc_block = 2

  uint8_t readBuffer[18];
  bool readSuccess = NFC_ReadTag(nfc_block, readBuffer); // Read sector 2 in to the read buffer
  if(readSuccess == false){ return false; } // TODO: Figure out why it failed

  uint8_t valueAtHead = readBuffer[0]; // For now we will only use 1 byte of the tag to store a symbol. TODO: Expand this to allow multi byte symbols, and maybe multi block symbols
  state specificStateRule = NULL;
  for(int i=0; i<states.size(); i++){
    state compareState = states[i];
    if(compareState.id != currentStateID){ continue; }
    if(compareState.readValue != valueAtHead){ continue; }
    specificStateRule = compareState;
    break; // we found out state so we should exit the loop;
  }
  if(specificStateRule == NULL){ return false; }

  // maybe to save resources we can write over the read buffer?
  uint8_t writeBuffer[16]; // we cannot write the last 2 bytes like the read because those are checksums.
  memset(writeBuffer, 0, 16); // zero out the buffer before putting data into it
  writeBuffer[0] = specificStateRule.writeValue;

  bool writeSuccess = NFC_WriteTag(nfc_block, writeBuffer);
  if(writeSuccess == false){ return false; } // TODO: Figure out why it failed

  moveTape(specificStateRule.moveDirection == RIGHT_MOVE);
  currentStateID = specificStateRule.nextId; // Set the next state id

  return true;
}

void moveTape(bool isRight){
  // TODO: When I get the machine built I need to find how much I actually need to move the motors to get to a new tag. But for now I will use 512 (one full rotation)
  // Maybe we can just keep moving until we hit another tag?
  // Also I am currently assuming clockwise if moving right but double check this when I get the machine
  int amountOfStepsToMove = 512; // 1 full step. this is temporary until I get the physical machine
  stepMotors(amountOfStepsToMove, isRight); // Right is clockwise so just use that bool
}

void NFC_loop(){
  // Look for new cards
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  uint8_t buffer[18];
  if (NFC_ReadTag(4, buffer)) {
    Serial.println("Read 18 bytes (data + CRC):");
    for (int i = 0; i < 18; i++) {
      Serial.printf("%02X ", buffer[i]);
    }
    Serial.println();
  } else {
    Serial.println("Failed to read NFC tag.");
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(2000);
}

bool NFC_WriteTag(uint8_t block, uint8_t dataBlock[16]) {
  //uint8_t block = 4; // Block to write (not sector 0!)

  // Authenticate
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, block,
    &mfrc522.MIFARE_Key(), &mfrc522.uid
  );

  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Write data
  status = mfrc522.MIFARE_Write(block, dataBlock, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Write failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  Serial.println("Write successful.");
  return true;
}

bool NFC_ReadTag(uint8_t block, uint8_t* outputBuffer) {
  //uint8_t block = 4;
  uint8_t buffer[18];
  uint8_t size = sizeof(buffer);

  // Authenticate
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A, block,
    &mfrc522.MIFARE_Key(), &mfrc522.uid
  );

  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Read data
  status = mfrc522.MIFARE_Read(block, buffer, &size);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Read failed: ");
    Serial.println(mfrc522.GetStatusCodeName(status));
    return false;
  }

  // Copy the data to the output buffer
  memcpy(outputBuffer, buffer, 18);
  return true;
}

// "interrupt service routine" Idk when this is triggered. Maybe when the chip detects a card and then we read it from here.
void NFC_ISR(){}

void setLEDStatus(bool isOn){
  digitalWrite(LightPin, isOn ? HIGH : LOW);
}

void blinkLED(int numberOfTimes, int blinkSpacing,){
  while(numberOfTimes > 0){
    setLEDStatus(true);
    delay(blinkSpacing);
    setLEDStatus(false);
    delay(blinkSpacing);
    numberOfTimes--;
  }
}

void stepMotors(int steps, bool isClockwise){
  // 512 steps is 1 full rotation
  // TODO: Actually determine what is clockwise and what is not
  while(steps--){
    for(int i=0;i<4;i++)
    {
      int index = i;
      if(isClockwise==false){
        index -= 3; // subtract the highest index (which is 3, 4 is not exclusive because we index at 0)
        index = abs(index);
        // This should flip the index when we go the other way
        // 0 - 3 = -3 (3)
        // 1 - 3 = -2 (2)
        // 2 - 3 = -1 (1)
        // 3 - 3 = 0 (0)
      }
      digitalWrite(RightMotorPins[index],1);
      digitalWrite(LeftMotorPins[index],1);
      delay(10);
      digitalWrite(RightMotorPins[index],0);
      digitalWrite(LeftMotorPins[index],0);
    }
  }
}

void initSDCard(){
  Serial.print("Initializing SD card...");

  if (!SD.begin(chipSelect, spiBus)) {
    Serial.println("initialization failed. Things to check:");
    Serial.println("1. is a card inserted?");
    Serial.println("2. is your wiring correct?");
    Serial.println("3. did you change the chipSelect pin to match your shield or module?");
    Serial.println("Note: press reset or reopen this serial monitor after fixing your issue!");

    //while (true);
    return; // Don't hang the program, let's just return
  }

  Serial.println("initialization done.");

  // open the file. note that only one file can be open at a time,
  // so you have to close this one before opening another.
  File dataFile = SD.open(DATA_FILE_NAME);

  // if the file is available, write to it:
  if (dataFile) {

    while (dataFile.available()) {

      Serial.write(dataFile.read());
      // TODO: Load the turing machine data and states

    }

    dataFile.close();

  }
  else { // if the file isn't open, pop up an error:
    Serial.println("error opening datalog.txt");
  }
}

void pinInit()
{
  pinMode(LightPin, OUTPUT);
  pinMode(NFC_RST_Pin, OUTPUT);
  pinMode(NFC_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  pinMode(RightMotorPin4, OUTPUT);
  pinMode(RightMotorPin3, OUTPUT);
  pinMode(RightMotorPin2, OUTPUT);
  pinMode(RightMotorPin1, OUTPUT);
  pinMode(LeftMotorPin4, OUTPUT);
  pinMode(LeftMotorPin3, OUTPUT);
  pinMode(LeftMotorPin2, OUTPUT);
  pinMode(LeftMotorPin1, OUTPUT);

  pinMode(ActiveSwitchPin, INPUT_PULLDOWN);

  // idk if the interrupt is necessary so i'll comment it out for now
  //attachInterrupt(digitalPinToInterrupt(NFC_IRQ_Pin), NFC_ISR, CHANGE); // idk when the interupt should be triggered so I put CHANGE, but LOW, RISING, and FALLING all are options.
}
