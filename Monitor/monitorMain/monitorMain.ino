#include <ESP8266WiFi.h>
#include <HX711.h>
#include <Time.h>

// Wifi Parâmetros & Módulo ESP8266
const char* serverIp = "192.0.0.1";
const int serverPort = 8080;
const char* ssid = "service-set-identifier";  // SSID da rede Wifi
const char* password = "network-password";    // Senha da rede Wifi

// Detecção de Presença & Sensor de Infravermelho
int extraLedPin = 8;    // Pino onde o LED extra de feedback está conectado
int infraPin = 7;       // Pino onde a saída do sensor óptico está conectado
int detectedObject = 1; // Estado de presença de objeto (0 - detectado, 1 - não detectado)

// Sensor de Peso & Leitura da Carga
const int dOut = A3;    // Pino DT
const int hxSCK = A4;   // Pino SCK
float objWeight = 0;

// Cache de Leituras de objetos
int notSent = 0;        // Contador de msgs não enviadas à central sobre pacotes monitorados 
time_t latestObj = -1;  // Timestamp do último objeto monitorado

void setup() {
  // put your setup code here, to run once:

  // Inicialização do sensor de infravermelho e LED extra
  pinMode(extraLedPin, OUTPUT);   // Definição do pino de LED como saída
  pinMode(infraPin, INPUT);       // Definição do pino do sensor como entrada

  // Inicialização do sensor de peso
  pinMode(hxSCK, OUTPUT);
  pinMode(dOut, INPUT);
  digitalWrite(hxSCK, HIGH);      // Reset do módulo HX711
  delay(200);
  digitalWrite(hxSCK, LOW);

  // Conexão à rede WiFi
  WiFi.begin(ssid, password);  
}

// Leitura do peso
void readWeight(){ 
  unsigned long weight = 0;
  
  digitalWrite(hxSCK, LOW);
  
  while (digitalRead(dOut) == 1) ;
  
  for (int i=0; i < 24; i++){
    digitalWrite(hxSCK, HIGH);
    weight = weight << 1;
    digitalWrite(hxSCK, LOW);
    
    if(digitalRead(dOut) == 1) weight++;
  }
  
  digitalWrite(hxSCK, HIGH);
  digitalWrite(hxSCK, LOW);
  
  weight ^= 0x00800000;
  objWeight = weight;
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(extraLedPin, LOW);         // Apaga Led extra de presença de objeto
  objWeight = 0;                          // Limpa valor de leitura de peso para objeto

  // Verifica Sensor Presença
  detectedObject = digitalRead(infraPin); // Atualiza estado de presença de objeto
  // Se dectectado um objeto
  if(detectedObject == LOW) {
    // Acende Led extra de presença de objeto
    digitalWrite(extraLedPin, HIGH);      
    
    // Atualiza última ocorrência de objeto monitorado
    latestObj = now();
    
    // Verifica e Salva Peso
    readWeight();

    notSent++;
  }

  // Se conectado ao WiFi
  if(WiFi.status() == WL_CONNECTED) {
      // Cria um cliente TCP
      WiFiClient client;
      // Conecta-se ao cliente TCP
      // Se conectado com sucesso
      if(client.connect(serverIP, serverPort)) {
        // Envia msg com informações do pacote à central
        client.print(latestObj + ":" 
                    + weight + ":" 
                    + (notSent-1));
        
        unsigned long timeout = millis();
        bool timeoutClient = false;
        // Verifica/Aguarda conexão por 400 ms
        while(client.available() == 0 &&
              timeoutClient == false) {
          if(millis() - timeout > 400) {
            // Hora de quebrar ligação, tempo expirado
            client.stop();
            timeoutClient = true;
          }
        }

        // Se não houve problema de conexão com o módulo central
        if(!timeoutClient) {
          // Prepara resposta de feedback do módulo central
          String ackStr = "";
          while(client.available()) {
            ackStr += client.readStringUntil('\r');
          }

          // Verifica se a resposta foi uma confirmação
          if(ackStr.indexOf("OK") > 0) {
            // Confirmação recebida
            notSent--;
          }
        }

        // Interrompe conexão com módulo central
        client.stop();
        
      } else {
        // Não foi possível se conectar ao módulo Central
      }
  } else {
    // Se não conectado ao WiFi
    WiFi.begin(ssid, password);  
  }
  
  delay(500);
}
