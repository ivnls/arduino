#include <WiFi.h>
#include <WebServer.h>
#include <Ticker.h>
#include <math.h>
#include "driver/mcpwm.h"

//wifi login
const char* ssid = ""; //SSID
const char* password = ""; //pass

//termistor
const float R25 = 10000.0; //resistencia em 25 graus
const float T25 = 298.15;  //temp em kelvin 25 graus
const float Beta = 3950.0; //beta do termistor
const float resistor = 10000.0; //valor da resistencia do resistor


float temperaturaC = 0.0; //Essa temperatura vai se exibida no html


//porta do servidor html
WebServer server(80);

bool automaticRPM = true; //controle automatico do rpm

//pinos gpio
const int fanPin = 16;   //pwm
const int tachPin = 17;  //tacometro
const int pinThermistor = 34; //termistor

//variaveis do fan
volatile int rpm = 0;
volatile int tachCounter = 0;
unsigned long prevMillis = 0;

Ticker ticker;

//atualizações do tacometro
void IRAM_ATTR tachCounterISR() {
  tachCounter++;
}

void setup() {
  Serial.begin(115200);
  pinMode(tachPin, INPUT_PULLUP);

  //configuração do pwm usando o mcpwm
  Serial.println("Configurando MCPWM...");
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, fanPin);  //gpio
  mcpwm_config_t pwmConfig;
  pwmConfig.frequency = 25000;         //frequencia de 25KHz
  pwmConfig.cmpr_a = 0;                //duty cicle 0%
  pwmConfig.cmpr_b = 0;                //xnão usar, remover depois
  pwmConfig.counter_mode = MCPWM_UP_COUNTER;
  pwmConfig.duty_mode = MCPWM_DUTY_MODE_0;
  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwmConfig);

  //wifi configuração
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi conectado");

  //inicia servidor e rotas
  server.on("/", handleRoot); //rota padrão
  server.on("/set_speed", handleSetSpeed); //definir velocidade rpm
  server.on("/toggle_auto", handleToggleAuto); //modo automático ou manual
  server.begin();
  Serial.println("Servidor HTTP iniciado");

  //interrupção no pino do tacometro
  attachInterrupt(digitalPinToInterrupt(tachPin), tachCounterISR, FALLING);

  //calcular rpm a cada segundo
  ticker.attach(1, calculateRPM);
}

void loop() {
  server.handleClient();

  getTemp();

  if (automaticRPM) {
    setAutomaticRPM();
  }
}

//página inicial em html
void handleRoot() {
  String html = "<html><body><h1>Controlar Ventilador</h1>";
  html += "<input type='range' id='speedSlider' min='0' max='255' value='" + String(rpm) + "'>";
  html += "<button onclick='sendSpeed()'>Enviar Velocidade</button>";
  html += "<h2>Velocidade do Ventilador: " + String(rpm) + " RPM</h2>";
  html += "<h2>Temperatura: " + String(temperaturaC) + " ºC</h2>";
  html += "<label for='autoSwitch'>Controle Automático: </label>";
  html += "<input type='checkbox' id='autoSwitch' onclick='toggleAuto()' " + String(automaticRPM ? "checked" : "") + ">";
  html += "<script>";
  html += "function sendSpeed() {";
  html += "  var speed = document.getElementById('speedSlider').value;";
  html += "  fetch('/set_speed?value=' + speed);";
  html += "  setTimeout(() => { location.reload(); }, 1000);"; // Atualiza a página para mostrar a nova velocidade
  html += "}";
  html += "function toggleAuto() {";
  html += "  fetch('/toggle_auto');";
  html += "  setTimeout(() => { location.reload(); }, 1000);"; // Atualiza a página para refletir a mudança
  html += "}";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

//rota definir a velocidade do fan
void handleSetSpeed() {
  if (server.hasArg("value")) {
    int speed = server.arg("value").toInt();
    setFanSpeed(speed);
    automaticRPM = false; // Desativa o controle automático ao ajustar manualmente
  }
  server.send(200, "text/plain", "Velocidade ajustada");
}

//rota alternar o controle automático
void handleToggleAuto() {
  automaticRPM = !automaticRPM;
  server.send(200, "text/plain", "Controle automático " + String(automaticRPM ? "ativado" : "desativado"));
}

// calcular rpm
void calculateRPM() {
  unsigned long currentMillis = millis();
  rpm = (tachCounter * 60) / 2;
  tachCounter = 0;
  prevMillis = currentMillis;
}

//duty cycle do fan
void setFanSpeed(int speed) {
  float dutyCycle = (speed / 255.0) * 100.0; //velocidade (0-255) para duty cycle em %
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, dutyCycle);
}

//obter a temperatura do termistor
void getTemp() {
  int adcValue = analogRead(pinThermistor);
  float voltage = adcValue * 3.3 / 4095.0;
  float resistance = (resistor * (3.3 - voltage)) / voltage;
  float tempK = 1.0 / ((log(resistance / R25) / Beta) + (1.0 / T25));
  temperaturaC = tempK - 273.15;
  delay(1000);
}

//ajustar automaticamente a velocidade do fan com base na temperatura
void setAutomaticRPM() {
  int speed = 0;
  if (temperaturaC < 20) {
    speed = 0;
  } else if (temperaturaC < 30) {
    speed = 85;
  } else if (temperaturaC < 38) {
    speed = 147;
  } else if (temperaturaC < 45) {
    speed = 200;
  } else if (temperaturaC >= 50) {
    speed = 255;
  }
  setFanSpeed(speed);
}
