#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>

// --------- PINOS ----------
const int DHTPIN = 15;
const int LDR_PIN = 34;
const int PIR_PIN = 27;
const int BUZZER_PIN = 25;
const int LED_R = 12;
const int LED_G = 14;
const int LED_B = 13;

// --------- WIFI (Wokwi) ----------
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// --------- MQTT ----------
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const char* topic_pub = "revoo/workstation/esp32-01/telemetry";
const char* topic_cmd = "revoo/workstation/esp32-01/cmd";

WiFiClient espClient;
PubSubClient client(espClient);
DHTesp dht;

// Variáveis de Controle de Tempo
unsigned long lastMsgTime = 0;
const long interval = 2000; // Intervalo de envio (2 segundos)

// Sedentarismo
unsigned long sedentaryStartMillis = 0;
bool wasPresent = false;

void setLED(const String& color) {
  // Desliga todos primeiro
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);

  if (color == "red") digitalWrite(LED_R, HIGH);
  else if (color == "green") digitalWrite(LED_G, HIGH);
  else if (color == "blue") digitalWrite(LED_B, HIGH);
  else if (color == "yellow") {
    digitalWrite(LED_R, HIGH);
    digitalWrite(LED_G, HIGH);
  }
  else if (color == "off") {
    // Já estão desligados
  }
}

int readLuxRelativo() {
  int raw = analogRead(LDR_PIN);     
  // Ajuste conforme o seu circuito. No Wokwi padrão com Pull-down:
  // Mais luz = Maior tensão = Maior RAW. 
  // Se quiser inverter (0 = muita luz), use o map invertido:
  return map(raw, 0, 4095, 0, 1000); 
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  
  Serial.print("Mensagem recebida [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  // Controle simples via JSON string search
  if (msg.indexOf("\"buzzer\":1") >= 0) {
    Serial.println("Ativando Buzzer");
    tone(BUZZER_PIN, 2000, 300); // Toca por 300ms
  }

  if (msg.indexOf("\"led\":\"red\"") >= 0) setLED("red");
  else if (msg.indexOf("\"led\":\"green\"") >= 0) setLED("green");
  else if (msg.indexOf("\"led\":\"blue\"") >= 0) setLED("blue");
  else if (msg.indexOf("\"led\":\"yellow\"") >= 0) setLED("yellow");
  else if (msg.indexOf("\"led\":\"off\"") >= 0) setLED("off");
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password, 6);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop até reconectar
  while (!client.connected()) {
    Serial.print("Tentando conexao MQTT... ");
    
    // Cria um ID aleatório para evitar conflito no broker público
    String clientId = "ESP32Revoo-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("Conectado!");
      client.subscribe(topic_cmd);
    } else {
      Serial.print("Falha, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 2s");
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  dht.setup(DHTPIN, DHTesp::DHT22);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  sedentaryStartMillis = millis();
  setLED("green"); // Start com verde
}

void loop() {
  // Garante conexão WiFi e MQTT
  if (WiFi.status() != WL_CONNECTED) setup_wifi();
  if (!client.connected()) reconnect();
  
  // Mantém a comunicação MQTT ativa (recebe callbacks)
  client.loop();

  // Uso de Millis em vez de delay para não bloquear o processador
  unsigned long now = millis();
  
  if (now - lastMsgTime > interval) {
    lastMsgTime = now;

    TempAndHumidity th = dht.getTempAndHumidity();
    // Verifica se a leitura foi válida
    if (dht.getStatus() != DHTesp::ERROR_NONE) {
       Serial.println("Erro ao ler DHT!");
       return;
    }
    
    float t = th.temperature;
    float h = th.humidity;
    int lux = readLuxRelativo();
    bool presence = digitalRead(PIR_PIN);

    // Lógica de Sedentarismo
    if (presence && !wasPresent) {
      sedentaryStartMillis = now; // Reinicia contador ao chegar
      wasPresent = true;
    }
    if (!presence && wasPresent) {
      wasPresent = false; // Saiu da mesa
    }

    // Se está presente, calcula minutos. Se não, 0.
    int sedentaryMin = (wasPresent) ? (now - sedentaryStartMillis) / 60000 : 0;

    // Montagem do JSON
    String payload = "{";
    payload += "\"deviceId\":\"esp32-01\",";
    payload += "\"temp\":" + String(t, 1) + ",";
    payload += "\"hum\":" + String(h, 1) + ",";
    payload += "\"lux\":" + String(lux) + ",";
    payload += "\"presence\":" + String(presence ? "true" : "false") + ",";
    payload += "\"sedentaryMin\":" + String(sedentaryMin);
    payload += "}";

    Serial.print("Publicando: ");
    Serial.println(payload);
    
    client.publish(topic_pub, payload.c_str());
  }
}