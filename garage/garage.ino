#include <SPI.h>
#include <WiFiNINA.h>

// defines SSID and PASS
#include "secrets.h"

#define GARAGE_PIN 5

WiFiServer server(80);
int Idx;
unsigned long NextWiFiCheck;
uint_fast8_t ErrCount;

void setup() {
	
	Serial.begin(9600);
	Serial.println("Trying connect");
	
	WiFi.setHostname("garaguino");
	WiFi.config({192,168,1,206});
	
	while (WiFi.begin(SSID, PASS) != WL_CONNECTED) {
		delay(1000);
	}
	
	WiFi.lowPowerMode();
	
	NextWiFiCheck = millis() + 10000;
	
	Serial.println(WiFi.localIP());
	
	server.begin();
}

void (*SystemReset)() = 0;

void loop() {
	char buf[2048];
	int r;
	
	WiFiClient client = server.available();
	
	if (client) {
		
		r = client.read((uint8_t *)buf, sizeof(buf));
		
		Serial.write(buf, r);
		Serial.println("");
		
		memset(buf, 0, sizeof(buf));
		
		strcpy(buf, "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\nOk\n");
		client.write((uint8_t *)buf, strlen(buf));
		
		client.stop();
		
		// actively having a connection is a pretty solid indicator the wifi
		// is still working.
		NextWiFiCheck = millis() + 10000;
		ErrCount = 0;
		
	} else {
		if (millis() > NextWiFiCheck) {
			if (WiFi.status() != WL_CONNECTED) {
				Serial.println("Wifi connection error");
				delay(100 * (1 << ErrCount));
				// exponential backoff, for good measure.
				if (++ErrCount > 5) {
					// failed for a good chunk of time. kill switch.
					SystemReset();
				}
				return;
			} else {
				ErrCount = 0;
				NextWiFiCheck = millis() + 10000;
			}
		}
		delay(100);
	}
}