// 09. SoftAP + web server — wiki "WiFiAccessPoint" example adapted for XIAO ESP32S3
// Board becomes a hotspot; browse to http://192.168.4.1 and click links to
// toggle the user LED (GPIO21, inverted).
#include <WiFi.h>
#include <NetworkClient.h>
#include <WiFiAP.h>

const char *ssid = "XIAO_ESP32S3_AP";
const char *password = "12345678";
const int LED = LED_BUILTIN;

NetworkServer server(80);

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);  // off
  delay(2000);

  Serial.println("Configuring access point...");
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("softAP failed!");
    while (true) delay(1000);
  }
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());
  server.begin();
  Serial.println("Server started. Connect to the AP and open http://192.168.4.1");
}

void loop() {
  NetworkClient client = server.accept();
  if (!client) return;

  Serial.println("client connected");
  String currentLine = "";
  while (client.connected()) {
    if (!client.available()) continue;
    char c = client.read();
    if (c == '\n') {
      if (currentLine.length() == 0) {
        client.println("HTTP/1.1 200 OK");
        client.println("Content-type:text/html");
        client.println();
        client.print("<h1>XIAO ESP32S3</h1>");
        client.print("<p><a href=\"/H\">LED ON</a></p>");
        client.print("<p><a href=\"/L\">LED OFF</a></p>");
        client.println();
        break;
      }
      currentLine = "";
    } else if (c != '\r') {
      currentLine += c;
    }
    if (currentLine.endsWith("GET /H")) {
      digitalWrite(LED, LOW);   // on (inverted)
      Serial.println("LED ON");
    }
    if (currentLine.endsWith("GET /L")) {
      digitalWrite(LED, HIGH);  // off
      Serial.println("LED OFF");
    }
  }
  client.stop();
  Serial.println("client disconnected");
}
