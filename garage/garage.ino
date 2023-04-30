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
	
	WiFi.setHostname("garaguino");
	WiFi.config({192,168,1,206});
	
	while (WiFi.begin(SSID, PASS) != WL_CONNECTED) {
		digitalWrite(13, HIGH);
		delay(1000);
		digitalWrite(13, LOW);
	}
	
	WiFi.lowPowerMode();
	
	NextWiFiCheck = millis() + 10000;
	
	
	server.begin();
	
	pinMode(GARAGE_PIN, INPUT_PULLUP);
	
	srand(millis());
}

void (*SystemReset)() = 0;

void loop() {
	char buf[2048];
	int r;
	const char *p;
	
	WiFiClient client = server.available();
	
	if (client) {
		
		r = client.read((uint8_t *)buf, sizeof(buf));
		
		memset(buf, 0, sizeof(buf));
		
		p = (rand() % 2) ? /* digitalRead(GARAGE_PIN) ? */ "Switch is open" : "Switch is closed";
		
		r = snprintf(buf, sizeof(buf),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/plain\r\n"
			"Server: Garaguino\r\n"
			"Content-Length: %d\r\n\r\n"
			"%s\r\n",
			strlen(p) + 2, p);
		client.write((uint8_t *)buf, strlen(buf));
		
		client.stop();
		
		// actively having a connection is a pretty solid indicator the wifi
		// is still working.
		NextWiFiCheck = millis() + 10000;
		ErrCount = 0;
		
	} else {
		if (millis() > NextWiFiCheck) {
			if (WiFi.status() != WL_CONNECTED) {
				digitalWrite(13, HIGH);
				delay(100 * (1 << ErrCount));
				digitalWrite(13, LOW);
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
		delay(50);
	}
}