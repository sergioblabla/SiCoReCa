#include <WiShield.h>
#include <WiServer.h>
#include <Thread.h>
#include <ThreadController.h>
#include <Arduino.h>
#include <Time.h>
#include <string.h>

#define maxWeight 0.1           // Máximo Peso por pacote permitido
#define delayProPacket 5        // Tempo que um pacote leva pra percorrer caminho do módulo monitor até o atuador
#define delaySinceMonitored 0.5 // Tempo do momento em que o pacote é monitorado até que a msg sobre ele chega ao módulo central

#define WIRELESS_MODE_INFRA	1
#define WIRELESS_MODE_ADHOC	2

// Server & Internet Parâmetros
unsigned char local_ip[] = {192,168,1,2};	// Endereço IP para o shield/módulo
unsigned char gateway_ip[] = {192,168,0,1};	// router or gateway IP address
unsigned char subnet_mask[] = {255,255,255,0};	// subnet mask for the local network
const prog_char ssid[] PROGMEM = {"Casa Blabla"};		// max 32 bytes

unsigned char security_type = 3;	// 0 - open; 1 - WEP; 2 - WPA; 3 - WPA2

// WPA/WPA2 passphrase
const prog_char security_passphrase[] PROGMEM = {"999999999"};	// max 64 characters

// WEP 128-bit keys
// sample HEX keys
prog_uchar wep_keys[] PROGMEM = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,	// Key 0
				  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	// Key 1
				  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	// Key 2
				  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	// Key 3
				};

// setup the wireless mode
// infrastructure - connect to AP
// adhoc - connect to another WiFi device
unsigned char wireless_mode = WIRELESS_MODE_INFRA;

unsigned char ssid_len;
unsigned char security_passphrase_len;

//int state = WL_IDLE_STATUS;               // Auxiliar para estado da rede
// Fim dos parâmetros WiFi da rede ----------------------------------------

// Cache de Msgs de Monitor
int nMaxMsgs = 10;            // Limite máximo de Msgs no Cache Central
float weight[10];       // Cache de Pesos dos Objetos
time_t latestMsgs[10];  // Cache de Timestamps das últimas Msgs
time_t latestTimeStamp = -1;  // Último Timestamp tomado como base
int nextDelay = 0;            // Delay para enviar próxima ordem
int nPullOrder = 0;           // Número de Objetos a empurrar em sequência

/*
// Servo Motor Parâmetros
Servo auxServo;
const int attachedPin = 9;   // Pino desejado ligado ao pino de dados do Servo

// Cache de Pedidos da Central
int pullObjs = 0;         // Contador de objetos a serem empurrados em sequência 
int delayToPull = 0;      // Tempo de espera até começar rejeição de objetos
time_t latestOrder = -1;  // Timestamp do último pedido enviado
*/

// Ação Paralelizada Parâmetros
ThreadController parallelCtr;
Thread decisionThread;

// Função para monitor momento de empurrar objetos
void calculateOrders() {
  // Calcula pacotes acima do peso
  // Verifica se não há ordens acumuladas
  if(nextDelay == 0 && nPullOrder == 0) {
    // Toma decisão sobre ordem de pacotes a serem empurrados
    // Isso se baseia na informação mais antiga válida de pacote armazenada no cache
    // O pacote acima de peso com informação mais antiga deverá ser empurrado para fora da esteira
    // Os pacotes em sequência à eles que também estão acima do peso também
    for(int i = nMaxMsgs - 1; i >= 0; i--) {
      // Verifica no cache se o pacote i mais antigo tem peso acima do permitido e é válido
      if(latestMsgs[i] != -1 &&
          weight[i] >= maxWeight &&
          latestMsgs[i] != latestTimeStamp) {
            nPullOrder++;
            // O tempo de espera para o momento de início de empurrar na ordem
            // somente é calculado uma vez, considerando o momento que o pacote
            // mais antigo chegará no módulo atuador
            if(nPullOrder == 1) {
              nextDelay = delayProPacket
                  - (delaySinceMonitored 
                      + (now() - latestMsgs[i]));
              // Atualiza momento base de referência do pacote inicial na ordem a ser empurrado
              latestTimeStamp = latestMsgs[i];
            }
          } else {
            // Se o pacote i mais próximo antigo tem peso permitido ou não é válido
            // Encerra automaticamente o loop de decisão da ordem
            if(nPullOrder > 0)
              i = nMaxMsgs;
          }
    }
  }
}

// Função para Limpar Cache Local
void initializeCache() {
  // Inicializa valores na Cache
  for(int i = 0; i < nMaxMsgs; i++) {
    weight[i] = 0;
    latestMsgs[i] = -1;
  }
  
  latestTimeStamp = -1;
  nextDelay = 0;
}

// Função para Adicionar Msg à Cache Local
// Mantendo organização da ordem:
// Msg mais recente está na primeira posição do array
// Em que seu Timestamp tem valor diferente de -1 
void addMsgCache(char *auxStr) {
  // Calcula os espaços livres na Cache
  int freePlaces = nMaxMsgs;
  for(int i = 0; i < nMaxMsgs; i++) {
    if(weight[i] != 0 || latestMsgs[i] != -1)
      freePlaces--;
  }

  // Variáveis locais auxiliares a serem extraídas da msg
  char *auxItem = strtok(auxStr, ":");
  int i = 0;
  float auxWeight = 0;
  time_t auxTimestamp = -1;
  while(auxItem != NULL) {
    if(i == 1)
      auxTimestamp = atoi(auxItem);
    else if(i == 2)
      auxWeight = atof(auxItem);
    auxItem = strtok(NULL, ":");
    i++;
  }
    
  bool error = true;
  // Verifica se há espaços livres na Cache
  if(freePlaces > 0) {
    // Atualiza Msgs na Cache de Acordo com espaço vazio
    // Da última posição à primeira
    for(i = nMaxMsgs-1; i >= 0; i--) {
      if(weight[i] == 0 && latestMsgs[i] == -1){
        weight[i] = auxWeight;
        latestMsgs[i] = auxTimestamp;
        error = false;
      }
    }
  }

  // Caso não haja espaços livres
  if(freePlaces < 1 || error) {
    // Atualiza Msgs na Cache de Acordo com Maior Timestamp
    // Realoca todos os Timestamps do mais antigo para mais recente
    // Ou seja, a última posição será removida e todo mundo deslocado para trás no array
    for(i = nMaxMsgs-1; i >= 0; i--) {
      if(i == nMaxMsgs-2) {
        // Última posição será limpada
        weight[i] = 0;
        latestMsgs[i] = -1;
      } else {
        // Outras posições serão deslocadas
        weight[i+1] = weight[i];
        latestMsgs[i+1] = latestMsgs[i];
      }
    }

    // Salva última mensagem na primeira posição
    weight[0] = auxWeight;
    latestMsgs[0] = auxTimestamp; 
  }
}

boolean serverCallback(char* str) {
    Serial.print(str);
    // Check if the requested URL matches "/"
    /*WiServer.print("hey\n");
    if (strcmp(URL, "/") == 0) {
        // Use WiServer's print and println functions to write out the page content
        //WiServer.print("<html>");
        WiServer.print("hghghjhgjhgjhg7878h87h8h8777h8hh87");
        //WiServer.print("</html>");
        
        // URL was recognized
        return true;
    }
    // URL not found
    return false;*/
    String msgStr = str;
    // Verifica se é uma msg de nó Monitor
    if(msgStr.indexOf("MONITOR") > 0) {
      // Atualiza o Cache com a msg recebida de Monitor
      addMsgCache(str);
        
      // Envia confirmação ACK ao módulo Monitor
      WiServer.print("OK\r\n");
      
      unsigned long timeout = millis();
      // Verifica/Aguarda conexão por 400 ms
      /*while(client.available() == 0) {
        if(millis() - timeout > 400) {
          // Hora de quebrar ligação, tempo expirado
          break;
        }
      }*/
        
      return true;
      //client.stop();    // Interrompe conexão com cliente
    } else {
    //if(msgStr.indexOf("/") > 0) {
      //Serial.print("2");
      // Verifica se há Cache de Ordem no momento
      //WiServer.print("ORDER:" + String(nPullOrder) + ":" + String(nextDelay) + "\r\n");
      if(nPullOrder != 0 && nextDelay != 0) {
        // Envia Ordem à Atuador
        WiServer.print("ORDER:" + String(nPullOrder) + ":" + String(nextDelay) + "\r\n");
        //WiServer.print("ORDER:" + /*String(nPullOrder)*/"" + ":" + /*String(nextDelay)*/"" + "\r\n");
        /*server.write("ORDER:" +
                      + nPullOrder + ":" +
                      + nextDelay + "\r\n");*/

        /*unsigned long timeout = millis();
        bool timeoutClient = false;*/
        // Verifica/Aguarda conexão por 400 ms
        /*while(client.available() == 0 &&
                timeoutClient == false) {
          if(millis() - timeout > 400) {
            // Hora de quebrar ligação, tempo expirado
            client.stop();
            timeoutClient = true;
          }
        }*/
      
        // Se não houve problema de conexão com o módulo central
        /*if(!timeoutClient) {
          // Prepara resposta de feedback do módulo central
          String ackStr = "";
          while(client.available()) {
            ackStr += client.readStringUntil('\r');
          }

          // Verifica se a resposta foi uma confirmação
          if(ackStr.indexOf("OK") > 0) {
            // Confirmação recebida
            // Então Limpa Cache de Decisão
            nPullOrder = nextDelay = 0;    
            latestTimeStamp = -1;
          }
        }*/
      
        //client.stop();    // Interrompe conexão com cliente
      } else {
        WiServer.print("Nothing to Show\r\n");
      }
      
      return true;
    }
    
    /*WiServer.print("hey\n");
    if (strcmp(URL, "/") == 0) {
        // Use WiServer's print and println functions to write out the page content
        //WiServer.print("<html>");
        WiServer.print("hghghjhgjhgjhg7878h87h8h8777h8hh87");
        //WiServer.print("</html>");
        
        // URL was recognized
        return true;
    }*/
    // URL not found
    return false;
}

void setup() {
  // put your setup code here, to run once:
/*
  // Pino de Dados do Servo conectado ao pino attachedPin no Arduino
  auxServo.attach(attachedPin);*/

  // Inicializa valores na Cache
  initializeCache();

  // Inicializa Serial
  Serial.begin(57600);
  
  // Verifica presença do shield WiFi
  /*if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("Shield WiFi não presente);
    while(true);
  }
  
  // Configura rede WiFi
  WiFi.config(myIp);
  
  // Inicia rede WiFi
  while(status != WL_CONNECTED) {
    status = WiFi.begin(mySSID, myPASS);
  }
  server.begin();*/
  WiServer.init(serverCallback);
  WiServer.enableVerboseMode(true);
  
  // Inicialização da Paralelização e Controle de Thread
  decisionThread.setInterval(1000);
  decisionThread.onRun(calculateOrders);
  parallelCtr.add(&decisionThread);
}

void loop() {
  // put your main code here, to run repeatedly:

  // Ativa Controle de Execução de Threads
  parallelCtr.run();
  WiServer.server_task();

  //delay(500);
}
