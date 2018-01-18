#include <Arduino.h>
#include <IPAddress.h>

#include <ESP8266WiFiAP.h>
#include <WiFiClientSecure.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiType.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiServer.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiGeneric.h>

#include <EEPROM.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>


const uint8_t modeAP = 0;
const uint8_t modeSTA = 1;

const unsigned int receiverKhz = 36;    // TSOP4836 als Receiver (36kHz Bandpass)

// Konfigurationsspeicher im EEPROM
const uint32_t configMagic = 0x54FE93B0;

struct ConfigData
{
  uint32_t magic;
  uint8_t mode;
  char ssid[32];
  char password[64];
};

ConfigData config;

// Geöffneter Port zur Kommunikation mit der App
WiFiUDP udpPort;

// Instanzen der IR-Bibliothek
IRsend irsend(4);   // IR Sende-LED ist an Pin 4 (D2 @ NodeMCU) angeschlossen
IRrecv irrecv(14);  // IR Empfänger ist an Pin 14 (D5 @ NodeMCU) angeschlossen

bool ledStatus = true;


char readByte() {   // Analog zu Serial.read() welches wegen Timeout nicht richtig funktioniert
  while (Serial.available() == 0)
  {
    delay(1);
  }
  return Serial.read();
}

void readLine(char *array, size_t length)   // Analog zu Serial.readBytesUntil() welches ebenfalls nicht funktioniert
{
  size_t bytesRead = 0;
  do
  {
    const char input = readByte();
    if (input == 10 || input == 13)
    {
      if (bytesRead > 0)
      {
        break;
      }
    }
    else
    {
      array[bytesRead] = input;
      bytesRead++;
    }
  } while (bytesRead < length);
  array[bytesRead] = 0;
}

void setup() {
  // LED konfigurieren
  pinMode(D4, OUTPUT);
  digitalWrite(D4, true);
  
  // Seriellen Port initialisieren
  Serial.begin(115200, SERIAL_8N1);
  
  // EEPROM initialisieren
  delay(20);
  EEPROM.begin(512);
  delay(20);

  // Infrarotbibliothek initialisieren
  irsend.begin();
  
  // Kurz warten, damit die Konsole geöffnet werden kann
  delay(1000);

  // Startmeldung anzeigen
  Serial.println("ESPmote v1.1");
  Serial.println();

  // Prüfen, ob WLAN-Daten hinterlegt sind
  memset(&config, 0, sizeof(config));
  EEPROM.get(0, config);
  if (config.magic == configMagic)
  {
    Serial.println("Konfigurationsdaten gefunden. Drücken Sie innerhalb der nächsten 2 Sekunden eine beliebige Taste, um diese zu löschen");
    delay(2000);
    if (Serial.read() > 0)
    {
      config.magic = 0;
      Serial.println("WLAN-Konfiguration wird gelöscht...");
    }
  }

  // Konfiguration durchführen
  if (config.magic != configMagic)
  {
    Serial.println("WLAN-Konfiguration nicht vorhanden. Bitte geben Sie nun die WLAN-Daten ein.");
    Serial.println("Betriebsmodus: 0 - Access Point 1 - Station");
    Serial.flush();
    config.mode = (readByte() == '1') ? modeSTA : modeAP;
    Serial.println("SSID:");
    readLine(config.ssid, sizeof(config.ssid));
    Serial.println("Passwort:");
    readLine(config.password, sizeof(config.password));
    Serial.print("Konfiguration abgeschlossen. Modus: ");
    Serial.print((config.mode == 0) ? "AP" : "STA");
    Serial.print(", SSID: ");
    Serial.print(config.ssid);
    Serial.print(", Passwort: ");
    Serial.println(config.password);
  }

  // WLAN starten
  if (config.mode == modeAP)
  {
    WiFi.mode(WIFI_AP);
    IPAddress apIP(192, 168, 1, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(config.ssid);
    Serial.println("SoftAP gestartet");
  }
  else
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);
  
    Serial.print("Versuche zu verbinden... ");
    while (WiFi.status() != WL_CONNECTED)
    {
      ledStatus = !ledStatus;
      digitalWrite(D4, ledStatus);
      delay(500);
    }
    Serial.print("Erledigt! Eigene IP-Adresse ist ");
    Serial.println(WiFi.localIP());
  }

  // Konfiguration speichern sofern erfolgreich
  if (config.magic != configMagic)
  {
    config.magic = configMagic;
    EEPROM.put(0, config);
    EEPROM.commit();
    Serial.println("Konfiguration im EEPROM hinterlegt!");
  }
  Serial.println();

  // LED einschalten
  digitalWrite(D4, false);

  // UDP-Port öffnen
  udpPort.begin(8765);
}

void loop() {
  int packetSize = udpPort.parsePacket();
  if (packetSize > 0)
  {
    // Paket einlesen
    char packet[512];
    int len = udpPort.read(packet, 512);
    if (len > 0)
    {
      packet[len] = 0;
    }
    //Serial.printf("Received %d bytes from %s, port %d\n", packetSize, udpPort.remoteIP().toString().c_str(), udpPort.remotePort());
    //Serial.printf("UDP packet contents: %s\n", packet);

    // 3 Befehle werden unterstützt:
    // 1: HELO (Check, ob Gerät erreichbar ist)
    // 2: RECV (IR-code einlesen und code zurücksenden)
    // 3: SEND <code> (IR-code senden)
    if (len >= 4)
    {
      // HELO
      if (packet[0] == 'H' && packet[1] == 'E' && packet[2] == 'L' && packet[3] == 'O')
      {
        udpPort.beginPacket(udpPort.remoteIP(), udpPort.remotePort());
        udpPort.write("HELO\n");
        udpPort.endPacket();
      }
      // RECV
      else if (packet[0] == 'R' && packet[1] == 'E' && packet[2] == 'C' && packet[3] == 'V')
      {
        // Empfänger aktivieren
        irrecv.enableIRIn();

        // Code einlesen
        String result;
        decode_results results;
        while (true)
        {
          if (irrecv.decode(&results))
          {
            if (results.overflow)
            {
              Serial.println("Überlauf beim Empfangen!");
              continue;
            }
            else
            {
              for (uint16_t i = 1; i < results.rawlen; i++)
              {
                uint32_t usecs;
                for (usecs = results.rawbuf[i] * RAWTICK; usecs > UINT16_MAX; usecs -= UINT16_MAX)
                {
                  result += uint64ToString(UINT16_MAX);
                  result += " 0 ";
                }
                result += uint64ToString(usecs, 10);
                if (i < results.rawlen - 1)
                {
                  result += " ";  // ' ' not needed on the last one
                }
              }
            }
  
            Serial.print("Fragmente eines IR-Codes empfangen: ");
            Serial.println(result);
  
            udpPort.beginPacket(udpPort.remoteIP(), udpPort.remotePort());
            udpPort.write(result.c_str());
            udpPort.write("\n");
            udpPort.endPacket();

            break;
          }
          
          yield();
        }

        // Empfänger wieder deaktivieren
        irrecv.disableIRIn();
      }
      // SEND <CODE>
      else if (packet[0] == 'S' && packet[1] == 'E' && packet[2] == 'N' && packet[3] == 'D')
      {
        // Code in int-array umwandeln
        size_t numData = 0;
        uint16_t rawData[128];
        for (size_t i = 5; i < packetSize - 1; i++)
        {
          if (i == 5)
          {
            rawData[0] = atoi(&packet[5]);
            numData++;
          }
          else if (packet[i] == ' ')
          {
            rawData[numData] = atoi(&packet[i + 1]);
            numData++;
          }
        }

        Serial.print("Sende ");
        Serial.print(numData);
        Serial.print(" Fragmente an Rohdaten: ");
        for (size_t i = 0; i < numData; i++)
        {
          Serial.print(rawData[i]);
          Serial.print(" ");
        }
        Serial.println();

        // Danach per IRsend senden
        irsend.sendRaw(rawData, numData, receiverKhz);
      }
    }
  }
}
