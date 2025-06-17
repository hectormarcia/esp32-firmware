#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Wi-Fi credentials
const char* ssid = "Net-HGSDConsulting";
const char* password = "64730053812020";

// Pin definitions
#define DHTPIN 42
#define DHTTYPE DHT11
#define LED_PIN 21
#define ONE_WIRE_BUS 2  // DS18B20 data pin

// DHT11
DHT dht(DHTPIN, DHTTYPE);

// DS18B20
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

// Web server and WebSocket
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Auto control flag
bool autoControlEnabled = false;

// Sensor read functions
String readDHTTemperature() {
  float t = dht.readTemperature();
  if (isnan(t)) return "--";
  return String(t);
}

String readDHTHumidity() {
  float h = dht.readHumidity();
  if (isnan(h)) return "--";
  return String(h);
}

String readDS18B20Temp() {
  ds18b20.requestTemperatures();
  float tempC = ds18b20.getTempCByIndex(0);
  if (tempC == DEVICE_DISCONNECTED) return "--";
  return String(tempC);
}

// Notify LED and Auto state to all clients
void notifyClients() {
  String state = digitalRead(LED_PIN) ? "ON" : "OFF";
  String autoState = autoControlEnabled ? "ON" : "OFF";
  ws.textAll("{\"led\":\"" + state + "\",\"auto\":\"" + autoState + "\"}");
}

// Handle incoming WebSocket messages
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    String msg = (char*)data;
    if (msg == "toggle") {
      if (!autoControlEnabled) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        notifyClients();
      }
    } else if (msg == "auto") {
      autoControlEnabled = !autoControlEnabled;
      controlLEDByWaterTemp();  // Run immediately based on temp
      notifyClients();          // Sync UI
    }
  }
}

// WebSocket events
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    notifyClients();
  } else if (type == WS_EVT_DATA) {
    handleWebSocketMessage(arg, data, len);
  }
}

// Automatically control LED based on water temperature
void controlLEDByWaterTemp() {
  if (!autoControlEnabled) return;

  ds18b20.requestTemperatures();
  float tempC = ds18b20.getTempCByIndex(0);

  if (tempC == DEVICE_DISCONNECTED) {
    Serial.println("DS18B20 disconnected");
    return;
  }

  bool previous = digitalRead(LED_PIN);
  if (tempC > 21.0) {
    digitalWrite(LED_PIN, HIGH);
  } else {
    digitalWrite(LED_PIN, LOW);
  }

  if (digitalRead(LED_PIN) != previous) {
    notifyClients();  // Update UI only if LED state changed
  }

  Serial.print("Water Temp: ");
  Serial.print(tempC);
  Serial.print(" °C | LED State: ");
  Serial.println(digitalRead(LED_PIN) ? "ON" : "OFF");
}

// HTML Page (embedded)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  html { font-family: Arial; text-align: center; }
  h2 { font-size: 2.5rem; }
  p { font-size: 1.8rem; }
  .units { font-size: 1.2rem; }
  .dht-labels { font-size: 1.5rem; padding-bottom: 10px; }
  button {
    font-size: 1.5rem; padding: 10px 20px; margin-top: 10px;
    border: none; background-color: #059e8a; color: white;
    border-radius: 5px; cursor: pointer;
  }
  button:hover { background-color: #037c6b; }
</style>
</head><body>
  <h2>Cultivando Futuro</h2><h2>Panel de Control</h2>
  <p>
    <span style='font-size:40px;'>&#128201;</span>
    <span class="dht-labels">Temperatura Ambiental</span>
    <span id="temperature">--</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>
    <span style='font-size:40px;'>&#128202;</span>
    <span class="dht-labels">Humedad</span>
    <span id="humidity">--</span>
    <sup class="units">&percnt;</sup>
  </p>
  <p>
    <span style='font-size:40px;color:blue;'>&#127777;</span>
    <span class="dht-labels">Temp. Agua</span>
    <span id="waterTemp">--</span> <sup class="units">&deg;C</sup>
  </p>
  <p>
    <span style='font-size:40px;'>&#9889;</span>
    <span class="dht-labels">LED State:</span>
    <span id="ledState">...</span><br>
    <button onclick="toggleLED()">Toggle LED</button><br><br>
    <button onclick="toggleAuto()">Auto LED (Water Temp)</button>
    <p><strong>Auto Mode:</strong> <span id="autoState">OFF</span></p>
  </p>
<script>
let socket = new WebSocket('ws://' + window.location.hostname + '/ws');

socket.onmessage = function(event) {
  let data = JSON.parse(event.data);
  if (data.led !== undefined) document.getElementById('ledState').innerHTML = data.led;
  if (data.temperature !== undefined) document.getElementById('temperature').innerHTML = data.temperature;
  if (data.humidity !== undefined) document.getElementById('humidity').innerHTML = data.humidity;
  if (data.waterTemp !== undefined) document.getElementById('waterTemp').innerHTML = data.waterTemp;
  if (data.auto !== undefined) document.getElementById('autoState').innerHTML = data.auto;
};

function toggleLED() {
  socket.send("toggle");
}

function toggleAuto() {
  socket.send("auto");
}
</script>
</body></html>
)rawliteral";

// Setup
void setup() {
  Serial.begin(115200);
  dht.begin();
  ds18b20.begin();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }
  Serial.println(WiFi.localIP());

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
  server.begin();
}

// Loop: broadcast sensor data
unsigned long lastUpdate = 0;
const unsigned long INTERVAL = 10000;

void loop() {
  ws.cleanupClients();
  
  controlLEDByWaterTemp();

  if (millis() - lastUpdate > INTERVAL) {
    lastUpdate = millis();

    String temperature = readDHTTemperature();
    String humidity = readDHTHumidity();
    String waterTemp = readDS18B20Temp();

    String json = "{\"temperature\":\"" + temperature + "\","
                  "\"humidity\":\"" + humidity + "\","
                  "\"waterTemp\":\"" + waterTemp + "\"}";
    ws.textAll(json);
  }
}
