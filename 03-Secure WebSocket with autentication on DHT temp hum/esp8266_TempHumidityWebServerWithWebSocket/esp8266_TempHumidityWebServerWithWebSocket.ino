/*
 *  WeMos D1 mini (esp8266)
 *  Simple web server that read from SPIFFS and
 *  stream on browser various type of file
 *  and manage gzip format also.
 *  Here I add a management of a token authentication
 *  with a custom login form, and relative logout.
 *  Add WebSocket data update with credentials management
 *
 * DHT12 library https://www.mischianti.org/2019/01/01/dht12-library-en/
 *
 * DHT12      ----- Esp8266
 * SDL        ----- D1
 * SDA        ----- D2
 *
 *  by Mischianti Renzo <https://www.mischianti.org>
 *
 *  https://www.mischianti.org/
 *
 */
#define ENABLE_WS_AUTHORIZATION

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <base64.h>


#include <FS.h>
#include "Hash.h"
#include <DHT12.h>
#include <ArduinoJson.h>

const char* ssid = "<YOUR-SSID>";
const char* password = "<YOUR-PASSWD>";

const char* www_username = "admin";
const char* www_password = "esp8266";
String ws_auth_code;

const uint8_t wsPort = 81;
int8_t connectionNumber = 0;
unsigned long messageInterval = 2000;

// Set dht12 i2c comunication on default Wire pin
DHT12 dht12;

ESP8266WebServer httpServer(80);
WebSocketsServer webSocket = WebSocketsServer(wsPort);

void serverRouting();

void setup(void) {
	Serial.begin(115200);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	Serial.println("");
	// Start sensor
	dht12.begin();

	// Wait for connection
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}

	Serial.println("");
	Serial.print("Connected to ");
	Serial.println(ssid);
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

	Serial.print(F("Inizializing FS..."));
	if (SPIFFS.begin()) {
		Serial.println(F("done."));
	} else {
		Serial.println(F("fail."));
	}

	Serial.println("Set routing for http server!");
	serverRouting();
	httpServer.begin();
	Serial.println("HTTP server started");

	Serial.print("Starting WebSocket..");

#ifdef ENABLE_WS_AUTHORIZATION
//  Simple way to invalidate session every restart
//	ws_auth_code = sha1(String(www_username) + ":" + String(www_password) + ":" + String(random(1000)))+":";
	ws_auth_code = sha1(String(www_username) + ":" + String(www_password))+":";

	Serial.println(base64::encode(ws_auth_code).c_str());

	webSocket.setAuthorization(base64::encode(ws_auth_code).c_str());
#endif

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
	Serial.println("OK");
}
unsigned long lastUpdate = millis()+messageInterval;

void loop(void) {
	httpServer.handleClient();
    webSocket.loop();

	if (connectionNumber>0 && lastUpdate+messageInterval<millis()){
		Serial.println("[WSc] SENT: Broadcast message!!");
    	const size_t capacity = 1024;
    	DynamicJsonDocument doc(capacity);

    	doc["humidity"] = dht12.readHumidity();
    	doc["temp"] = dht12.readTemperature();

    // If you don't have a DHT12 put only the library
    // comment upper line and decomment this line
    	doc["humidity"] = random(10,80);
    	doc["temp"] = random(1000,3500)/100.;

    	String buf;
    	serializeJson(doc, buf);

		webSocket.broadcastTXT(buf);
		lastUpdate = millis();
	}

}

String getContentType(String filename) {
	if (filename.endsWith(F(".htm"))) return F("text/html");
	else if (filename.endsWith(F(".html"))) return F("text/html");
	else if (filename.endsWith(F(".css"))) return F("text/css");
	else if (filename.endsWith(F(".js"))) return F("application/javascript");
	else if (filename.endsWith(F(".json"))) return F("application/json");
	else if (filename.endsWith(F(".png"))) return F("image/png");
	else if (filename.endsWith(F(".gif"))) return F("image/gif");
	else if (filename.endsWith(F(".jpg"))) return F("image/jpeg");
	else if (filename.endsWith(F(".jpeg"))) return F("image/jpeg");
	else if (filename.endsWith(F(".ico"))) return F("image/x-icon");
	else if (filename.endsWith(F(".xml"))) return F("text/xml");
	else if (filename.endsWith(F(".pdf"))) return F("application/x-pdf");
	else if (filename.endsWith(F(".zip"))) return F("application/x-zip");
	else if (filename.endsWith(F(".gz"))) return F("application/x-gzip");
	return F("text/plain");
}

bool handleFileRead(String path) {
	Serial.print(F("handleFileRead: "));
	Serial.println(path);

	if (!is_authenticated()) {
		Serial.println(F("Go on not login!"));
		path = "/login.html";
	} else {
		if (path.endsWith("/")) path += F("index.html"); // If a folder is requested, send the index file
	}
	String contentType = getContentType(path);             	// Get the MIME type
	String pathWithGz = path + F(".gz");
	if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
		if (SPIFFS.exists(pathWithGz)) // If there's a compressed version available
			path += F(".gz");                      // Use the compressed version
		fs::File file = SPIFFS.open(path, "r");                 // Open the file
		size_t sent = httpServer.streamFile(file, contentType); // Send it to the client
		file.close();                                    // Close the file again
		Serial.println(
				String(F("\tSent file: ")) + path + String(F(" of size "))
						+ sent);
		return true;
	}
	Serial.println(String(F("\tFile Not Found: ")) + path);
	return false;                     // If the file doesn't exist, return false
}

void handleNotFound() {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += httpServer.uri();
	message += "\nMethod: ";
	message += (httpServer.method() == HTTP_GET) ? "GET" : "POST";
	message += "\nArguments: ";
	message += httpServer.args();
	message += "\n";

	for (uint8_t i = 0; i < httpServer.args(); i++) {
		message += " " + httpServer.argName(i) + ": " + httpServer.arg(i)
				+ "\n";
	}

	httpServer.send(404, "text/plain", message);
}

void handleLogin() {
	Serial.println("Handle login");
	String msg;
	if (httpServer.hasHeader("Cookie")) {
		// Print cookies
		Serial.print("Found cookie: ");
		String cookie = httpServer.header("Cookie");
		Serial.println(cookie);
	}

	if (httpServer.hasArg("username") && httpServer.hasArg("password")) {
		Serial.print("Found parameter: ");

		if (httpServer.arg("username") == String(www_username) && httpServer.arg("password") == String(www_password)) {
			httpServer.sendHeader("Location", "/");
			httpServer.sendHeader("Cache-Control", "no-cache");

			String token = sha1(String(www_username) + ":" + String(www_password) + ":" + httpServer.client().remoteIP().toString());
			httpServer.sendHeader("Set-Cookie", "ESPSESSIONID=" + token);

#ifdef ENABLE_WS_AUTHORIZATION
			httpServer.sendHeader("Set-Cookie", "ESPWSSESSIONID=" + String(ws_auth_code));

			Serial.print("WS Token: ");
			Serial.println(ws_auth_code);
#endif

			httpServer.send(301);
			Serial.println("Log in Successful");
			return;
		}
		msg = "Wrong username/password! try again.";
		Serial.println("Log in Failed");
		httpServer.sendHeader("Location", "/login.html?msg=" + msg);
		httpServer.sendHeader("Cache-Control", "no-cache");
		httpServer.send(301);
		return;
	}
}

/**
 * Manage logout (simply remove correct token and redirect to login form)
 */
void handleLogout() {
	Serial.println("Disconnection");
	httpServer.sendHeader("Location", "/login.html?msg=User disconnected");
	httpServer.sendHeader("Cache-Control", "no-cache");
	httpServer.sendHeader("Set-Cookie", "ESPSESSIONID=0");
	httpServer.sendHeader("Set-Cookie", "ESPWSSESSIONID=0");
	httpServer.send(301);
	return;
}

/**
 * Retrieve temperature humidity realtime data
 */
void handleTemperatureHumidity(){
	const size_t capacity = 1024;
	DynamicJsonDocument doc(capacity);

	doc["humidity"] = dht12.readHumidity();
	doc["temp"] = dht12.readTemperature();

// If you don't have a DHT12 put only the library
// comment upper line and decomment this line
	doc["humidity"] = random(10,80);
	doc["temp"] = random(1000,3500)/100.;

	String buf;
	serializeJson(doc, buf);
	httpServer.send(200, F("application/json"), buf);

}

//Check if header is present and correct
bool is_authenticated() {
	Serial.println("Enter is_authenticated");
	if (httpServer.hasHeader("Cookie")) {
		Serial.print("Found cookie: ");
		String cookie = httpServer.header("Cookie");
		Serial.println(cookie);

		String token = sha1(String(www_username) + ":" +
				String(www_password) + ":" +
				httpServer.client().remoteIP().toString());
//	token = sha1(token);

		if (cookie.indexOf("ESPSESSIONID=" + token) != -1) {
			Serial.println("Authentication Successful");
			return true;
		}
	}
	Serial.println("Authentication Failed");
	return false;
}

void restEndPoint(){
	// External rest end point (out of authentication)
	httpServer.on("/login", HTTP_POST, handleLogin);
	httpServer.on("/logout", HTTP_GET, handleLogout);

	httpServer.on("/temperatureHumidity", HTTP_GET, handleTemperatureHumidity);
}

void serverRouting() {
	restEndPoint();

	// Manage Web Server
	Serial.println(F("Go on not found!"));
	httpServer.onNotFound([]() {               // If the client requests any URI
				Serial.println(F("On not found"));
				if (!handleFileRead(httpServer.uri())) { // send it if it exists
					handleNotFound();// otherwise, respond with a 404 (Not Found) error
				}
			});

	Serial.println(F("Set cache!"));
	// Serve a file with no cache so every tile It's downloaded
	httpServer.serveStatic("/configuration.json", SPIFFS, "/configuration.json", "no-cache, no-store, must-revalidate");
	// Server all other page with long cache so browser chaching they
	httpServer.serveStatic("/", SPIFFS, "/", "max-age=31536000");

	//here the list of headers to be recorded
	const char * headerkeys[] = { "User-Agent", "Cookie" };
	size_t headerkeyssize = sizeof(headerkeys) / sizeof(char*);
	//ask server to track these headers
	httpServer.collectHeaders(headerkeys, headerkeyssize);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {

    switch(type) {
        case WStype_DISCONNECTED:
        	Serial.printf("[%u] Disconnected!\n", num);
			webSocket.sendTXT(num, "{\"connected\":false}");
			connectionNumber = webSocket.connectedClients();

			Serial.print("Connected devices ");
			Serial.println(connectionNumber);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

				// send message to client
				webSocket.sendTXT(num, "{\"connected\":true}");
				connectionNumber = webSocket.connectedClients();

				Serial.print("Connected devices ");
				Serial.println(connectionNumber);
            }
            break;
        case WStype_TEXT:
        	Serial.printf("[%u] RECEIVE TXT: %s\n", num, payload);

            // send message to client
             webSocket.sendTXT(num, "(ECHO MESSAGE) "+String((char *)payload));

            // send data to all connected clients
            // webSocket.broadcastTXT("message here");
            break;
        case WStype_BIN:
        	Serial.printf("[%u] get binary length: %u\n", num, length);
            hexdump(payload, length);

            // send message to client
            // webSocket.sendBIN(num, payload, length);
            break;
    }
}
