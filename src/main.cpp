#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h> // needed by NTPClient.h
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h> // API Doc: https://pubsubclient.knolleary.net/api.html
#include <ArduinoJson.h>  // API Doc: https://arduinojson.org/v6/doc/
#include <EEPROM.h>
#include "TinyIRSender.hpp"
#include "settings.h"

// ++++++++++++++++++++++++++++++++++++++++
//
// CONSTANTS
//
// ++++++++++++++++++++++++++++++++++++++++

// Constants - Misc
const char FIRMWARE_VERSION[] = "1.0";
const char COMPILE_DATE[] = __DATE__ " " __TIME__;
const int CURRENT_CONFIG_VERSION = 1;
const int HTTP_PORT = 80;

// Constants - HW pins
const int HWPIN_IR_LED = D5;
// const int HWPIN_PUSHBUTTON = D2;
// const int HWPIN_LED = D4;

// Constants - Intervals (all in ms)
const int LED_MQTT_MIN_TIME = 500;
const int LED_WEB_MIN_TIME = 500;
const int TIME_BUTTON_LONGPRESS = 10000;
const int MQTT_RECONNECT_INTERVAL = 2000;

// Constants - MQTT
const char MQTT_SUBSCRIBE_CMD_TOPIC1[] = "%scmd";                // Subscribe patter without hostname
const char MQTT_SUBSCRIBE_CMD_TOPIC2[] = "%s%s/cmd";             // Subscribe patter with hostname
const char MQTT_PUBLISH_STATUS_TOPIC[] = "%s%s/status";          // Public pattern for status (normal and LWT) with hostname
const char MQTT_LWT_MESSAGE[] = "{\"bridge\":\"disconnected\"}"; // LWT message

// Constants - NTP
const char NTP_SERVER[] = "europe.pool.ntp.org";
const long NTP_TIME_OFFSET = 0;                  // in s
const unsigned long NTP_UPDATE_INTERVAL = 60000; // in ms

// Constants - Serial
const int HWSERIAL_BAUD = 9600;

// ++++++++++++++++++++++++++++++++++++++++
//
// ENUMS
//
// ++++++++++++++++++++++++++++++++++++++++

enum class LedColor
{
  BLACK,
  RED,
  GREEN,
  BLUE,
  WHITE,
};

// ++++++++++++++++++++++++++++++++++++++++
//
// LIBS
//
// ++++++++++++++++++++++++++++++++++++++++

// Webserver
ESP8266WebServer server(HTTP_PORT);

// Wifi Client
WiFiClient espClient;

// MQTT Client
PubSubClient client(espClient);

// OTA Updater
ESP8266HTTPUpdateServer httpUpdater;

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, NTP_TIME_OFFSET, NTP_UPDATE_INTERVAL);

// LED RGBW
// Adafruit_NeoPixel led(1, HWPIN_LED, NEO_GRB + NEO_KHZ800);

// ++++++++++++++++++++++++++++++++++++++++
//
// VARS
//
// ++++++++++++++++++++++++++++++++++++++++

// Buffers
String html;
char buff[255];

// Config
uint16_t cfgStart = 0;        // Start address in EEPROM for structure 'cfg'
configData_t cfg;             // Instance 'cfg' is a global variable with 'configData_t' structure now
bool configIsDefault = false; // true if no valid config found in eeprom and defaults settings loaded

// Runtime default config values
int ledBrightness = PWMRANGE;

// Variables will change
bool ledOneToggle = false;
bool ledTwoToggle = false;
uint32_t ledOneLastColor = 0;
uint32_t ledTwoLastColor = 0;
char mqtt_prefix[50];
unsigned long lastDevicePollTime = 0;       // will store last beamer state time
unsigned long lastPublishTime = 0;          // will store last publish time
unsigned long ledOneTime = 0;               // will store last time LED was updated
unsigned long ledTwoTime = 0;               // will store last time LED was updated
unsigned long mqttLastReconnectAttempt = 0; // will store last time reconnect to mqtt broker
bool previousButtonState = 1;               // will store last Button state. 1 = unpressed, 0 = pressed
unsigned long buttonTimer = 0;              // will store how long button was pressed

void HTMLHeader(const char section[], unsigned int refresh = 0, const char url[] = "/");

// ++++++++++++++++++++++++++++++++++++++++
//
// MAIN CODE
//
// ++++++++++++++++++++++++++++++++++++++++

void HTMLHeader(const char *section, unsigned int refresh, const char *url)
{

  char title[50];
  char hostname[50];
  WiFi.hostname().toCharArray(hostname, 50);
  snprintf(title, 50, "IRBridge@%s - %s", hostname, section);

  html = "<!DOCTYPE html>";
  html += "<html>\n";
  html += "<head>\n";
  html += "<meta name='viewport' content='width=600' />\n";
  if (refresh != 0)
  {
    html += "<META http-equiv='refresh' content='";
    html += refresh;
    html += ";URL=";
    html += url;
    html += "'>\n";
  }
  html += "<title>";
  html += title;
  html += "</title>\n";
  html += "<style>\n";
  html += "body {\n";
  html += " background-color: #EDEDED;\n";
  html += " font-family: Arial, Helvetica, Sans-Serif;\n";
  html += " Color: #333;\n";
  html += "}\n";
  html += "\n";
  html += "h1 {\n";
  html += "  background-color: #333;\n";
  html += "  display: table-cell;\n";
  html += "  margin: 20px;\n";
  html += "  padding: 20px;\n";
  html += "  color: white;\n";
  html += "  border-radius: 10px 10px 0 0;\n";
  html += "  font-size: 20px;\n";
  html += "}\n";
  html += "\n";
  html += "ul {\n";
  html += "  list-style-type: none;\n";
  html += "  margin: 0;\n";
  html += "  padding: 0;\n";
  html += "  overflow: hidden;\n";
  html += "  background-color: #333;\n";
  html += "  border-radius: 0 10px 10px 10px;";
  html += "}\n";
  html += "\n";
  html += "li {\n";
  html += "  float: left;\n";
  html += "}\n";
  html += "\n";
  html += "li a {\n";
  html += "  display: block;\n";
  html += "  color: #FFF;\n";
  html += "  text-align: center;\n";
  html += "  padding: 16px;\n";
  html += "  text-decoration: none;\n";
  html += "}\n";
  html += "\n";
  html += "li a:hover {\n";
  html += "  background-color: #111;\n";
  html += "}\n";
  html += "\n";
  html += "#main {\n";
  html += "  padding: 20px;\n";
  html += "  background-color: #FFF;\n";
  html += "  border-radius: 10px;\n";
  html += "  margin: 10px 0;\n";
  html += "}\n";
  html += "\n";
  html += "#footer {\n";
  html += "  border-radius: 10px;\n";
  html += "  background-color: #333;\n";
  html += "  padding: 10px;\n";
  html += "  color: #FFF;\n";
  html += "  font-size: 12px;\n";
  html += "  text-align: center;\n";
  html += "}\n";

  html += "table  {\n";
  html += "border-spacing: 0;\n";
  html += "}\n";

  html += "table td, table th {\n";
  html += "padding: 5px;\n";
  html += "}\n";

  html += "table tr:nth-child(even) {\n";
  html += "background: #EDEDED;\n";
  html += "}";

  html += "input[type=\"submit\"] {\n";
  html += "background-color: #333;\n";
  html += "border: none;\n";
  html += "color: white;\n";
  html += "padding: 5px 25px;\n";
  html += "text-align: center;\n";
  html += "text-decoration: none;\n";
  html += "display: inline-block;\n";
  html += "font-size: 16px;\n";
  html += "margin: 4px 2px;\n";
  html += "cursor: pointer;\n";
  html += "}\n";

  html += "input[type=\"submit\"]:hover {\n";
  html += "background-color:#4e4e4e;\n";
  html += "}\n";

  html += "input[type=\"submit\"]:disabled {\n";
  html += "opacity: 0.6;\n";
  html += "cursor: not-allowed;\n";
  html += "}\n";

  html += "</style>\n";
  html += "</head>\n";
  html += "<body>\n";
  html += "<h1>";
  html += title;
  html += "</h1>\n";
  html += "<ul>\n";
  html += "<li><a href='/'>Home</a></li>\n";
  html += "<li><a href='/send'>Send</a></li>\n";
  html += "<li><a href='/settings'>Settings</a></li>\n";
  html += "<li><a href='/wifiscan'>WiFi Scan</a></li>\n";
  html += "<li><a href='/fwupdate'>FW Update</a></li>\n";
  html += "<li><a href='/reboot'>Reboot</a></li>\n";
  html += "</ul>\n";
  html += "<div id='main'>";
}

void HTMLFooter()
{
  html += "</div>";
  html += "<div id='footer'>&copy; 2022 Fabian Otto - Firmware v";
  html += FIRMWARE_VERSION;
  html += " - Compiled at ";
  html += COMPILE_DATE;
  html += "</div>\n";
  html += "</body>\n";
  html += "</html>\n";
}

void setLed(LedColor color)
{
  // led.clear();
  // led.setBrightness(cfg.led_brightness);

  // switch (color)
  // {
  // case LedColor::BLACK:
  //   led.setPixelColor(0, led.Color(0, 0, 0));
  //   break;
  // case LedColor::RED:
  //   led.setPixelColor(0, led.Color(255, 0, 0));
  //   break;
  // case LedColor::GREEN:
  //   led.setPixelColor(0, led.Color(0, 255, 0));
  //   break;
  // case LedColor::BLUE:
  //   led.setPixelColor(0, led.Color(0, 0, 255));
  //   break;
  // case LedColor::WHITE:
  //   led.setPixelColor(0, led.Color(255, 255, 255));
  //   break;
  // default:
  //   led.setPixelColor(0, led.Color(0, 0, 0));
  //   break;
  // }

  // led.show();
}

long dBm2Quality(long dBm)
{
  if (dBm <= -100)
    return 0;
  else if (dBm >= -50)
    return 100;
  else
    return 2 * (dBm + 100);
}

void showMQTTAction()
{
  // ledTwoLastColor = led.getPixelColor(0);
  // setLed(LedColor::BLACK);
  // ledTwoTime = millis();
}

void showWEBAction()
{
  // ledOneLastColor = led.getPixelColor(0);
  // setLed(LedColor::BLUE);
  // ledOneTime = millis();
}

void saveConfig()
{
  EEPROM.begin(512);
  EEPROM.put(cfgStart, cfg);
  delay(200);
  EEPROM.commit(); // Only needed for ESP8266 to get data written
  EEPROM.end();
}

void eraseConfig()
{
  Serial.print(F("Erase EEPROM config..."));
  EEPROM.begin(512);
  for (uint16_t i = cfgStart; i < sizeof(cfg); i++)
  {
    EEPROM.write(i, 0);
    // Serial.printf_P(PSTR("Block %i of %i\n"), i, sizeof(cfg));
  }
  delay(200);
  EEPROM.commit();
  EEPROM.end();
  Serial.print(F("done\n"));
}

void toHex(char *buffer, uint32_t *value)
{
  char *pEnd;
  uint32_t temp;
  // Serial.printf("Buffer: %s\n", buffer);
  temp = (uint32_t)strtoull(buffer, &pEnd, 16);
  // Serial.printf("Temp: %x\n", temp);
  // Serial.printf("Temp: %u\n", temp);
  *value = temp;
}

void sendIR(uint8_t sAddress, uint8_t sCommand, uint_fast8_t sRepeats)
{
  Serial.printf("Sending IR\nadr: 0x%02x cmd: 0x%02x rpt:%d\n", sAddress, sCommand, sRepeats);

  sendNEC(HWPIN_IR_LED, sAddress, sCommand, sRepeats);
}

void handleSend()
{
  showWEBAction();

  String value;
  char buffer[100];

  if (!server.authenticate(cfg.admin_username, cfg.admin_password))
  {
    return server.requestAuthentication();
  }
  else
  {

    if (server.method() == HTTP_POST)
    {
      uint32_t hexaddress;
      uint32_t hexcommand;
      uint8_t repeats = 0;

      for (uint8_t i = 0; i < server.args(); i++)
      {
        if (server.argName(i) == "address")
        {
          value = server.arg(i);
          value.toCharArray(buffer, sizeof(buffer));
          toHex(buffer, &hexaddress);
        }
        if (server.argName(i) == "command")
        {
          value = server.arg(i);
          value.toCharArray(buffer, sizeof(buffer));
          toHex(buffer, &hexcommand);
        }
        if (server.argName(i) == "repeats")
        {
          value = server.arg(i);
          repeats = value.toInt();
        }
      }

      if (hexaddress != 0 && hexcommand != 0)
      {
        sendIR(hexaddress, hexcommand, repeats);
      }
    }
  }

  HTMLHeader("Send");
  html += "<form method='POST' action='/send'>";
  html += "<input type='input' name='address' placeholder='address'><br />";
  html += "<input type='input' name='command' placeholder='command'><br />";
  html += "<input type='input' name='repeats' placeholder='repeats'><br />";
  html += "<input type='submit' name='cmd' value='Send'>";
  html += "</form>";

  HTMLFooter();
  server.send(200, "text/html", html);
}

void handleFWUpdate()
{
  showWEBAction();
  if (!server.authenticate(cfg.admin_username, cfg.admin_password))
  {
    return server.requestAuthentication();
  }
  else
  {
    HTMLHeader("Firmware Update");

    html += "<form method='POST' action='/dofwupdate' enctype='multipart/form-data'>\n";
    html += "<table>\n";
    html += "<tr>\n";
    html += "<td>Current version</td>\n";
    html += String("<td>") + FIRMWARE_VERSION + String("</td>\n");
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>Compiled</td>\n";
    html += String("<td>") + COMPILE_DATE + String("</td>\n");
    html += "</tr>\n";
    html += "<tr>\n";
    html += "<td>Firmware file</td>\n";
    html += "<td><input type='file' name='update'></td>\n";
    html += "</tr>\n";
    html += "</table>\n";
    html += "<br />";
    html += "<input type='submit' value='Update'>";
    html += "</form>";
    HTMLFooter();
    server.send(200, "text/html", html);
  }
}

void handleNotFound()
{
  showWEBAction();
  HTMLHeader("File Not Found");
  html += "URI: ";
  html += server.uri();
  html += "<br />\nMethod: ";
  html += (server.method() == HTTP_GET) ? "GET" : "POST";
  html += "<br />\nArguments: ";
  html += server.args();
  html += "<br />\n";
  HTMLFooter();
  for (uint8_t i = 0; i < server.args(); i++)
  {
    html += " " + server.argName(i) + ": " + server.arg(i) + "<br />\n";
  }

  server.send(404, "text/html", html);
}

void handleWiFiScan()
{
  showWEBAction();
  if (!server.authenticate(cfg.admin_username, cfg.admin_password))
  {
    return server.requestAuthentication();
  }
  else
  {

    HTMLHeader("WiFi Scan");

    int n = WiFi.scanNetworks();
    if (n == 0)
    {
      html += "No networks found.\n";
    }
    else
    {
      html += "<table>\n";
      html += "<tr>\n";
      html += "<th>#</th>\n";
      html += "<th>SSID</th>\n";
      html += "<th>Channel</th>\n";
      html += "<th>Signal</th>\n";
      html += "<th>RSSI</th>\n";
      html += "<th>Encryption</th>\n";
      html += "<th>BSSID</th>\n";
      html += "</tr>\n";
      for (int i = 0; i < n; ++i)
      {
        html += "<tr>\n";
        snprintf(buff, sizeof(buff), "%02d", (i + 1));
        html += String("<td>") + buff + String("</td>");
        html += "<td>\n";
        if (WiFi.isHidden(i))
        {
          html += "[hidden SSID]";
        }
        else
        {
          html += "<a href='/settings?ssid=";
          html += WiFi.SSID(i).c_str();
          html += "'>";
          html += WiFi.SSID(i).c_str();
          html += "</a>";
        }
        html += "</td>\n<td>";
        html += WiFi.channel(i);
        html += "</td>\n<td>";
        html += dBm2Quality(WiFi.RSSI(i));
        html += "%</td>\n<td>";
        html += WiFi.RSSI(i);
        html += "dBm</td>\n<td>";
        switch (WiFi.encryptionType(i))
        {
        case ENC_TYPE_WEP: // 5
          html += "WEP";
          break;
        case ENC_TYPE_TKIP: // 2
          html += "WPA TKIP";
          break;
        case ENC_TYPE_CCMP: // 4
          html += "WPA2 CCMP";
          break;
        case ENC_TYPE_NONE: // 7
          html += "OPEN";
          break;
        case ENC_TYPE_AUTO: // 8
          html += "WPA";
          break;
        }
        html += "</td>\n<td>";
        html += WiFi.BSSIDstr(i).c_str();
        html += "</td>\n";
        html += "</tr>\n";
      }
      html += "</table>";
    }

    HTMLFooter();

    server.send(200, "text/html", html);
  }
}

void handleReboot()
{
  showWEBAction();
  if (!server.authenticate(cfg.admin_username, cfg.admin_password))
  {
    return server.requestAuthentication();
  }
  else
  {
    boolean reboot = false;
    if (server.method() == HTTP_POST)
    {
      HTMLHeader("Reboot", 10, "/");
      html += "Reboot in progress...";
      reboot = true;
    }
    else
    {
      HTMLHeader("Reboot");
      html += "<form method='POST' action='/reboot'>";
      html += "<input type='submit' value='Reboot'>";
      html += "</form>";
    }
    HTMLFooter();

    server.send(200, "text/html", html);

    if (reboot)
    {
      delay(200);
      ESP.reset();
    }
  }
}

void handleRoot()
{
  showWEBAction();

  HTMLHeader("Main");

  html += "<table>\n";

  char timebuf[20];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;
  int days = hr / 24;
  snprintf(timebuf, 20, " %02d:%02d:%02d:%02d", days, hr % 24, min % 60, sec % 60);

  html += "<tr>\n<td>Uptime:</td>\n<td>";
  html += timebuf;
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Current time:</td>\n<td>";
  html += timeClient.getFormattedDate();
  html += " (UTC)</td>\n</tr>\n";

  html += "<tr>\n<td>Firmware:</td>\n<td>v";
  html += FIRMWARE_VERSION;
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Compiled:</td>\n<td>";
  html += COMPILE_DATE;
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>MQTT state:</td>\n<td>";
  if (client.connected())
  {
    html += "Connected";
  }
  else
  {
    html += "Not Connected";
  }
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Note:</td>\n<td>";
  if (strcmp(cfg.note, "") == 0)
  {
    html += "---";
  }
  else
  {
    html += cfg.note;
  }
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Hostname:</td>\n<td>";
  html += WiFi.hostname().c_str();
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>IP address:</td>\n<td>";
  html += WiFi.localIP().toString();
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Subnetmask:</td>\n<td>";
  html += WiFi.subnetMask().toString();
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Gateway:</td>\n<td>";
  html += WiFi.gatewayIP().toString();
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>DNS server:</td>\n<td>";
  html += WiFi.dnsIP().toString();
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>MAC address:</td>\n<td>";
  html += WiFi.macAddress().c_str();
  html += "</td>\n</tr>\n";

  html += "<tr>\n<td>Signal strength:</td>\n<td>";
  html += dBm2Quality(WiFi.RSSI());
  html += "% (";
  html += WiFi.RSSI();
  html += " dBm)</td>\n</tr>\n";

  html += "<tr>\n<td>Client IP:</td>\n<td>";
  html += server.client().remoteIP().toString().c_str();
  html += "</td>\n</tr>\n";

  html += "</table>\n";

  HTMLFooter();
  server.send(200, "text/html", html);
}

void handleSettings()
{
  showWEBAction();
  Serial.println(F("Site: handleSettings"));
  // HTTP Auth
  if (!server.authenticate(cfg.admin_username, cfg.admin_password))
  {
    return server.requestAuthentication();
  }
  else
  {
    Serial.println(F("Auth okay!"));
    boolean saveandreboot = false;
    String value;
    if (server.method() == HTTP_POST)
    { // Save Settings

      for (uint8_t i = 0; i < server.args(); i++)
      {
        // Trim String
        value = server.arg(i);
        value.trim();

        // RF Note
        if (server.argName(i) == "note")
        {
          value.toCharArray(cfg.note, sizeof(cfg.note) / sizeof(*cfg.note));

        } // HTTP Auth Adminaccess Username
        else if (server.argName(i) == "admin_username")
        {
          value.toCharArray(cfg.admin_username, sizeof(cfg.admin_username) / sizeof(*cfg.admin_username));

        } // HTTP Auth Adminaccess Password
        else if (server.argName(i) == "admin_password")
        {
          value.toCharArray(cfg.admin_password, sizeof(cfg.admin_password) / sizeof(*cfg.admin_password));

        } // WiFi SSID
        else if (server.argName(i) == "ssid")
        {
          value.toCharArray(cfg.wifi_ssid, sizeof(cfg.wifi_ssid) / sizeof(*cfg.wifi_ssid));

        } // WiFi PSK
        else if (server.argName(i) == "psk")
        {
          value.toCharArray(cfg.wifi_psk, sizeof(cfg.wifi_psk) / sizeof(*cfg.wifi_psk));

        } // Hostname
        else if (server.argName(i) == "hostname")
        {
          value.toCharArray(cfg.hostname, sizeof(cfg.hostname) / sizeof(*cfg.hostname));

        } // MQTT Server
        else if (server.argName(i) == "mqtt_server")
        {
          value.toCharArray(cfg.mqtt_server, sizeof(cfg.mqtt_server) / sizeof(*cfg.mqtt_server));

        } // MQTT Port
        else if (server.argName(i) == "mqtt_port")
        {
          cfg.mqtt_port = value.toInt();

        } // MQTT User
        else if (server.argName(i) == "mqtt_user")
        {
          value.toCharArray(cfg.mqtt_user, sizeof(cfg.mqtt_user) / sizeof(*cfg.mqtt_user));

        } // MQTT Password
        else if (server.argName(i) == "mqtt_password")
        {
          value.toCharArray(cfg.mqtt_password, sizeof(cfg.mqtt_password) / sizeof(*cfg.mqtt_password));

        } // MQTT Prefix
        else if (server.argName(i) == "mqtt_prefix")
        {
          value.toCharArray(cfg.mqtt_prefix, sizeof(cfg.mqtt_prefix) / sizeof(*cfg.mqtt_prefix));

        } // LED Brightness
        else if (server.argName(i) == "led_brightness")
        {
          cfg.led_brightness = value.toInt();
        }

        saveandreboot = true;
      }
    }

    if (saveandreboot)
    {
      HTMLHeader("Settings", 10, "/settings");
      html += ">>> New Settings saved! Device will be reboot <<< ";
    }
    else
    {
      HTMLHeader("Settings");

      html += "<form action='/settings' method='post'>\n";
      html += "<table>\n";

      html += "<tr>\n<td>\nSettings source:</td>\n";
      html += "<td><input type='text' disabled value='";
      html += (configIsDefault ? "Default settings" : "EEPROM");
      html += "'></td>\n</tr>\n";

      html += "<tr>\n";
      html += "<td>Hostname:</td>\n";
      html += "<td><input name='hostname' type='text' maxlength='30' autocapitalize='none' placeholder='";
      html += WiFi.hostname().c_str();
      html += "' value='";
      html += cfg.hostname;
      html += "'></td></tr>\n";

      html += "<tr>\n<td>\nSSID:</td>\n";
      html += "<td><input name='ssid' type='text' autocapitalize='none' maxlength='30' value='";
      bool showssidfromcfg = true;
      if (server.method() == HTTP_GET)
      {
        if (server.arg("ssid") != "")
        {
          html += server.arg("ssid");
          showssidfromcfg = false;
        }
      }
      if (showssidfromcfg)
      {
        html += cfg.wifi_ssid;
      }
      html += "'> <a href='/wifiscan' onclick='return confirm(\"Go to scan site? Changes will be lost!\")'>Scan</a></td>\n</tr>\n";

      html += "<tr>\n<td>\nPSK:</td>\n";
      html += "<td><input name='psk' type='password' maxlength='30' value='";
      html += cfg.wifi_psk;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>\nNote:</td>\n";
      html += "<td><input name='note' type='text' maxlength='30' value='";
      html += cfg.note;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>\nAdmin username:</td>\n";
      html += "<td><input name='admin_username' type='text' maxlength='30' autocapitalize='none' value='";
      html += cfg.admin_username;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>\nAdmin password:</td>\n";
      html += "<td><input name='admin_password' type='password' maxlength='30' value='";
      html += cfg.admin_password;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>LED brightness:</td>\n";
      html += "<td><select name='led_brightness'>";
      html += "<option value='5'";
      html += (cfg.led_brightness == 5 ? " selected" : "");
      html += ">5%</option>";
      html += "<option value='10'";
      html += (cfg.led_brightness == 10 ? " selected" : "");
      html += ">10%</option>";
      html += "<option value='15'";
      html += (cfg.led_brightness == 15 ? " selected" : "");
      html += ">15%</option>";
      html += "<option value='25'";
      html += (cfg.led_brightness == 25 ? " selected" : "");
      html += ">25%</option>";
      html += "<option value='50'";
      html += (cfg.led_brightness == 50 ? " selected" : "");
      html += ">50%</option>";
      html += "<option value='75'";
      html += (cfg.led_brightness == 75 ? " selected" : "");
      html += ">75%</option>";
      html += "<option value='100'";
      html += (cfg.led_brightness == 100 ? " selected" : "");
      html += ">100%</option>";
      html += "</select>";
      html += "</td>\n</tr>\n";

      html += "<tr>\n<td>\nMQTT server:</td>\n";
      html += "<td><input name='mqtt_server' type='text' maxlength='30' autocapitalize='none' value='";
      html += cfg.mqtt_server;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>\nMQTT port:</td>\n";
      html += "<td><input name='mqtt_port' type='text' maxlength='5' autocapitalize='none' value='";
      html += cfg.mqtt_port;
      html += "'> (Default 1883)</td>\n</tr>\n";

      html += "<tr>\n<td>\nMQTT username:</td>\n";
      html += "<td><input name='mqtt_user' type='text' maxlength='50' autocapitalize='none' value='";
      html += cfg.mqtt_user;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>\nMQTT password:</td>\n";
      html += "<td><input name='mqtt_password' type='password' maxlength='50' autocapitalize='none' value='";
      html += cfg.mqtt_password;
      html += "'></td>\n</tr>\n";

      html += "<tr>\n<td>\nMQTT prefix:</td>\n";
      html += "<td><input name='mqtt_prefix' type='text' maxlength='30' autocapitalize='none' value='";
      html += cfg.mqtt_prefix;
      html += "'></td>\n</tr>\n";

      html += "</table>\n";

      html += "<br />\n";
      html += "<input type='submit' value='Save'>\n";
      html += "</form>\n";
    }
    HTMLFooter();
    server.send(200, "text/html", html);

    if (saveandreboot)
    {
      saveConfig();
      ESP.reset();
    }
  }
}

void MQTTprocessCommand(JsonObject &json)
{
  Serial.println(F("Processing incomming MQTT command"));

  char buffer[100];

  uint32_t hexaddress;
  uint32_t hexcommand;
  uint8_t repeats = 0;

  if (json.containsKey("adr"))
  {
    String hexstr = json["adr"].as<String>();
    hexstr.toCharArray(buffer, sizeof(buffer));
    toHex(buffer, &hexaddress);
  }

  if (json.containsKey("cmd"))
  {
    String hexstr = json["cmd"].as<String>();
    hexstr.toCharArray(buffer, sizeof(buffer));
    toHex(buffer, &hexcommand);
  }

  if (json.containsKey("rpt"))
  {
    repeats = json["rpt"].as<uint8_t>();
  }

  Serial.printf("MQTT command: adr: %02X, cmd: %02X, rpt: %d\n", hexaddress, hexcommand, repeats);

  if (hexaddress != 0 && hexcommand != 0)
  {
    sendIR(hexaddress, hexcommand, repeats);
  }
}

void MQTTcallback(char *topic, byte *payload, unsigned int length)
{
  showMQTTAction();
  Serial.println(F("Neq MQTT message (MQTTcallback)"));
  Serial.print(F("> Lenght: "));
  Serial.println(length);
  Serial.print(F("> Topic: "));
  Serial.println(topic);

  if (length)
  {
    StaticJsonDocument<256> jsondoc;
    DeserializationError err = deserializeJson(jsondoc, payload);
    if (err)
    {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(err.c_str());
    }
    else
    {

      Serial.print(F("> JSON: "));
      serializeJsonPretty(jsondoc, Serial);
      Serial.println();

      JsonObject object = jsondoc.as<JsonObject>();
      MQTTprocessCommand(object);
    }
  }
}

boolean MQTTreconnect()
{

  Serial.printf_P(PSTR("Connecting to MQTT Broker \"%s:%i\"..."), cfg.mqtt_server, cfg.mqtt_port);
  if (strcmp(cfg.mqtt_server, "") == 0)
  {
    Serial.println(F("failed. No server configured."));
    return false;
  }
  else
  {

    client.setServer(cfg.mqtt_server, cfg.mqtt_port);
    client.setCallback(MQTTcallback);

    // last will and testament topic
    snprintf(buff, sizeof(buff), MQTT_PUBLISH_STATUS_TOPIC, mqtt_prefix, WiFi.hostname().c_str());

    if (client.connect(WiFi.hostname().c_str(), cfg.mqtt_user, cfg.mqtt_password, buff, 0, 1, MQTT_LWT_MESSAGE))
    {
      Serial.println(F("connected!"));

      snprintf(buff, sizeof(buff), MQTT_SUBSCRIBE_CMD_TOPIC1, mqtt_prefix);
      client.subscribe(buff);
      Serial.printf_P(PSTR("Subscribed to topic %s\n"), buff);

      snprintf(buff, sizeof(buff), MQTT_SUBSCRIBE_CMD_TOPIC2, mqtt_prefix, WiFi.hostname().c_str());
      client.subscribe(buff);
      Serial.printf_P(PSTR("Subscribed to topic %s\n"), buff);
      return true;
    }
    else
    {
      Serial.print(F("failed with state "));
      Serial.println(client.state());
      return false;
    }
  }
}

void loadDefaults()
{

  // Config NOT from EEPROM
  configIsDefault = true;

  // Valid-Falg to verify config
  cfg.configversion = CURRENT_CONFIG_VERSION;

  // Note
  memcpy(cfg.note, "", sizeof(cfg.note) / sizeof(*cfg.note));

  memcpy(cfg.wifi_ssid, "", sizeof(cfg.wifi_ssid) / sizeof(*cfg.wifi_ssid));
  memcpy(cfg.wifi_psk, "", sizeof(cfg.wifi_psk) / sizeof(*cfg.wifi_psk));

  memcpy(cfg.hostname, "", sizeof(cfg.hostname) / sizeof(*cfg.hostname));
  memcpy(cfg.note, "", sizeof(cfg.note) / sizeof(*cfg.note));

  memcpy(cfg.admin_username, "", sizeof(cfg.admin_username) / sizeof(*cfg.admin_username));
  memcpy(cfg.admin_password, "", sizeof(cfg.admin_password) / sizeof(*cfg.admin_password));

  memcpy(cfg.mqtt_server, "", sizeof(cfg.mqtt_server) / sizeof(*cfg.mqtt_server));
  memcpy(cfg.mqtt_user, "", sizeof(cfg.mqtt_user) / sizeof(*cfg.mqtt_user));
  cfg.mqtt_port = 1883;
  memcpy(cfg.mqtt_password, "", sizeof(cfg.mqtt_password) / sizeof(*cfg.mqtt_password));
  memcpy(cfg.mqtt_prefix, "irbridge", sizeof(cfg.mqtt_prefix) / sizeof(*cfg.mqtt_prefix));
}

void loadConfig()
{
  EEPROM.begin(512);
  EEPROM.get(cfgStart, cfg);
  EEPROM.end();

  if (cfg.configversion != CURRENT_CONFIG_VERSION)
  {
    loadDefaults();
  }
  else
  {
    configIsDefault = false; // Config from EEPROM
  }
}

void handleButton()
{
  // bool inp = digitalRead(HWPIN_PUSHBUTTON);
  // // Serial.printf_P(PSTR("Button state: %d\n"), inp);
  // if (inp == 0) // Button pressed
  // {
  //   if (inp != previousButtonState)
  //   {
  //     Serial.printf_P(PSTR("Button short press @ %lu\n"), millis());
  //     buttonTimer = millis();
  //   }
  //   if ((millis() - buttonTimer >= TIME_BUTTON_LONGPRESS))
  //   {
  //     Serial.printf_P(PSTR("Button long press @ %lu\n"), millis());
  //     eraseConfig();
  //     ESP.reset();
  //   }

  //   // Delay a little bit to avoid bouncing
  //   delay(50);
  // }
  // previousButtonState = inp;
}

void setup(void)
{
  // LED Basic Setup
  // led.begin();

  // GPIO Basic Setup
  // pinMode(HWPIN_PUSHBUTTON, INPUT_PULLUP);

  // Load Config
  loadConfig();

  if (strcmp_P(cfg.mqtt_prefix, PSTR("")) == 0)
  {
    strncpy(mqtt_prefix, cfg.mqtt_prefix, sizeof(mqtt_prefix));
  }
  else
  {
    strncpy(mqtt_prefix, cfg.mqtt_prefix, (sizeof(mqtt_prefix) - 1));
    strcat(mqtt_prefix, "/");
  }

  Serial.begin(HWSERIAL_BAUD);
  delay(1000);
  Serial.printf_P(PSTR("\n+++ Welcome to IRBridge v%s+++\n"), FIRMWARE_VERSION);
  WiFi.mode(WIFI_OFF);

  // AP or Infrastucture Mode
  if (configIsDefault)
  {
    // Start AP
    Serial.println(F("Default Config loaded."));
    Serial.println(F("Starting WiFi SoftAP"));
    WiFi.softAP("IRBridge", "");
    setLed(LedColor::BLUE);
  }
  else
  {

    // LED brightness
    ledBrightness = (PWMRANGE / 100.00) * cfg.led_brightness;
    Serial.printf("LED brightness: %i/%i (%i%%)\n", ledBrightness, PWMRANGE, cfg.led_brightness);

    WiFi.mode(WIFI_STA);
    if (strcmp(cfg.hostname, "") != 0)
    {
      WiFi.hostname(cfg.hostname);
    }
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_psk);
    if (strcmp(cfg.hostname, "") != 0)
    {
      WiFi.hostname(cfg.hostname);
    }

    Serial.printf_P(PSTR("Connecing to '%s'. Please wait"), cfg.wifi_ssid);

    // Wait for connection
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(250);
      Serial.print(F("."));
      if (ledOneToggle)
      {
        setLed(LedColor::BLACK);
      }
      else
      {
        setLed(LedColor::BLUE);
      }
      ledOneToggle = !ledOneToggle;

      handleButton();
    }

    Serial.printf_P(PSTR("\nConnected to '%s'\n"), cfg.wifi_ssid);
    WiFi.printDiag(Serial);
    Serial.printf_P(PSTR("IP address: %s\n"), WiFi.localIP().toString().c_str());

    setLed(LedColor::BLUE);

    // MDNS responder
    if (MDNS.begin(cfg.hostname))
    {
      Serial.println(F("MDNS responder started"));
    }

    // NTPClient
    timeClient.begin();
  }

  // Arduino OTA Update
  httpUpdater.setup(&server, "/dofwupdate", cfg.admin_username, cfg.admin_password);

  // Webserver
  server.on(F("/"), handleRoot);
  server.on(F("/settings"), handleSettings);
  server.on(F("/fwupdate"), handleFWUpdate);
  server.on(F("/send"), handleSend);
  server.on(F("/reboot"), handleReboot);
  server.on(F("/wifiscan"), handleWiFiScan);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println(F("HTTP server started"));
}

void loop(void)
{

  // Switch back on WiFi LED after Webserver access
  if (((millis() - ledOneTime) > LED_WEB_MIN_TIME) &&
      ledOneLastColor != 0)
  {
    // led.clear();
    // led.setBrightness(cfg.led_brightness);
    // led.setPixelColor(0, ledOneLastColor);
    // led.show();
  }

  // Handle Button
  handleButton();

  // Handle Webserver
  server.handleClient();

  // NTPClient Update
  timeClient.update();

  // Config valid and WiFi connection
  if (!configIsDefault && WiFi.status() == WL_CONNECTED)
  {

    if (!client.connected())
    {
      // MQTT connect
      if (mqttLastReconnectAttempt == 0 || (millis() - mqttLastReconnectAttempt) >= MQTT_RECONNECT_INTERVAL)
      {
        mqttLastReconnectAttempt = millis();

        // switch off MQTT LED
        setLed(LedColor::RED);

        // try to reconnect
        if (MQTTreconnect())
        {
          // switch on MQTT LED
          setLed(LedColor::GREEN);

          mqttLastReconnectAttempt = 0;
        }
      }
    }
    else
    {
      // Switch on MQTT LED after MQTT action if we have server connection
      if (((millis() - ledTwoTime) > LED_MQTT_MIN_TIME) &&
          ledTwoLastColor != 0)
      {
        // led.clear();
        // led.setBrightness(cfg.led_brightness);
        // led.setPixelColor(0, ledTwoLastColor);
        // led.show();
      }

      // Handle MQTT msgs
      client.loop();
    }
  }
}
