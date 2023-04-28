#include <SPI.h>
#include <WiFiNINA.h>

// defines SSID and PASS
#include "secrets.h"

WiFiServer server(80);
int Idx;

void setup() {
	
	Serial.begin(9600);
	Serial.println("Trying connect");
	while (WiFi.begin(SSID, PASS) != WL_CONNECTED) {
		delay(1000);
	}
	
	Serial.println(WiFi.localIP());
	Serial.println("Ping result options:");
	Serial.println(">= 0 is ms rtt");
	Serial.println(WL_PING_DEST_UNREACHABLE);
	Serial.println(WL_PING_TIMEOUT);
	Serial.println(WL_PING_UNKNOWN_HOST);
	Serial.println(WL_PING_ERROR);
	Serial.println("");
	
	server.begin();
}

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
	} else {
		if (++Idx % 100 == 0) Serial.println("No client there");
		delay(100);
	}
}