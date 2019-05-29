/**
   ESP8266/Arduino Slack DoorBot

   Based on https://github.com/urish/arduino-slack-bot by Uri Shaked.

   This project is brought to you by open software and open hardware. Without it, we wouldn't have nice things. "Something, something, shoulders of giants."

   Licensed under the MIT License
*/

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>

#include "lib/webSockets/WebSocketsClient.h"
#include "lib/webSockets/WebSocketsClient.cpp"
#include "lib/webSockets/WebSockets.cpp"
/**
      Modified websockets lib to disable cert fingerprint verification as workaround for failed WSS:// connections
      See https://github.com/Links2004/arduinoWebSockets/issues/428 for more info
 */


#include <ArduinoJson.h>

#define SLACK_SSL_FINGERPRINT "C1 0D 53 49 D2 3E E5 2B A2 61 D5 9E 6F 99 0D 3D FD 8B B2 B3"
 /** 
  *  If Slack changes their SSL cert, you will need to update this ^
  *  This cert is currently set to expire Feb 12, 2021, but could change sooner.
  *  
  *  The following command can be used to get the current ssl fingerprint, from bash (linux) and possibly mac:
  *  
  *   openssl s_client -connect api.slack.com:443 < /dev/null 2>/dev/null | openssl x509 -fingerprint -noout -in /dev/stdin | sed 's/:/ /g' | cut -d '=' -f 2
  */

#include "Secrets.h"
 /**
  
 ---------------------------------------
 *  Secrets.h contains the following:  *
 ---------------------------------------
 
#define SLACK_BOT_TOKEN "slackbottokenhere" // Get token by creating new bot integration at https://my.slack.com/services/new/bot 
#define WIFI_SSID       "wifinetworkname"
#define WIFI_PASSWORD   "wifipassword"
 
 */
#define RELAY_PIN        D2

// set to true for dev or debugging
#define DEBUG_SERIAL_PRINT true

#define SUCCESS_RESP_MSG  "Your wish is my command!"
#define FAIL_RESP_MSG     "You didn't say the magic words."

ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;


unsigned long nextCmdId = 1UL; // signed longs overflow to a large negative value, which isn't what we want. Even though it would probably take 13 years :P
bool connected = false;

/**
  Sends a ping message to Slack. Call this function immediately after establishing
  the WebSocket connection, and then every 5 seconds to keep the connection alive.
*/
void sendPing() {
  char nxtCmd[10]; // unsigned long is max ten char in base 10 representation (4,294,967,295)
  ultoa(nextCmdId, nxtCmd, 10); // converts unsigned long into chars. last param is "base", as in decimal (base 10) number, binary (base 2), hex (base 16), etc
  nextCmdId++;
  String json = "{\"type\":\"ping\",\"id\":" + String(nxtCmd) + "}"; // hand writing serialized json because it was easier than creating an object, adding the properties and values, then serializing it
  webSocket.sendTXT(json);
}

/**
  Sends a text message to a particular slackChannel
 */
void respond(const char* slackChannel, String txtMsg) {
    char nxtCmd[10]; // nextCmdId is an "unsigned long", max value 4,294,967,295 which is 10 chars
    ultoa(nextCmdId, nxtCmd, 10); // convert from unsigned long to chars
    String json = "{\"type\":\"message\",\"id\":" + String(nxtCmd) + ",\"channel\":\"" + String(slackChannel) + "\",\"text\":\"" + txtMsg + "\"}"; // hand write serialized json, even tho we have a json lib
    webSocket.sendTXT(json);
    nextCmdId++;
}
void toggleRelay() {
    digitalWrite(RELAY_PIN, HIGH);
    delay(200);
    digitalWrite(RELAY_PIN, LOW);
    delay(125);
    digitalWrite(RELAY_PIN, HIGH);
    delay(200);
    digitalWrite(RELAY_PIN, LOW);
}
/**
  Deserialize response and get the channel, type, and text
  If text starts and ends with the magic words, do stuff
*/
void processSlackMessage(char *payload) {
  
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, payload);
  const char* slackChannel = doc["channel"];
  const char* type = doc["type"];
  const char* text = doc["text"];
  String txt = String( text );
  String chanString = String(slackChannel);
  
  bool publicChan = chanString.startsWith("C");
  // https://stackoverflow.com/questions/41111227/how-can-a-slack-bot-detect-a-direct-message-vs-a-message-in-a-channel

  if ( !publicChan && String(type) == "message" ) {
    // its a direct message
    if ( txt.startsWith("open") && txt.endsWith("sesame") ) {
        respond(slackChannel, SUCCESS_RESP_MSG);
        toggleRelay();
    } else {
        respond(slackChannel, FAIL_RESP_MSG);
    }
  }
  
}

/**
  Called on each web socket event. Handles disconnection, and also
  incoming messages from slack.
*/
void webSocketEvent(WStype_t type, uint8_t *payload, size_t len) {
  switch (type) {
    case WStype_DISCONNECTED:
      if (DEBUG_SERIAL_PRINT) {
        Serial.printf("[WebSocket] Disconnected :-( \n");
      }
      connected = false;
      break;

    case WStype_CONNECTED:
      if (DEBUG_SERIAL_PRINT) {
        Serial.printf("[WebSocket] Connected to: %s\n", payload);
      }
      sendPing();
      break;

    case WStype_TEXT:
      if (DEBUG_SERIAL_PRINT) {
        Serial.printf("[WebSocket] Message: %s\n", payload);
      }
      processSlackMessage((char*)payload);
      break;
  }
}

/**
  Establishes a bot connection to Slack:
  1. Performs a REST call to get the WebSocket URL
  2. Conencts the WebSocket
  Returns true if the connection was established successfully.
*/
bool connectToSlack() {
  
  // Step 1: Find WebSocket address via RTM API (https://api.slack.com/methods/rtm.connect)
  HTTPClient http;
  http.begin("https://slack.com/api/rtm.connect?token=" SLACK_BOT_TOKEN, SLACK_SSL_FINGERPRINT);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    if (DEBUG_SERIAL_PRINT) {
      Serial.printf("HTTP GET failed with code %d\n", httpCode);
    }
    if (httpCode == 429) {
      /**
        the RTM API can only be called once per minute! So if it didn't work the first time, we'll just wait 60 seconds.
        It would be better to parse the "Retry-After" header, as it is probably somewhat < 60 seconds
       */
      if (DEBUG_SERIAL_PRINT) {
        Serial.printf("[Warn] Rate limited, waiting the max (60 secs) rather than parsing the header");
      }
      delay(60000);
    }
    return false;
  }
  WiFiClient *client = http.getStreamPtr();
  client->find("wss:\\/\\/");
  String host = client->readStringUntil('\\');
  String path = client->readStringUntil('"');
  path.replace("\\/", "/");

  // Step 2: Open WebSocket connection and register event handler
  if (DEBUG_SERIAL_PRINT) {
    Serial.println("WebSocket Host=" + host + " Path=" + path);
  }
  webSocket.beginSSL(host, 443, path);
  webSocket.onEvent(webSocketEvent);
  return true;
  
}

unsigned long connectAndPing( unsigned long lastPing ) {
  
  /**
    Establishes a connection to slack and then pings every 5 seconds to keep the connection active
    Returns unsigned long lastPing
   */
  
  if (connected) {
    // Send ping every 5 seconds, to keep the connection alive
    if (millis() - lastPing > 5000) {
      sendPing();
      lastPing = millis();
    }
  } else {
    // Try to connect / reconnect to slack
    connected = connectToSlack();
    if (!connected) {
      delay(500);
    }
  }
  return lastPing;

}

void setup() {
  
  if (DEBUG_SERIAL_PRINT) {
    Serial.begin(115200);
    Serial.setDebugOutput(true);
  }

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(100);
  }
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
}

unsigned long lastPing = 0;

void loop() {
  
  webSocket.loop();
  lastPing = connectAndPing( lastPing );

}
