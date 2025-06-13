#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <base64.h>
#include "pin_config.h"

// Web server for configuration
WebServer server(80);
Preferences prefs;

TFT_eSPI tft = TFT_eSPI();

String ssid;
String password;
String jiraHost;
String jiraEmail;
String jiraToken;
uint32_t lastFetch = 0;
uint16_t displayIndex = 0;

struct Issue {
    String key;
    String summary;
};

std::vector<Issue> issues;

void saveConfig() {
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.putString("host", jiraHost);
    prefs.putString("email", jiraEmail);
    prefs.putString("token", jiraToken);
}

void loadConfig() {
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    jiraHost = prefs.getString("host", "");
    jiraEmail = prefs.getString("email", "");
    jiraToken = prefs.getString("token", "");
}

void handleRoot() {
    String html = "<html><body><h1>Jira Carousel Config</h1>";
    html += "<form action=/save method=post>";
    html += "SSID:<input name=ssid value=" + ssid + "><br>";
    html += "Password:<input name=pass type=password value=" + password + "><br>";
    html += "Jira Host:<input name=host value=" + jiraHost + "><br>";
    html += "Email:<input name=email value=" + jiraEmail + "><br>";
    html += "Token:<input name=token type=password value=" + jiraToken + "><br>";
    html += "<input type=submit value=Save></form></body></html>";
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("ssid")) ssid = server.arg("ssid");
    if (server.hasArg("pass")) password = server.arg("pass");
    if (server.hasArg("host")) jiraHost = server.arg("host");
    if (server.hasArg("email")) jiraEmail = server.arg("email");
    if (server.hasArg("token")) jiraToken = server.arg("token");
    saveConfig();
    server.send(200, "text/plain", "Saved. Rebooting...");
    delay(1000);
    ESP.restart();
}

void setupWiFi() {
    if (ssid.isEmpty()) {
        WiFi.softAP("JiraCarouselSetup");
        IPAddress IP = WiFi.softAPIP();
        Serial.print("AP IP: ");
        Serial.println(IP);
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
            delay(500);
        }
        if (WiFi.status() != WL_CONNECTED) {
            WiFi.softAP("JiraCarouselSetup");
        }
    }
}

void fetchIssues() {
    if (jiraHost.isEmpty()) return;
    HTTPClient http;
    String url = "https://" + jiraHost + "/rest/api/3/search?jql=assignee=currentUser()%20order%20by%20updated%20desc&maxResults=5";
    http.begin(url);
    String auth = jiraEmail + ":" + jiraToken;
    auth = base64::encode(auth);
    http.addHeader("Authorization", "Basic " + auth);
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        DynamicJsonDocument doc(8192);
        deserializeJson(doc, http.getString());
        JsonArray arr = doc["issues"].as<JsonArray>();
        issues.clear();
        for (JsonObject obj : arr) {
            Issue issue;
            issue.key = obj["key"].as<String>();
            issue.summary = obj["fields"]["summary"].as<String>();
            issues.push_back(issue);
        }
    }
    http.end();
}

void displayCurrentIssue() {
    if (WiFi.status() != WL_CONNECTED || ssid.isEmpty()) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 0);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.println("Connect to Wi-Fi:");
        tft.println("JiraCarouselSetup");
        return;
    }
    if (issues.empty()) {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(0, 0);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print("No issues");
        return;
    }
    displayIndex %= issues.size();
    const Issue &issue = issues[displayIndex];
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.println(issue.key);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println(issue.summary);
    displayIndex++;
}

void setup() {
    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    Serial.begin(115200);
    tft.begin();
    tft.setRotation(1);

    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Connect to Wi-Fi:");
    tft.println("JiraCarouselSetup");

    prefs.begin("jira", false);
    loadConfig();
    setupWiFi();

    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
}

void loop() {
    server.handleClient();
    if (WiFi.status() == WL_CONNECTED && millis() - lastFetch > 60000) {
        fetchIssues();
        lastFetch = millis();
    }
    static uint32_t lastDisplay = 0;
    if (millis() - lastDisplay > 5000) {
        displayCurrentIssue();
        lastDisplay = millis();
    }
}

