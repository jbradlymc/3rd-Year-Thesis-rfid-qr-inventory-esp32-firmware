#include <WiFi.h>     //libraries needed for ESP 32, SD card, and RFID
#include <SPI.h>      //Software SPI for RFID
#include <MFRC522.h>
#include <SD.h>

#define RST_PIN 21    // RFID pins
#define SS_PIN 5

#define SD_MOSI 13    // SD Card pins
#define SD_MISO 34
#define SD_SCK 14
#define SD_CS 15

SPIClass spi(HSPI);         // Hardware SPI for SD card

WiFiServer server(80);     // Create Wifi Server and Client to handle connection at port 80
WiFiClient client;

const char *NETWORKID = "ESP32";                // Wifi SSID
const char *PASSWORD = "ConnectKaSakin";        // Wifi password

const String READ_AUTHORIZE = "GET /authorize"; // Strings for HTTP requests
const String READ_HISTORY = "GET /history";
const String READ_DONE = "GET /done";
const String READ_ADD = "GET /add";

int GRANTED = -1;                               // status of various operations, -1 as initial value
int DENIED = -1;
int HISTORY = -1;
int ADD = -1;

unsigned long lastActivityTime = 0;
unsigned long timeoutDuration = 5 * 60 * 1000;         // 5 minutes of inactivity in the app

String request = "";                            // String to hold incoming data from the client
String csvHistory = "";
String profTag = "";

byte readCard[4];
String MasterTag1 = "71456227";                 // ID of registered RFID tags
String MasterTag2 = "FAFE6080";
String MasterTag3 = "E28C219";
String MasterTag4 = "7933F27A";
String MasterTag5 = "6983327A";
String MasterTag6 = "793BFD7A";
String MasterTag7 = "69E2BC7A";
String MasterTag8 = "69D7987A";
String MasterTag9 = "7914417A";
String MasterTag10 = "69F61B7A";
String MasterTag11 = "1937807A";
String MasterTag12 = "89D307A";

// create an array containing all tag values
String MasterTags[] = {MasterTag1, MasterTag2, MasterTag3, MasterTag4, MasterTag5, MasterTag6, MasterTag7, MasterTag8, MasterTag9, MasterTag10, MasterTag11, MasterTag12};

String tagID = "";                            // String to hold UID of tags

MFRC522 mfrc522(SS_PIN, RST_PIN);             // Create instances for RFID

// RFID Functions
boolean getID() {                             // Read new tag if available
                                              

  if (!mfrc522.PICC_IsNewCardPresent()) {     // If a new PICC placed to RFID reader continue
    return false;
  }

  if (!mfrc522.PICC_ReadCardSerial()) {       // Since a PICC placed get Serial and continue
    return false;
  }

  tagID = "";                                 // reset String tagID to empty before reading new RFID tag

  for (uint8_t i = 0; i < 4; i++) {           // The MIFARE PICCs that we use have 4 byte UID
    readCard[i] = mfrc522.uid.uidByte[i];
    tagID.concat(String(mfrc522.uid.uidByte[i], HEX));      // Adds the 4 bytes in a single String variable
  }

  tagID.toUpperCase();
  mfrc522.PICC_HaltA();             // Stop reading
  return true;
}

String grantedResponse() {          // response if ID tag is accepted
  String response;
  response.reserve(1024);
  String body = "{\"id\":\"";
  body += tagID;
  body += "\",";
  body += "\"profName\":\"";
  body += profTag;
  body += "\"}";
  response = "HTTP/1.1 202 Accepted\r\nContent-Type: application/json\r\nContent-Length: ";
  response += body.length();
  response += "\r\nConnection: close\r\n\r\n";
  response += body;
  return response;
}

String deniedResponse() {           // response if ID tag is denied
  String response;
  response.reserve(1024);
  String body = "{}";
  response = "HTTP/1.1 403 Forbidden\r\nContent-Type: application/json\r\nContent-Length: ";
  response += body.length();
  response += "\r\nConnection: close\r\n\r\n";
  response += body;
  return response;
}

String RESPONSE = deniedResponse(); // default response is denied/forbidden

String historyResponse()  {         // response if History is requested
  String response;
  response.reserve(1024);
  String body = "{\"history\":\"";
  body += csvHistory;
  body += "\"}";
  response = "HTTP/1.1 200 History\r\nContent-Type: application/json\r\nContent-Length: ";
  response += body.length();
  response += "\r\nConnection: close\r\n\r\n";
  response += body;
  return response;
}

String addResponse()  {           // response to when a new equipment is added to the borrowed list
  String response;
  response.reserve(1024);
  String body = "{}";
  response = "HTTP/1.1 200 Add\r\nContent-Type: application/json\r\nContent-Length: ";
  response += body.length();
  response += "\r\nConnection: close\r\n\r\n";
  response += body;
  return response;
}

String doneResponse() {           // response when all transactions in the app is done, return to connect
  String response;
  response.reserve(1024);
  String body = "{}";
  response = "HTTP/1.1 200 Done\r\nContent-Type: application/json\r\nContent-Length: ";
  response += body.length();
  response += "\r\nConnection: close\r\n\r\n";
  response += body;
  return response;
}

// SD Card Functions
void createCSV(fs::FS &fs, const char *path)  {           // create a csv file inside the SD card
  Serial.printf("File Created: %s\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to create file");
    return;
  }
  file.close();
}

void appendHistory(fs::FS &fs, const char *path, const char *message) {     // append all requests in the csv file created
  Serial.printf("Logging history to: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    Serial.println("Failed to open file");
    return;
  }
  if (!file.print(message))
  {
    Serial.println("Failed to log history");
  }

  file.close();
}

void readHistory(fs::FS &fs, const char *path)  {       // read history written in the SD card
  Serial.printf("Reading file: %s\n", path);

  csvHistory = "";

  File file = fs.open(path);
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available())
  {
    char c = (char)file.read();                        // Read a character from the file
    csvHistory += c;
  }
  file.close();
}

String parseAdd(String request) {                      // String parsing of the request
  
  int index = 0;
  String parts[14];
  while (request.indexOf('/') != -1) {                 // Split the URL by '/'
  
    int slashIndex = request.indexOf('/');
    parts[index++] = request.substring(0, slashIndex);
    request = request.substring(slashIndex + 1);
  }

  String datetimein = parts[2];                        // Extract values from the URL request
  String timeused = parts[3];
  String tagid = tagID;
  String student_name = parts[5];
  String prof_name = profTag;
  String subject = parts[7];
  String purpose = parts[8];
  String inventory_code = parts[9];
  String equipment_name = parts[10];
  String logID = parts[11];
  String logDirection = parts[12];
  String remarks = parts[13];

  // Format values into CSV string
  String csvtext = datetimein + ";" + timeused + ";" + tagid + ";" + student_name + ";" + prof_name + ";" + subject + ";" + purpose + ";" + inventory_code + ";" + equipment_name + ";" + logID + ";" + logDirection + ";" + remarks;

  return csvtext;
}

void printHistory() {                                 // append or print the parsed request string into the CSV file
  String callHistory = request;
  String csvHistory = parseAdd(callHistory);
  String nextrowHistory = csvHistory + "\n";
  Serial.println(csvHistory);
  appendHistory(SD, "/history.csv", nextrowHistory.c_str());
}

void setup()  {
  Serial.begin(115200);
  SPI.begin();                                       // SPI bus for RFID
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);        // SPI bus for SD card

  mfrc522.PCD_Init(); // MFRC522

  WiFi.softAP(NETWORKID, PASSWORD);                  // Create ESP32 Access Point

  server.begin();                                    // Start ESP32 server

  if (!SD.begin(SD_CS, spi))  {                      // If SD card mounted
    Serial.println("Card Mount Failed");
    return;
  }
  
  Serial.println("SD Card initialized");
  Serial.println("Waiting for a client to connect...");

  if (!SD.exists("/history.csv")) {
    createCSV(SD, "/history.csv");
  }
}

void loop() {
  WiFiClient client = server.available();                  // Listen for incoming clients

  if (millis() - lastActivityTime > timeoutDuration) {      // Check for timeout
  
    RESPONSE = deniedResponse();
    GRANTED = 0;
    client.print(RESPONSE);
    client.flush();
    lastActivityTime = millis();                           // Reset last activity time
    Serial.println("Timeout occurred. Resetting authorization status.");
  }

  if (!GRANTED) {                      // Default response
    RESPONSE = deniedResponse();
  }

  while (getID()) {                    // reads RFID tags that are tapped in the sensor
  
    bool isTagValid = false;          // flag to be true if tagID is in the array of tags

    for (int i = 0; i < 12; i++) {    // check the 10 registered tags
    
      if (MasterTags[i] == tagID) {   // check if tagID is in array MasterTags
        isTagValid = true;
        break;
      }
    }

      if (tagID == MasterTag1) {          //1
        profTag = "Sir JP";
      }
  
      else if (tagID == MasterTag2) {    //2
        profTag = "Engr. Enmar Tuazon";
      }
  
      else if (tagID == MasterTag3) {     //3
        profTag = "Engr. Nilo Manuntag";
      }
  
      else if (tagID == MasterTag4) {    //4
        profTag = "Engr. Mary Anne Sahagun";
      }
  
      else if (tagID == MasterTag5) {    //5
        profTag = "Engr. Emmanuel Trinidad";
      }
  
      else if (tagID == MasterTag6) {    //6
        profTag = "Engr. Lester Natividad";
      }
  
      else if (tagID == MasterTag7) {    //7
        profTag = "Engr. Elias Vergara";
      }
  
      else if (tagID == MasterTag8) {    //8
        profTag = "Engr. Christian Pineda";
      }
  
      else if (tagID == MasterTag9) {    //9
        profTag = "Engr. Dorothy Joy Tongol";
      }
  
      else if (tagID == MasterTag10) {    //10
        profTag = "Engr. Anthony Tolentino";
      }

      else if (tagID == MasterTag11) {    //11
        profTag = "Engr. Nicholas Alonzo";
      }
  
      else if (tagID == MasterTag12) {    //12
        profTag = "Engr. Arlon Calma";
      }

    if (isTagValid) {                 // if tag is registered
      Serial.println(" Access Granted!");
      Serial.println(" Handler : " + profTag);
      Serial.println(" ID : " + tagID);
      RESPONSE = grantedResponse();
      GRANTED = 1;
    }

    else  {                           // if tag is not registered
      Serial.println(" Access Denied!");
      Serial.println(" ID : " + tagID);
      RESPONSE = deniedResponse();
      DENIED = 1;
      GRANTED = 0;
    }
    break;
  }

  if (client) {                     // If a client (web browser) connects
    Serial.println("A Client is connected to ESP32");
  
    while (client.connected())  {  // loop while the client's connected
      
      if (client.available()) {   // read line by line what the client (web browser) is requesting
        
        request = client.readStringUntil('\r');           // reads the request of client and store it in String request
        Serial.println();
        Serial.println("RECEIVED REQUEST: " + request);
        lastActivityTime = millis();                      // Update last activity time

        if (request.length() > 0) {                       // ensures valid requests from the client
        
          if (GRANTED) {
            Serial.println("HANDLING THE REQUEST NOW...");

            if (request.startsWith(READ_AUTHORIZE)) {      // handles GET /authorize request                                             
              Serial.println("REQUEST TO AUTHORIZE");
              //   Serial.println("Sending response: " + RESPONSE);
              client.print(RESPONSE);
              client.flush();
            }
            else if (request.startsWith(READ_HISTORY)) {   // handles GET /history request 
              Serial.println("CALLING HISTORY");
              //   Serial.println("Sending response: " + RESPONSE);
              readHistory(SD, "/history.csv");
              RESPONSE = historyResponse();
              HISTORY = 1;
              client.print(RESPONSE);
              client.flush();
            }
            else if (request.startsWith(READ_ADD))  {     // handles GET /add request
              Serial.println("ADD");
              //     Serial.println("Sending response: " + RESPONSE);
              printHistory();
              RESPONSE = addResponse();
              ADD = 1;
              client.print(RESPONSE);
              client.flush();
            }
            else if (request.startsWith(READ_DONE)) {     // handles GET /done request
              Serial.println("DONE");
              //     Serial.println("Sending response: " + RESPONSE);
              RESPONSE = doneResponse();
              GRANTED = 0;
              tagID = "";
              client.print(RESPONSE);
              client.flush();
            }
            
            else  {
              client.print(RESPONSE);
              client.flush();
            }
            
          } // end of GRANTED
          
          else  {
            RESPONSE = deniedResponse();
            client.print(RESPONSE);
            client.flush();
          }
        }
      }
      break;
    }
  }
}
