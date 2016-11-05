#include <ESP8266WiFi.h>
#include <Servo.h>
#include <Thread.h>
#include <ThreadController.h>
#include <string.h>

// Wifi Parâmetros & Módulo ESP8266
const char* serverIp = "192.0.0.1";
const int serverPort = 8080;
const char* ssid = "service-set-identifier";  // SSID da rede Wifi
const char* password = "network-password";    // Senha da rede Wifi

// Servo Motor Parâmetros
Servo auxServo;
const int attachedPin = 9;   // Pino desejado ligado ao pino de dados do Servo

// Cache de Pedidos da Central
int pullObjs = 0;         // Contador de objetos a serem empurrados em sequência 
int delayToPull = 0;      // Tempo de espera até começar rejeição de objetos
time_t latestOrder = -1;  // Timestamp do último pedido enviado

// Ação Paralelizada Parâmetros
ThreadController parallelCtr;
Thread pullThread;

// Função para monitor momento de empurrar objetos
void pullAction() {
  unsigned long timeNow = millis();
  // Verifica se é chegado momento de empurrar objetos
    if(now() - (latestOrder + delayToPull)  >= 0 &&
                latestOrder != -1) {
      // Salva variáveis locais para impedir inconsistência
      int auxPullObjs = pullObjs, cachePullObjs;
      time_t cacheLatestOrder = latestOrder;
      // Empurra objetos um a um, de acordo com número de empurrados ordenado
      while(auxPullObjs > 0) {
        auxServo.write(90);   // Vira 90º
        delay(200);           // Aguarda 0,1 segundos
        auxServo.write(0);    // Vira servo de volta à posição central
        auxPullObjs--;
      }

      // Verifica não chegada de novo pedido enquanto empurrava objetos
      if(latestOrder == cacheLatestOrder) {
        // Nenhum novo pedido chegou, então limpa cache de pedidos da central
        pullObjs = 0;
        delayToPull = 0;
        latestOrder = -1;
      }
    }
}

void setup() {
  // put your setup code here, to run once:

  // Pino de Dados do Servo conectado ao pino attachedPin no Arduino
  auxServo.attach(attachedPin);
  
  // Conexão à rede WiFi
  WiFi.begin(ssid, password);  

  // Inicialização da Paralelização e Controle de Thread
  pullThread.setInterval(500);
  pullThread.onRun(pullAction);
  parallelCtr.add(&pullThread);
}

void loop() {
  // put your main code here, to run repeatedly:

  // Ativa Controle de Execução de Threads
  parallelCtr.run();
  
  // Se conectado ao WiFi
  if(WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      // Conecta-se ao cliente TCP (central)
      // Se conectado com sucesso
      if(client.connect(serverIP, serverPort)) {
        // Se não houve problema de conexão com o módulo central
        // Recebe mensagens do módulo central
        String msgStr = "";
        while(client.available()) {
          msgStr += client.readStringUntil('\r');
        }

        // Verifica se a mensagem recebida foi uma ORDEM
        if(msgStr.indexOf("ORDER") > 0) {
          // Atualiza timestamp do último pedido
          latestOrder = now();

          // Envia confirmação ACK ao módulo Central
          client.print("OK\r\n");
        
          unsigned long timeout = millis();
          // Verifica/Aguarda conexão por 400 ms
          while(client.available() == 0) {
            if(millis() - timeout > 400) {
              // Hora de quebrar ligação, tempo expirado
              client.stop();
              break;
            }
          }

          // Atualiza Valores de Ordens
          char *auxItem = strtok(msgStr, ":");
          int i = 0;
          while(auxItem != NULL) {
            if(i == 1)
              pullObjs = atoi(auxItem);
            else if(i == 2)
              delayToPull = atoi(auxItem);
            auxItem = strtok(NULL, ":");
            i++;
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
