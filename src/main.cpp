#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include <FS.h>
#include <SPIFFS.h>

#include <Debounce.h>
#include <ArduinoJson.h>


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

void deleteFile(fs::FS &fs, const char * path) {
  // Serial.printf("Deleting file: %s\n", path);
  if (fs.remove(path)) {
    // Serial.println("File deleted");
  } else {
    // Serial.println("Delete failed");
  }
}

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

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
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
  pinMode(LED0, OUTPUT);
  pinMode(LED1, OUTPUT);

  pinMode(BUTTON0, INPUT);
  pinMode(BUTTON1, INPUT);

  Serial.begin(115200);
  Serial.println();
  Serial.print("Start...");

  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/delete_index", HTTP_GET, [](AsyncWebServerRequest *request){
    deleteFile(SPIFFS, "/index.html");
    request->send(200, "text/plain", "index.html deleted");
  });

  server.on("/sys", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<form enctype='multipart/form-data' method='POST'><input type='file' name='fileToUpload' id='fileToUpload'><input type='submit' name='submit'></form><a href='http://192.168.4.1/delete_index'>DELETE index.html</a>");
  });

  server.onNotFound([](AsyncWebServerRequest *request){
      request->send(404);
  });

  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){

    if(!index){
        Serial.printf("UploadStart: %s\n", filename.c_str());
    }

    File f = SPIFFS.open("/index.html", "a+");
    for(size_t i=0; i<len; i++){
      f.write(data[i]);
    }

    f.seek(0);
    f.close();

    if(final){
      Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index+len);
    }
  });

  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });

 server.begin();

  digitalWrite(LED_BUILTIN, HIGH);
  delay(3000);
  digitalWrite(LED_BUILTIN, LOW);

  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");;
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
