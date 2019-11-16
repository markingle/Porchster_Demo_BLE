/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
//#include <BLE2902.h>
//#include <BLE2904.h>
#include <EEPROM.h>
#include <HardwareSerial.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristicA = NULL;
BLECharacteristic* pCharacteristicB = NULL;

HardwareSerial MySerial(1);


const byte numChars = 32;
char receivedChars[numChars]; // an array to store the received data
boolean newData = false;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;
uint32_t onState = 1;

//These will need to be updated to the GPIO pins for each control circuit.
int POWER = 13; //13
int TIMER_SWITCH = 2; 
int WIFI_CONNECTION = 15; //15
int WIFI_CLIENT_CONNECTED = 16; //16
int Solenoid_lock = 14;  //17
int SPEED = 2; 
int LEFT = 12; 
int RIGHT = 13;
const int ANALOG_PIN = A0;

int onoff,powerOn = 1; 

volatile byte switch_state = HIGH;
boolean pumpOn = false;
boolean timer_state = false;
boolean timer_started = false;
boolean wifi_state = false;
boolean wifi_client_conn = false;
int startup_state;

int Clock_seconds;

int characteristic_value;

int result;

//Timer variables
hw_timer_t * timer = NULL;
int ontime_value;  //number of ON minutes store in EEPROM
int offtime_value; //number of OFF minutes store in EEPROM
int ontime;   //On time setting from mobile web app
int offtime;  //Off time setting from mobile web app

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331915c"      //Porchster_Service_CBUUID
#define CHARACTERISTIC_A_UUID "beb5483e-36e1-4688-b7f5-ea07361b26b7"    //Porchster_Solenoid_Characteristic_CBUUID
#define CHARACTERISTIC_B_UUID "beb5483e-36e1-4688-b7f5-ea07361b26c8"    //Porchster_Scanner_Characteristic_CBUUID


void unlock(){
  digitalWrite(Solenoid_lock, HIGH);
  delay(2000);
  digitalWrite(Solenoid_lock,LOW);
}

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      Serial.println("Characteristic Callback");
      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("Power New value: ");
        
        char Str[] = {value[0], value[1], value[2], value[3], value[4]};
        
        Serial.println(atoi(Str));
        powerOn = atoi(Str);
        Serial.println();
        Serial.println("*********");

        if (powerOn == 1)
                  {
                    startup_state = 1;
                    EEPROM.write(0,startup_state);
                    EEPROM.commit();
                    byte value = EEPROM.read(0);
                    Serial.println("Power turned ON " + String(value));
                    timer_state = true;
                    pumpOn = true;
                    //digitalWrite(Solenoid_lock, HIGH);
                    unlock();
                  }
         if (powerOn == 0)
                  {
                    startup_state = 0;
                    EEPROM.write(0,startup_state);
                    EEPROM.commit();
                    byte value = EEPROM.read(0);
                    Serial.println("Power turned OFF " + String(value));
                    timer_state = false;
                    pumpOn = false;
                    //digitalWrite(Solenoid_lock, LOW);
                    //unlock();
                  }
      }
    };
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};



void setup() {

  pinMode(POWER, OUTPUT);
  //pinMode(TIMER_SWITCH, OUTPUT);
  pinMode(WIFI_CONNECTION, OUTPUT);
  pinMode(WIFI_CLIENT_CONNECTED, OUTPUT);
  pinMode(Solenoid_lock, OUTPUT);
  pinMode(SPEED, OUTPUT);
  
  digitalWrite(POWER, LOW);
  //digitalWrite(TIMER_SWITCH, LOW);
  digitalWrite(WIFI_CONNECTION, LOW);
  digitalWrite(WIFI_CLIENT_CONNECTED, LOW);
  digitalWrite(Solenoid_lock, LOW);
  digitalWrite(SPEED, LOW);
  
  Serial.begin(115200);
  MySerial.begin(9600, SERIAL_8N1, 3, 1);
  Serial.print("My Serial started!");

  Serial.println("Start up state of pump is OFF");
  timer_state = false;
  pumpOn = false;
  pinMode(TIMER_SWITCH, OUTPUT);
  digitalWrite(TIMER_SWITCH, LOW);
  digitalWrite(Solenoid_lock, LOW);
  
  EEPROM.begin(3); //Index of three for - On/Off state 1 or 0, OnTime value, OffTime value

  //Determine the value set for ON Time in EEPROM
  ontime_value = EEPROM.read(1);
  if (ontime_value > 0) 
  {
    ontime = ontime_value;
    Clock_seconds = ontime_value*5;
    Serial.println("On Time setting is " + String(Clock_seconds));
  }
    
  //Determine the value set for OFF Time inEEPROM
  offtime_value = EEPROM.read(2);
  if (offtime_value > 0) 
  {
    offtime = offtime_value;
    Clock_seconds = offtime_value*5;
    Serial.println("OFF Time setting is " + String(Clock_seconds));
  }

  
  // Create the BLE Device
  BLEDevice::init("Prstr");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic A
  pCharacteristicA = pService->createCharacteristic(
                      CHARACTERISTIC_A_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

// Create a BLE Characteristic B
  pCharacteristicB = pService->createCharacteristic(
                      CHARACTERISTIC_B_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
                    


  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  //pCharacteristicA->addDescriptor(new BLE2902());
  //pCharacteristicB->addDescriptor(new BLE2904());

  pCharacteristicA->setCallbacks(new MyCallbacks());
  pCharacteristicB->setCallbacks(new MyCallbacks());
  
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}


void IRAM_ATTR onoffTimer(){

  switch (powerOn) {

    case 0:
      if (pumpOn == false) 
      {
        digitalWrite(TIMER_SWITCH,LOW);  //We only need to set TIMER_SWITCH once....set pumpOn to TRUE in prep for end of OFFTIME.
        pumpOn = true;
        Serial.println("Turning OFF pump");
        digitalWrite(Solenoid_lock, LOW);
      }
      Serial.println("Pump has been OFF for " + String(Clock_seconds) + " seconds");
      
      break;
    
    case 1:
      if (pumpOn == true) {
        digitalWrite(TIMER_SWITCH,HIGH);  //We only need to set TIMER_SWITCH once....set pumpOn to FALSE in prep for end of OFFTIME.
        pumpOn = false;
        Serial.println("Turning ON pump");
        digitalWrite(Solenoid_lock, HIGH);
      }
      Serial.println("Pump is running for " + String(Clock_seconds) + " seconds");
    
      
      break;
  }
}


void loop() {
    // notify changed value
    if (deviceConnected) {
        //Serial.println("device connected");
        //pCharacteristicD->setValue((uint8_t*)&value, 4);
        //pCharacteristicD->notify();
        value++;
        digitalWrite(WIFI_CLIENT_CONNECTED, LOW);
        delay(500); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
        //pCharacteristicB->setValue((uint8_t*)&onState, 4);
        //pCharacteristicB->notify(); 
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        Serial.println(" Device is connecting");
        oldDeviceConnected = deviceConnected;
    }
    recvWithEndMarker();
}

void recvWithEndMarker() {
   static byte ndx = 0;
   char carriageReturn = '\r';
   char rc;
 
 
 while (MySerial.available() > 0) {
    rc = MySerial.read();
     if (rc != carriageReturn) {
         receivedChars[ndx] = rc;
         ndx++;
         if (ndx >= numChars) {
         ndx = numChars - 1;
         }
      } else {
           receivedChars[ndx] = '\0'; // terminate the string
           ndx = 0;
           //newData = true;
           MySerial.print(receivedChars);
           pCharacteristicB->setValue((uint8_t*)&receivedChars, 4);
           pCharacteristicB->notify();
           unlock();
           }
      }
}
