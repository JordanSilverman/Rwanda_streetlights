/*

This sketch gets the latest epoch timestamp from the NTP server,
calls the Victron API to get the latest statistics for the state
of charge of the victron controllers. It then changes
the colour of the lights accourding to how much charge is available.

This was written by Jordan Silverman of Scene Connect Ltd.

*/

#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_FONA.h>
#include <SoftwareSerial.h>
#include <TimeLib.h>

#define FONA_RX 9
#define FONA_TX 8
#define FONA_RST 4
#define FONA_RI  7

SoftwareSerial fonaSS = SoftwareSerial(FONA_TX, FONA_RX);
SoftwareSerial *fonaSerial = &fonaSS;
Adafruit_FONA fona = Adafruit_FONA(FONA_RST);
uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);
uint8_t type;

/// ENTER THE NUMBER OF INSTALLATIONS AND THE SITE IDS AND INSTANCE IDS
/// NOTE: PUT THE CORRESPONDING SITE ID AND INSTANCE INTO THE SAME POSITION IN THE ARRAY
#define numOfInstallations 4
uint32_t idSites[] = {38205, 38215, 38218, 38253};      // 
uint32_t instance[] = {260,   260,   260,   260};       //   

const int inactiveInterval = 900;                   // Amount of time in seconds for a lamp to be inactive (not sending data) before we consider there is no energy
const long sleepTime = 200;                          // Amount of time in seconds that we should sleep for between updating the readings 120 = 2 minutes!

int SOC[numOfInstallations];                        // Initialising an array of ints for the SOC of each lamp

const char* host = "142.93.40.176:5000";            // Info for getting the data
char token[] = "X-Authorization: Token c324f8876e672ad1797cd69a9d9f62611507d25aa5a0b1ff40f9fb524d96f2fc";   // This is my personal token which authenticates me as the user

long latestReadingTime;                             // Initialiser for the latest reading time from the lamp

unsigned long previousMillis = 0 - (sleepTime*1000);

/// Neopixel setup
#define PIN 12
Adafruit_NeoPixel strip = Adafruit_NeoPixel(numOfInstallations, PIN);       //numOfInstallations should also be equal to number of lights
uint32_t red = strip.Color(255, 0, 0);
uint32_t yellow = strip.Color(255, 255, 0);
uint32_t green = strip.Color(0, 255, 0);

time_t endTime = 0;
int failure = 0;

void setup() {
  Serial.begin(115200);

  fonaSerial->begin(4800);
  if (! fona.begin(*fonaSerial)) {
    Serial.println(F("Couldn't find FONA"));
    while (1);
  }
  type = fona.type();
  Serial.println(F("FONA is OK"));
  Serial.print(F("Found "));
  switch (type) {
    case FONA800L:
      Serial.println(F("FONA 800L")); break;
    case FONA800H:
      Serial.println(F("FONA 800H")); break;
    case FONA808_V1:
      Serial.println(F("FONA 808 (v1)")); break;
    case FONA808_V2:
      Serial.println(F("FONA 808 (v2)")); break;
    case FONA3G_A:
      Serial.println(F("FONA 3G (American)")); break;
    case FONA3G_E:
      Serial.println(F("FONA 3G (European)")); break;
    default: 
      Serial.println(F("???")); break;
  }

  /// Setup of neopixels ///
  for (int i = 0; i <= numOfInstallations - 1; i++) {
    strip.setPixelColor(i, red);
  }
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'
  strip.setBrightness(180);

//  fona.setGPRSNetworkSettings(F("data.lycamobile.co.uk"), F(""), F(""));
  fona.setGPRSNetworkSettings(F("internet.mtn"), F(""), F(""));

  int n = 0;
  while((n != 1) && (n != 5)) {
    n = fona.getNetworkStatus();
    delay(200);
  }
  Serial.println("Setup done");  
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= (sleepTime*1000)) {

    Serial.println("Waking up GSM");
    fona.wake();

    int failCount = 0;
    Serial.println("Enabling GPRS");
    boolean w = false;
    while (w != true) {
      w = fona.enableGPRS(true);
      delay(200);
      failCount ++;
      if (failCount > 4){
        fona.enableGPRS(false);
        break;
      }
    }

    Serial.println("Setting Time");
    failCount = 0;
    boolean t = false;
    while (t != true) {
      t = fona.enableNTPTimeSync(true, F("time1.google.com"));
      delay(200);
      failCount ++;
      if (failCount > 4){
        break;
      }
    }    

    char theTime[23];
    fona.getTime(theTime, 23);  // make sure replybuffer is at least 23 bytes!
    int hr; int min; int sec; int day; int mnth; int yr; int timezone;
    int items = sscanf(theTime, "\"%02d/%02d/%02d,%02d:%02d:%02d%03d\"",
        &yr, &mnth, &day, &hr, &min, &sec, &timezone);    

    if (items == 7) {                   // all information found
      setTime(hr,min,sec,day,mnth,yr);
      endTime = now();
    }

    time_t startTime = endTime - inactiveInterval;                     // This is the period of time to get the data from (currently the last 15 minutes)    


    for (int i = 0; i <= numOfInstallations - 1; i++) {
  
      char url1[60];
      char url2[45];
      char url[105];
      Serial.print("ID: ");
      Serial.println(idSites[i]);
      Serial.print("Instance: ");
      Serial.println(instance[i]);
      Serial.print("Start time: ");
      Serial.println(startTime);
      Serial.print("End time: ");
      Serial.println(endTime);
      snprintf(url1, sizeof(url1), "%s/victronapi/?id=%lu&instance=%d", host, idSites[i], instance[i]);
      snprintf(url2, sizeof(url2), "&start=%lu&end=%lu", startTime, endTime);
      snprintf(url, sizeof(url), "%s%s", url1, url2);
      
        Serial.print("connecting to ");
        Serial.println(url);
      
        uint16_t statuscode;
        int16_t length;
        if (!fona.HTTP_GET_start(url, token, &statuscode, (uint16_t *)&length)) {
              Serial.println("Failed!");
              failure = 1;
              previousMillis = currentMillis - sleepTime*1000;
              break;
        }

        char response[500];
        int c = 0;
        while (length > 0) {
           while (fona.available()) {
           response[c] = fona.read();
           c++;
           length--;
           if (! length) break;
          }
        }

        Serial.print("Response: ");
        Serial.println(response);

        StaticJsonDocument<500> doc;                       // Json document setup
        // Get the Json data from the response
        DeserializationError error = deserializeJson(doc, response);
        if (error) {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.c_str());
          Serial.print("\r\n");
          //return;
        }
        else {
          Serial.println("deserializeJson() successful\r\n");
        }
        
        JsonArray records_data_51 = doc["records"]["data"]["51"];       // Find the specific SOC stats that we're looking for
        int numReadings = records_data_51.size();                       // Get the number of readings we received
      
        if (numReadings > 0) {                                          // If there are some readings returned
          latestReadingTime = records_data_51[numReadings - 1][0];      // Get the time of the latest reading
          if (endTime - latestReadingTime < inactiveInterval) {         // If it's less than the inactive interval
            SOC[i] = records_data_51[numReadings - 1][1];               // Get the latest SOC
          }
          else {SOC[i] = 0;}}                                           // If it's more than the interval, SOC = 0
        else {SOC[i] = 0;}                                              // If there are no readings, SOC = 0
      
        if (SOC[i] < 60) {strip.setPixelColor(i, red);}                     // Set the colour according to the SOC
        if (SOC[i] > 60) {strip.setPixelColor(i, green);}
      
        Serial.print("Lamp number ");
        Serial.println(i + 1);
        Serial.print("Number of Readings: ");
        Serial.println(numReadings);
        Serial.print("Latest reading time: ");
        Serial.println(latestReadingTime);
        Serial.print("SOC: ");
        Serial.println(SOC[i]);
        Serial.print("\r\n");
        Serial.println(F("\n****"));
        fona.HTTP_GET_end();
      
        Serial.println("request sent");
        failure = 0;
        doc.clear();
        delay(500);
        
    }
    
      strip.show();                                                     // Enact on the colour change
      Serial.println("Going to sleep now");
    
      fona.enableGPRS(false);
      fona.sleep();

      if (failure == 0){
      previousMillis = currentMillis;
      }
}
}
