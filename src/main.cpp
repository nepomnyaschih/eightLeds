//с пинами всё как в ардуино
#include <Arduino.h>
#include <WiFi.h>

//веб-сервер https://github.com/me-no-dev/ESPAsyncWebServer
//про вебсокеты https://ru.wikipedia.org/wiki/WebSocket
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

//для работы со встроенной файловой системой
//работа с файлами как в c++
#include <FS.h>
#include <SPIFFS.h>

#include <Debounce.h> //дребезг контактов кнопки
#include <ArduinoJson.h> //json-формат

AsyncWebServer server(80); //запуск веб-сервера на 80м порту
AsyncWebSocket ws("/ws"); //запуск веб-сокетов ws://<адрес>/ws
AsyncEventSource events("/events"); //обработчик событий

//конигурация вайфай
const char* ssid = "esp32test";
const char* password = "12345678";

//конфигурация пинов
#define LED_BUILTIN 2

#define LED0 0
#define LED1 4

#define BUTTON0 19
#define BUTTON1 18

//установка обработчика дребезга контактов
Debounce debouncer0 = Debounce( 20 , BUTTON0 );
Debounce debouncer1 = Debounce( 20 , BUTTON1 );

//умолчательное состояние лампочек
bool state0 = false;
bool state1 = false;

//удаление файла
void deleteFile(fs::FS &fs, const char * path) {
  if (fs.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

//отправка данных через вебсокет на !!!всех!!! клиентов
void sendDataWs()
{
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject(); //создание объекта json

    //установка значений в объект
    root["led0"] = state0;
    root["led1"] = state1;

    size_t len = root.measureLength(); //вычисление длины объекта
    AsyncWebSocketMessageBuffer * buffer = ws.makeBuffer(len); //  creates a buffer (len + 1) for you.
    if (buffer) {
        root.printTo((char *)buffer->get(), len + 1); //json в буфер
        ws.textAll(buffer); //отправка буффера
    }
}


//обработка запросов по вебсокет
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
  if(type == WS_EVT_CONNECT){
    sendDataWs(); //отправка данных подключившемуся клиенту

  } else if(type == WS_EVT_DISCONNECT){ //обработка разрыва соеденения
    Serial.printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){ //обработка ошибок
    Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){ //обработка ответа на client->ping()
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");

  } else if(type == WS_EVT_DATA){ //обработка данных пришедших через вебсокет
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data

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

      //если в сообщении пришло "0", меняем состояние LED0
      if (msg=="0") {
        Serial.printf("change state for led0\n");
        state0 = !state0; //смена состояние
        digitalWrite(LED0, state0); //включение/выключение LED0
        sendDataWs(); //отправка данных о состоянии всем клиентам
      }

      //если в сообщении пришло "1", меняем состояние LED1
      //всё по аналогии LED0
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
  //устанавливаем назначение пинов
  pinMode(LED0, OUTPUT);
  pinMode(LED1, OUTPUT);

  pinMode(BUTTON0, INPUT);
  pinMode(BUTTON1, INPUT);

  //запуск serial, что-бы читать отладочные сообщения
  Serial.begin(115200);
  Serial.println();
  Serial.print("Start...");

  //запуск wifi в режиме точки доступа
  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();

  Serial.print("AP IP address: ");
  Serial.println(myIP);

  //запускаем файловую систему
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  //запуск обработчиков событий вебсокетов
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  //обработка GET запроса на удаление index.html
  server.on("/delete_index", HTTP_GET, [](AsyncWebServerRequest *request){
    deleteFile(SPIFFS, "/index.html"); //удаление
    request->send(200, "text/plain", "index.html deleted"); //отправка клиенту сообщения об успешности операции
  });

  //обработка GET запроса sys, в ответе форма для загрузки index.html
  server.on("/sys", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", "<form enctype='multipart/form-data' method='POST'><input type='file' name='fileToUpload' id='fileToUpload'><input type='submit' name='submit'></form><a href='http://192.168.4.1/delete_index'>DELETE index.html</a>");
  });

  //обработка всех остальных запросов
  server.onNotFound([](AsyncWebServerRequest *request){
      request->send(404); //page not found message for client
  });

  //обработчик загрузки файлов
  server.onFileUpload([](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final){

    if(!index){
      Serial.printf("UploadStart: %s\n", filename.c_str());
    }

    //создание и запись в файл во внутреней памяти
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

  //не понял зачем это нужно, но без этого не работает.
  events.onConnect([](AsyncEventSourceClient *client){
    client->send("hello!",NULL,millis(),1000);
  });

  //запуск вебсервера
   server.begin();


   //установка index.html умолчательным файлом для сервера
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");;
}

void loop()
{

  //обработка нажатия кнопки
  if ( debouncer0.update () && debouncer0.read()==1) {
    state0 = !state0;
    digitalWrite(LED0, state0);
    sendDataWs(); //отправка данных всем клиентам
  }

  if ( debouncer1.update () && debouncer1.read()==1) {
    state1 = !state1;
    digitalWrite(LED1, state1);
    sendDataWs();
  }

}
