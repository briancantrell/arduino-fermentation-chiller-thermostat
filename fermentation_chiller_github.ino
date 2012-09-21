#include <stdlib.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <Ethernet.h>
#include <OneWire.h>

const int potPin = 1;
const int lcdPin = 2;
const int thermPin = 3;
const int fanPin = 9;

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
IPAddress ip(192,168,1,99); //local IP if DHCP fails
EthernetClient client;
char server[] = "api.cosm.com";   // name address for cosm API

#define APIKEY         "" // your cosm api key
#define FEEDID         000000 // your cosm feed ID
#define USERAGENT      "Arduino Fermentation Chiller"

unsigned long lastConnectionTime = 0;          // last time you connected to the server, in milliseconds
boolean lastConnected = false;                 // state of the connection last time through the main loop
const unsigned long postingInterval = 10*1000; //delay between updates to Cosm.com

SoftwareSerial LCD = SoftwareSerial(0, lcdPin);
const int LCDdelay = 10;

OneWire ds(thermPin);

int prevTemp = 0;
int potVal = 0;
int targetTemp = 70;
int prevTargetTemp = 0;

static char dtostrfbuffer[15];

void setup(){
  pinMode(lcdPin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  LCD.begin(9600);
  clearLCD();
  Serial.begin(9600);

  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // DHCP failed, so use a fixed IP address:
    Ethernet.begin(mac, ip);
  }
  
  float currentTemp = getTempInF(); 
  updateLCD(currentTemp, getTargetTemp());
}

void loop(){
  
  float currentTemp = getTempInF();
  
  if(currentTemp != prevTemp){  
    updateCurrentTemp(currentTemp);
  }
  
  targetTemp = getTargetTemp();

  if(targetTemp != prevTargetTemp){  
    updateTargetTemp(targetTemp);
  }
  
  if(currentTemp > targetTemp){
     digitalWrite(fanPin, HIGH);
  }else{
     digitalWrite(fanPin, LOW);
  }
  
  prevTargetTemp = targetTemp;
  prevTemp = currentTemp;
  
  // ETHERNET
  // if there's incoming data from the net connection.
  // send it out the serial port.  This is for debugging
  // purposes only:
  if (client.available()) {
    char c = client.read();
    Serial.print(c);
  }
  // if there's no net connection, but there was one last time
  // through the loop, then stop the client:
  if (!client.connected() && lastConnected) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
  }

  // if you're not connected, and ten seconds have passed since
  // your last connection, then connect again and send data:
  if(!client.connected() && (millis() - lastConnectionTime > postingInterval)) {
     String payload = cosmPayload(currentTemp, targetTemp);
     logTemp(payload);
  }
  // store the state of the connection for next time through
  // the loop:
  lastConnected = client.connected();
  // END ETHERNET 
}


int getTargetTemp(){
  potVal = analogRead(potPin);
  return potVal / 20 + 40;
}

void updateCurrentTemp(float currentTemp){
    lcdPosition(0,6);
    LCD.print(currentTemp);
    delay(LCDdelay);
}

void updateTargetTemp(int targetTemp){
    lcdPosition(1,8);
    LCD.print(targetTemp);
    delay(LCDdelay);
}

void updateLCD(float currentTemp, int targetTemp){
  clearLCD();
  lcdPosition(0,0);
  LCD.print("Temp: ");
  LCD.print(currentTemp);
  LCD.print("F");
  
  lcdPosition(1,0);
  LCD.print("Target: ");
  LCD.print(targetTemp);
  LCD.print("F");

  delay(LCDdelay);
}

String cosmPayload(float currentTemp, int targetTemp) {
  String payload = "Temp,";
  payload += dtostrf(currentTemp, 8, 2, dtostrfbuffer);
  payload += "\nTarget,";
  payload += targetTemp;
  return payload; 
}
  

// this method makes a HTTP connection to the server:
void logTemp(String payload) {
//  Serial.print("CurrentTemp: ");
//  Serial.println(currentTemp); 
//  Serial.print("targetTemp: ");
//  Serial.println(targetTemp);
  
  // if there's a successful connection:
  if (client.connect(server, 80)) {
    Serial.println("connecting...");
    // send the HTTP PUT request:
    client.print("PUT /v2/feeds/");
    client.print(FEEDID);
    client.println(".csv HTTP/1.1");
    client.println("Host: api.cosm.com");
    client.print("X-ApiKey: ");
    client.println(APIKEY);
    client.print("User-Agent: ");
    client.println(USERAGENT);
    client.print("Content-Length: ");
    client.println(payload.length());
    
    client.println("Content-Type: text/csv");
    client.println("Connection: close");
    client.println();

    client.println(payload);
  } 
  else {
    // if you couldn't make a connection:
    Serial.println("connection failed");
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
  }
   // note the time that the connection was made or attempted:
  lastConnectionTime = millis();
}

float getTempInF(){
  float temp = getTemp();
  return temp  * 9 / 5 + 32;
}

float getTemp(){
  //returns the temperature from one DS18S20 in DEG Celsius
  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
      //no more sensors on chain, reset search
      ds.reset_search();
      return -1000;
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return -1000;
  }

  if ( addr[0] != 0x10 && addr[0] != 0x28) {
      Serial.print("Device is not recognized");
      return -1000;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44,1); // start conversion, with parasite power on at the end

  byte present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE); // Read Scratchpad
  
  for (int i = 0; i < 9; i++) { // we need 9 bytes
    data[i] = ds.read();
  }
  
  ds.reset_search();
  
  byte MSB = data[1];
  byte LSB = data[0];

  float tempRead = ((MSB << 8) | LSB); //using two's compliment
  float TemperatureSum = tempRead / 16;
  
  return TemperatureSum;
}

void lcdPosition(int row, int col) {
  LCD.write(0xFE);   //command flag
  LCD.write((col + row*64 + 128));    //position 
  delay(LCDdelay);
}

void clearLCD(){
  LCD.write(0xFE);   //command flag
  LCD.write(0x01);   //clear command.
  delay(LCDdelay);
}

void backlightOn() {  //turns on the backlight
  LCD.write(0x7C);   //command flag for backlight stuff
  LCD.write(157);    //light level.
  delay(LCDdelay);
}

void backlightOff(){  //turns off the backlight
  LCD.write(0x7C);   //command flag for backlight stuff
  LCD.write(128);     //light level for off.
   delay(LCDdelay);
}

void serCommand(){   //a general function to call the command flag for issuing all other commands   
  LCD.write(0xFE);
}
