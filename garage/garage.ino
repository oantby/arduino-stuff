#include <SPI.h>
#include <WiFiNINA.h>

// defines SSID + PASS + GARAGE_IP + LOG_ENABLE
#include "secrets.h"

#define STATUS_PIN 5
#define ACTION_PIN 8

// multicast address/port where we send logs when enabled
#define LOG_DEST {239, 255, 17, 73}, 1234

#ifndef DOOR_MOVE_TIME
#define DOOR_MOVE_TIME 10
#endif

WiFiServer server(80);
#ifdef LOG_ENABLE
WiFiUDP Udp;
#endif
int Idx;
unsigned long NextWiFiCheck;
uint_fast8_t ErrCount;
char MsgOut[80];

struct Route {
	const char path[80];
	int (*func)(WiFiClient *);
};

unsigned long LastToggle = 0;

enum GarageState {
	opened = 1,
	closed = 2,
	opening = 3,
	closing = 4
};

GarageState CurState;

int garageStat(WiFiClient *client);
int openGarage(WiFiClient *client);
int closeGarage(WiFiClient *client);
int toggleGarage(WiFiClient *client);

struct Route Routes[] = {
	{"/status", garageStat},
	{"/open", openGarage},
	{"/close", closeGarage},
	{"/toggle", toggleGarage}
};

const char *curStateString() {
	switch (CurState) {
		case opened : return "open";
		case closed : return "closed";
		case opening : return "opening";
		case closing : return "closing";
	}
	return "unknown";
}

// returns CurState - updates it if needed.
int garageStat(WiFiClient *client) {
	if ((CurState == opening || CurState == closing) && millis() < LastToggle + (DOOR_MOVE_TIME * 1000)) {
		// want to acknowledge "moving" status until we've reached DOOR_MOVE_TIME.
		return CurState;
	}
	return CurState = (digitalRead(STATUS_PIN) == HIGH ? opened : closed);
}

int openGarage(WiFiClient *client) {
	int s = garageStat(client);
	if (s == closed) {
		return toggleGarage(client);
	}
	strcpy(MsgOut, "Garage not closed");
	return 0;
}

int closeGarage(WiFiClient *client) {
	int s = garageStat(client);
	if (s == opened) {
		return toggleGarage(client);
	}
	strcpy(MsgOut, "Garage not open");
	return 0;
}

int toggleGarage(WiFiClient *client) {
	int stat = garageStat(client);
	strcpy(MsgOut, ((stat == opened || stat == opening) ? "Closing garage" : "Opening Garage"));
	CurState = (stat == opened || stat == opening) ? closing : opening;
	digitalWrite(ACTION_PIN, HIGH);
	delay(250);
	digitalWrite(ACTION_PIN, LOW);
	LastToggle = millis();
	return 0;
}

void setup() {
	
	WiFi.setHostname("garaguino");
	#ifdef GARAGE_IP
	WiFi.config(GARAGE_IP);
	// otherwise DHCP is used.
	#endif
	
	while (WiFi.begin(SSID, PASS) != WL_CONNECTED) {
		digitalWrite(13, HIGH);
		delay(1000);
		digitalWrite(13, LOW);
	}
	
	WiFi.lowPowerMode();
	
	NextWiFiCheck = millis() + 10000;
	
	#ifdef LOG_ENABLE
		Udp.begin(8000);
	#endif
	
	server.begin();
	
	pinMode(STATUS_PIN, INPUT_PULLUP);
	pinMode(ACTION_PIN, OUTPUT);
	
	// seed with noise, as long as A0 isn't connected to anything.
	randomSeed(analogRead(0));
	
	CurState = opened;
	garageStat(NULL); // will force update CurState.
}

void (*SystemReset)() = 0;

void loop() {
	#ifdef LOG_ENABLE
	char logBuf[2048];
	size_t logPos = 0;
	#endif
	char buf[2048];
	int r, i, stat;
	const char *p, *method;
	char *ptr, *ptr2;
	int (*func)(WiFiClient *) = NULL;
	
	WiFiClient client = server.available();
	MsgOut[0] = '\0';
	
	if (client) {
		
		r = client.read((uint8_t *)buf, sizeof(buf));
		
		#ifdef LOG_ENABLE
			auto ip = client.remoteIP();
			logPos = sprintf(logBuf, " %d.%d.%d.%d:%d ",
				ip[0], ip[1], ip[2], ip[3], client.remotePort());
		#endif
		
		// make buf just our first line.
		strtok(buf, "\r\n");
		
		if ((strncmp(buf, "POST ", 5) != 0 || ((method = "POST") && false))
			&& (strncmp(buf, "GET ", 4) != 0 || ((method = "GET") && false))) {
			// invalid request method.
			#ifdef LOG_ENABLE
				Udp.beginPacket(LOG_DEST);
				logPos += sprintf(logBuf + logPos, "- Invalid request - method not GET or POST");
				Udp.write(logBuf, logPos);
				Udp.endPacket();
			#endif
			
			client.write(buf, sprintf(buf,
				"HTTP/1.1 400 Bad Request\r\n"
				"Content-Type: application/json\r\n"
				"Server: Garaguino\r\n"
				"Content-Length: 65\r\n\r\n"
				"{\"message\": \"Request method invalid or unknown\","
				" \"status\": 400}\r\n"));
		} else {
			
			// find the requested path.
			for (ptr2 = ptr = strchr(buf, ' ') + 1; !isWhitespace(*ptr); ++ptr) {}
			*ptr = '\0';
			
			for (i = 0; i < sizeof(Routes) / sizeof(*Routes); i++) {
				if (strcmp(ptr2, Routes[i].path) == 0) {
					func = Routes[i].func;
					break;
				}
			}
			
			if (func) {
				stat = 200;
				p = NULL;
				ptr2 = strdup(ptr2);
				if (func != garageStat && (r = func(&client))) {
					// requested function failed.
					stat = 500;
					
					r = sprintf(buf,
						"HTTP/1.1 200 OK\r\n"
						"Content-Type: application/json\r\n"
						"Server: Garaguino\r\n"
						"Content-Length: 58\r\n\r\n"
						R"({"status": 500, "message": "An internal error occurred"})"
						"\r\n");
					
					client.write((uint8_t *)buf, r);
				} else {
					// get current status, and spit back response.
					garageStat(&client);
					p = curStateString();
					
					r = sprintf(buf,
						"HTTP/1.1 200 OK\r\n"
						"Content-Type: application/json\r\n"
						"Server: Garaguino\r\n"
						"Content-Length: %d\r\n\r\n"
						R"({"status": 200, "garage state": "%s", "message": "%s"})"
						"\r\n",
						52 + strlen(p) + strlen(MsgOut),
						p, MsgOut);
					
					client.write((uint8_t *)buf, r);
				}
				#ifdef LOG_ENABLE
					Udp.beginPacket(LOG_DEST);
					logPos += sprintf(logBuf + logPos, "%s %s %d (%s)", method, ptr2, stat, p);
					Udp.write(logBuf, logPos);
					Udp.endPacket();
				#endif
				free(ptr2);
			} else {
				// 404
				#ifdef LOG_ENABLE
					Udp.beginPacket(LOG_DEST);
					logPos += sprintf(logBuf + logPos, "%s %s 404", method, ptr2);
					Udp.write(logBuf, logPos);
					Udp.endPacket();
				#endif
				
				ptr2 = strdup(ptr2);
				
				r = sprintf(buf,
					"HTTP/1.1 404 Not Found\r\n"
					"Content-Type: application/json\r\n"
					"Server: Garaguino\r\n"
					"Content-Length: %d\r\n\r\n"
					R"({"status": 404, "message": "Requested path %s not found"})"
					"\r\n", strlen(ptr2) + 57, ptr2);
				
				free(ptr2);
				client.write((uint8_t *)buf, r);
			}
			
		}
		
		
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
	
	// update curState - even if everything SHOULD get a new update when needed.
	if (CurState == opening || CurState == closing
		&& millis() > LastToggle + (DOOR_MOVE_TIME * 1000)) {
		
		garageStat(NULL);
	}
}