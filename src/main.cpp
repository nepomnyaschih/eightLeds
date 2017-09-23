#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include <FS.h>

#include <Debounce.h>
#include <ArduinoJson.h>


// SKETCH BEGIN
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");

const char* ssid = "esp32test";
const char* password = "12345678";

#define LED_BUILTIN 2

#define LED0 0
#define LED1 4

#define BUTTON0 19
#define BUTTON1 18

Debounce debouncer0 = Debounce( 20 , BUTTON0 );
Debounce debouncer1 = Debounce( 20 , BUTTON1 );

bool state0 = false;
bool state1 = false;

void sendDataWs()
{
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();
    root["led0"] = state0;
    root["led1"] = state1;
    size_t len = root.measureLength();
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
    if (buffer) {
        root.printTo((char *)buffer->get(), len + 1);
        ws.textAll(buffer);
    }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){

Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());

  if(type == WS_EVT_CONNECT){
    Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
    sendDataWs();
    // client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      // Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }

      if (msg=="0") {
        Serial.printf("change state for led0\n");
        state0 = !state0;
        digitalWrite(LED0, state0);
        sendDataWs();
      }

      if (msg=="1") {
        Serial.printf("change state for led1\n");
        state1 = !state1;
        digitalWrite(LED1, state1);
        sendDataWs();
      }
    }

  }

}

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.print("Start...");

  WiFi.softAP(ssid, password);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);


  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED0, OUTPUT);
  pinMode(LED1, OUTPUT);

  pinMode(BUTTON0, INPUT);
  pinMode(BUTTON1, INPUT);

ws.onEvent(onWsEvent);
server.addHandler(&ws);

server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
  request->send(200, "text/plain", String(ESP.getFreeHeap()));
});

// server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
//   request->send(SPIFFS, "/index.htm");
// });

server.onNotFound([](AsyncWebServerRequest *request){
  Serial.printf("NOT_FOUND: ");
  if(request->method() == HTTP_GET){
    Serial.printf("GET");
  }
  request->send(404);
});

events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it gat is: %u\n", client->lastId());
    }
    //send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!",NULL,millis(),1000);
  });

 server.begin();

//server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");

  digitalWrite(LED_BUILTIN, HIGH);
  delay(3000);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.print("Started!");
}

void loop()
{

  if ( debouncer0.update () && debouncer0.read()==1) {
    state0 = !state0;
    digitalWrite(LED0, state0);
    sendDataWs();
  }

  if ( debouncer1.update () && debouncer1.read()==1) {
    state1 = !state1;
    digitalWrite(LED1, state1);
    sendDataWs();
  }

}
