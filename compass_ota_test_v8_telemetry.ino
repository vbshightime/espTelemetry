/************************************************
 * version 6 
 * Changes Made-
 * 1. Battery mapping more than 100 and less than 0
 * 2. WiFi counter from 20 to 30
 * 3. Going to 10 min deepsleep when not connecting to wifi 
 */
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include "FS.h"
#include "Updater.h"
#define MESSAGE_MAX_LEN 512
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "AgNextCaptive.h"
#define ONE_WIRE_BUS 2 // port of NodeMCU=D4

unsigned int startTime = 0;
unsigned int beforeConnec = 0;
unsigned int afterConnec = 0;
unsigned int beforeUp = 0;
unsigned int afterUp = 0;
int connStatus = 0;

//JSON buffer
DynamicJsonBuffer jsonBuffer;

// For Dallas Temp Sensor12
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

//************** Auxillary functions******************//
ESP8266WebServer server(80);
HTTPClient http;


int gatewayConfig = 0;  //flag for portal

String macid = "\"2ef\"";
/**********Soft AP network parameters***************/
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

//**********softAPconfig Timer*************//
unsigned long APTimer = 0;
unsigned long APInterval = 60000;

//*********SSID and Pass for AP**************//
const char* ssidAPConfig = "adminesp";
const char* passAPConfig = "adminesp";

//*********payload structure for values post **************//
const char MESSAGE_BODY[] = "{\"deviceId\":\"%s\", \"macId\":%s, \"temp\":%d, \"batteryLevel\":%d}";
char messagePayload[MESSAGE_MAX_LEN];

//*********payload structure for telemetry post **************//
const char TELE_BODY[] = "{\"deviceId\":\"%s\", \"startTime\":%d, \"beforeConnec\":%d, \"afterConnec\":%d, \"beforeUp\":%d, \"afterUp\":%d, \"connStatus\":%d, \"Counter\":%d, \"rssi\":%d}";
char telePayload[MESSAGE_MAX_LEN];

//*********payload structure for OTA **************//
const char OTA_BODY[] = "{\"deviceId\":\"%s\", \"firmwareVersion\":\"%s\"}";
char otaPayload[MESSAGE_MAX_LEN];

String url = "http://3.19.52.97:9955/api/data/download-file?filename=compass_ota.ino.bin";//URL for ota file download HAH
//String url = "http://3.19.52.97:9933/api/data/download-file?filename=ota_update.ino.nodemcu.bin";//URL for ota file download HAH test
String urlSend = "http://3.19.52.97:9955/api/data";
String postTele = "http://ptsv2.com/t/agnext/post";

String OtaStatus; //Status for initiate OTA and reset before reset
String fileName; //Name of downloaded file

//*********Store fw version**************//
String newfWVersion;
String oldfWVersion;

volatile uint8_t PUSH_PIN = 5; //Interrupt pin for portal
int startUpdate = 0; //start update after confirming size
int flag = 0;//flag for going to sleep
bool isConnected = true; //Check for AP connected

//***************Battery****************//
int maxVal = 420; 
int percen = 0;

//************Sensor Cut off8**************//
int cutPin = 4;

 int counter = 0;

//{"success":"true","message":"Save Success","data":[{"id":352963,"temp":"24","coldStoreId":96,"time":"10.12","date":"09/07/2019","statusId":1,"status":"active","deviceId":"AGNEXTSN00060","batteryLevel":"22","firmwareVersion":"V1","deviation":true}],"count":1,"timestamp":"07/09/2019 04:42:49"}

void ICACHE_RAM_ATTR ISR_int() {
       APTimer = millis();
       detachInterrupt(digitalPinToInterrupt(PUSH_PIN));
       gatewayConfig = 1;
  }

  //****************************EEPROM Read****************************//
String read_string(int l, int p) {
    String temp;
    for (int n = p; n < l + p; ++n)
      {
        // read the saved password from EEPROM
        if (char(EEPROM.read(n)) != ';') {
      temp += String(char(EEPROM.read(n)));
      } else n = l + p;
   }
     return temp;
}


void setup()
{   
    startTime = millis();
    Serial.begin(115200);//Serial connection
    EEPROM.begin(256);
    pinMode(A0, INPUT); // taking input from moisture sensor
    pinMode(PUSH_PIN, INPUT);//Interrupt Pin
    pinMode(cutPin,OUTPUT); //Sensor transistor control pin
    //Serial.println("Version No. 6");
    digitalWrite(cutPin,HIGH);
    attachInterrupt(digitalPinToInterrupt(PUSH_PIN), ISR_int, CHANGE);
    
    //ToDo 2
    sensors.begin();
    OtaStatus = read_string(10, 190);
    //Serial.println(OtaStatus);
    if (OtaStatus == "1") {
           OtaStatus = "0";
           ROMOtaWrite(OtaStatus);
           performOTA();
    }
}

void loop()
{  
//  if(Serial.available()>0){
//      if(Serial.read()=='r'){
//          Serial.println("received r");
//          handleWebForm();
//        }
//    }
  if (gatewayConfig) {
          //Serial.println("interruptGenerated");
          handleWebForm();
    }
  if (WiFi.status() == WL_CONNECTED) //Check WiFi connection status
      {
         serveHTTP();
       }
    else
       {
          reconnectWiFi();
          serveHTTP();
        }
  goToSleep();
   
}


/*****************OTA function************/
void performOTA() {
  //Serial.println("Enter performOTA");
     OtaStatus = read_string(10, 190);
     //Serial.println(OtaStatus);
     if (OtaStatus == "1") {
         ESP.reset();
       }
     if (!SPIFFS.begin()) {
            Serial.println("An Error has occurred while mounting SPIFFS");
           goToSleep();
            return;
    }
     size_t size;
     Serial.println(SPIFFS.format() ? "formated" : "error while formating");
     File f = SPIFFS.open("/otaFile.bin", "w");
      if (f) {
          http.begin(url);
          int httpCode = http.GET();
          if (httpCode > 0) {
              if (httpCode == HTTP_CODE_OK) {
                  size = http.writeToStream(&f);
                  if(size != 0){
                      Serial.println("File Written");
                    }
                  fileName = f.name();
        }
      } else {
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
     }
    f.close();
   } 
    http.end();
    //Serial.println(size);
    //https://agnext-jasmine.s3.us-east-2.amazonaws.com/iot/copass_program1_1.ino.nodemcu.bin
    File file = SPIFFS.open("/otaFile.bin", "r");
    if (!file) {
       Serial.println("Failed to open file for reading");
       return;
    }
       Serial.println("Starting update..");
       //uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
       size_t fileSize = file.size();
       
      if (!Update.begin(fileSize)) {
           Update.printError(Serial);
           //Serial.println("Cannot do the update");
           goToSleep();
           return;
      }
      fileSize = Update.writeStream(file);
      if (Update.end()) {
          Serial.println("Successful update");
          String deviceId = String(read_string(20, 100));
          int t = 2;
          int percentage = 90;
          snprintf(messagePayload, MESSAGE_MAX_LEN, MESSAGE_BODY, deviceId.c_str(),macid.c_str(),t,percentage);
          http.begin(urlSend); //SenseNext production
          http.addHeader("Content-Type" , "application/json");  //Specify content-type header
          int httpCode = http.POST(messagePayload);     //Send the request
          if(httpCode > 0 && httpCode==HTTP_CODE_OK){
          String payload = http.getString();   //Get the response payload
          //Serial.println(httpCode);  //Print HTTP return code
          //Serial.println(payload);
          JsonObject& root = jsonBuffer.parseObject(payload);
          JsonArray& requests = root["data"];
          for (auto& request : requests) {
            const char* value = request["firmwareVersion"];
            newfWVersion = (String)value;
          }
         /**************Issue 2**************/
          ROMVerWrite(newfWVersion);
          //Serial.print("new version is");
          String ver ="";
          ver = read_string(10,170);
          //Serial.println(read_string(10, 170));
          snprintf(otaPayload, MESSAGE_MAX_LEN, OTA_BODY, deviceId.c_str(), ver.c_str()); 
          int otaCode = http.PUT(otaPayload);
          if(otaCode > 200){
              String otaLoad = http.getString();
              //Serial.println(otaLoad);
              }
              OtaStatus = "0";
              ROMOtaWrite(OtaStatus);
          }
      }else {
          Serial.println("Error Occurred: " + String(Update.getError()));
          return;
      }
      file.close();
      Serial.print("Reset in....");
      for (int i = 0; i < 5; i++) {
         Serial.printf("%d", i);
      }
     ESP.restart();
  // }
}

//----------Write to ROM-----------//
void ROMwrite(String s, String p, String id, String delays) {
   s += ";";
   write_EEPROM(s, 0);
   p += ";";
   write_EEPROM(p, 50);
   id += ";";
   write_EEPROM(id, 100);
   delays += ";";
   write_EEPROM(delays, 150);
   EEPROM.commit();
  }

//----------Write version to ROM-----------//
void ROMVerWrite(String v) {
      v += ";";
      write_EEPROM(v, 170);
      EEPROM.commit();
}

//----------Write otaStatus to ROM-----------//
void ROMOtaWrite(String ota) {
      ota += ";";
      write_EEPROM(ota, 190);
      EEPROM.commit();
  }


//***********Write to ROM**********//
void write_EEPROM(String x, int pos) {
     for (int n = pos; n < x.length() + pos; n++) {
     //write the ssid and password fetched from webpage to EEPROM
     EEPROM.write(n, x[n - pos]);
   }
}

void handleWebForm() {
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, netMsk);
        //Serial.println(WiFi.softAP(ssidAPConfig, passAPConfig) ? "Configuring softAP" : "kya yaar not connected");
        //Serial.println(WiFi.softAPIP());
        server.begin();
        server.on("/", handleDHCP);
        server.onNotFound(handleNotFound);
        APTimer = millis();
        while (isConnected && millis() - APTimer <= APInterval) {
        server.handleClient();
      }
          ESP.restart();
          reconnectWiFi();
}


void handleDHCP() {
      if (server.args() > 0) {
           for (int i = 0; i <= server.args(); i++) {
           //Serial.println(String(server.argName(i)) + '\t' + String(server.arg(i)));
      }
            if (server.hasArg("ssid") && server.hasArg("passkey") && server.hasArg("device") && server.hasArg("sensor_list")) {
              /*for (int i = 0 ; i < EEPROM.length() ; i++) {
              EEPROM.write(i, 0);
            }*/
                 ROMwrite(String(server.arg("ssid")), String(server.arg("passkey")), String(server.arg("device")), String(server.arg("sensor_list")));
                 isConnected = false;
      }
   } else {
    String webString = FPSTR(HTTPHEAD);
             webString += FPSTR(HTTPBODYSTYLE);
             webString += FPSTR(HTTPBODY);
             webString += FPSTR(HTTPCONTENTSTYLE);
             webString += FPSTR(HTTPDEVICE);
             String device = String(read_string(20, 100));
             webString.replace("{s}", device);
             webString += FPSTR(HTTPFORM);
             webString += FPSTR(HTTPLABLE1);
             webString += FPSTR(HTTPLABLE2);
             webString += FPSTR(HTTPLABLE3);
             webString += FPSTR(HHTTPDELAY);
             webString += FPSTR(HTTPSUBMIT);
             webString += FPSTR(HTTPCLOSE);
             //File file = SPIFFS.open("/AgNextCaptive.html", "r");
             //server.streamFile(file,"text/html");
             server.send(200, "text/html", webString);
             //file.close();
    }
}

//****************HANDLE NOT FOUND*********************//
void handleNotFound()
{
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    server.send(404, "text/plain", message);
}


//****************************Connect to WiFi****************************//
void reconnectWiFi() {
    beforeConnec = millis();
    WiFi.mode(WIFI_STA);
    //WiFi.disconnect();
          String string_Ssid = "";
          String string_Password = "";
          string_Ssid = read_string(30, 0);
          string_Password = read_string(30, 50);
          //Serial.println("ssid: " + string_Ssid);
          //Serial.println("Password: " + string_Password);
    WiFi.begin(string_Ssid.c_str(), string_Password.c_str());
    while (WiFi.status() != WL_CONNECTED)
     {
      delay(300);
            ////Serial.print(".");
         if (counter == 1000) {
             //Serial.println("going to sleep for 600e6");
             ESP.deepSleep(600e6);
             connStatus = 1;
        } 
      counter++;
   }
    //Serial.print("Connected to:\t");
    //Serial.println(WiFi.localIP());
    afterConnec = millis();
}


//****************************ServeHTTP****************************//
void serveHTTP() {
  beforeUp = millis();
  //Serial.println("Entering serveHTTP");
    String deviceId = String(read_string(20, 100));
    sensors.requestTemperatures();
    int t = sensors.getTempCByIndex(0);
    //Serial.print("Temperature");
    //Serial.println(t);
    if (t <= -40 || t >= 50)
     {
       //Serial.print("-127 detect");
       t = 99;
     }
   float batVol=analogRead(A0)*0.0040; //finalVolt = (1/1024)(1/VD1)(1/VD2)    external VD [VD1 = 3.3kohm/(1kohm+3.3kohm)]  Internal VD[VD2 = 100kohm/(100kohm+220kohm)]    
    Serial.printf("Battery=%f \n",batVol);
    Serial.printf("analogValue %d",analogRead(A0));
    if(batVol>maxVal){
       maxVal = batVol;
        percen = map(int(batVol*100),327,maxVal,0,100);
        //Serial.printf("Battery Percentage %d \n",percen);
        if(percen<0){
             percen = 0;
          }
        if(percen>100){
             percen = 100;
          }
    }else{
         //Serial.printf("Max Value %d \n",maxVal);
         percen = map(int(batVol*100),327,maxVal,0,100);
         //Serial.printf("Battery Percentage %d \n",percen);
         if(percen<0){
             percen = 0;
          }  if(percen>100){
             percen = 100;
          }
      }
    snprintf(messagePayload, MESSAGE_MAX_LEN, MESSAGE_BODY, deviceId.c_str(), macid.c_str(), t, percen);
    http.begin(urlSend);//zx     //test
    http.addHeader("Content-Type" , "application/json");  //Specify content-type header
      int httpCode = http.POST(messagePayload);     //Send the request
      /**************Issue 1**************/
       if (httpCode == HTTP_CODE_OK && httpCode > 0) {
      String payload = http.getString();   //Get the response payload
      //Serial.println(httpCode);  //Print HTTP return code
      //Serial.println(payload);
      otaCheck(payload);  
    }
  http.end();      
  afterUp=millis();
}

void goToSleep(){
  String deviceId = String(read_string(20, 100));
  int n = WiFi.scanNetworks();
  int rssi;
  for(int i=0;i<=n;i++){
      String ssid = WiFi.SSID(i);
      if(ssid.indexOf("Agnextsn")>=0){
           rssi = WiFi.RSSI(i);
        }
    }
  snprintf(telePayload, MESSAGE_MAX_LEN, TELE_BODY, deviceId.c_str(),startTime,beforeConnec,afterConnec,beforeUp,afterUp,connStatus,counter,getRSSIasQuality(rssi));
   http.begin(postTele);//zx     //test
    http.addHeader("Content-Type" , "application/json");  //Specify content-type header
    int httpCode = http.POST(telePayload);     //Send the request
    if (httpCode == HTTP_CODE_OK && httpCode > 0) {
      String payload = http.getString();   //Get the response payload
      //Serial.println(payload);
      }
       digitalWrite(cutPin,LOW);
    String delays = read_string(20,150);
       //Serial.println(delays);
       //Serial.println("Entering DeepSleep Mode for" + delays);
       if(delays=="120e6"){
              ESP.deepSleep(120e6);// DeepSleep Mode for 1800 sec(30min)// 60e6 --- 1 min//300e6 --- 5 min // 2100e6---35 min //zx        
        }else if(delays=="300e6"){
                ESP.deepSleep(300e6);// DeepSleep Mode for 1800 sec(30min)// 60e6 --- 1 min//300e6 --- 5 min // 2100e6---35 min //zx        
          }else if(delays =="1200e6"){
                    ESP.deepSleep(2100e6);// DeepSleep Mode for 1800 sec(30min)// 60e6 --- 1 min//300e6 --- 5 min // 2100e6---35 min //zx        
            }
}

void otaCheck(String load){
  String succ = "";
  JsonObject& root = jsonBuffer.parseObject(load);
      JsonArray& requests = root["data"];
      for (auto& request : requests) {
         const char* value = request["firmwareVersion"];
         newfWVersion = (String)value;
        }
      const char * value1 = root["success"];
      succ = (String) value1;
      //Serial.println(newfWVersion);
      String oldfWVersion = read_string(10, 170);
      //Serial.println("old firmware version is");
      //Serial.println(oldfWVersion);
      if (newfWVersion != oldfWVersion) {
      OtaStatus = "1";
      ROMOtaWrite(OtaStatus);
      performOTA();
      }
  }  

  int getRSSIasQuality(int RSSI) {
  int quality = 0;
  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}
