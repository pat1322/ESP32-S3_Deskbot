#include "wifi_portal.h"

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

#include "wifi_store.h"
#include "network.h"

// Open network (no password) — the lowest-friction setup flow for a
// personal desk gadget. Accepted trade-off, scoped to the brief setup
// window only; see SECURITY.md.
static const char* AP_SSID = "Deskbot-Setup";
static const byte  DNS_PORT = 53;

static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

// Multiple OS captive-portal probes hit "/" in quick succession; a fresh
// scan takes 2-4s, so cache results briefly rather than rescanning per hit.
static String scannedOptionsHtml() {
    static String   cached;
    static uint32_t lastScanMs = 0;

    if (cached.length() == 0 || millis() - lastScanMs > 15000) {
        int n = WiFi.scanNetworks();
        String options;
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;
            options += "<option value=\"" + htmlEscape(ssid) + "\">" + htmlEscape(ssid) +
                       " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
        }
        cached = options;
        lastScanMs = millis();
    }
    return cached;
}

static String buildPortalPage() {
    String page;
    page += "<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>Deskbot Setup</title><style>"
            "body{font-family:sans-serif;background:#0a0d12;color:#edeff3;padding:24px;max-width:360px;margin:0 auto}"
            "h1{font-size:16px;letter-spacing:.08em;color:#ff9d42;margin-bottom:4px}"
            "label{display:block;margin:14px 0 4px;font-size:13px;color:#7c8492}"
            "select,input{width:100%;padding:10px;background:#12161f;border:1px solid #232935;"
            "border-radius:6px;color:#edeff3;font-size:16px;box-sizing:border-box}"
            "button{width:100%;margin-top:18px;padding:12px;background:#ff9d42;border:none;"
            "border-radius:6px;color:#1a1206;font-weight:600;font-size:15px}"
            "p{font-size:12px;color:#7c8492}</style></head><body>";
    page += "<h1>DESKBOT SETUP</h1>";
    page += "<p>Pick your WiFi network and enter its password.</p>";
    page += "<form method='POST' action='/save'>";
    page += "<label>Network</label>"
            "<select onchange=\"document.getElementById('ssid').value=this.value\">";
    page += "<option value=''>-- choose a network --</option>";
    page += scannedOptionsHtml();
    page += "</select>";
    page += "<label>Or enter it manually (hidden networks)</label>"
            "<input type='text' id='ssid' name='ssid' autocomplete='off'>";
    page += "<label>Password</label>"
            "<input type='password' name='password' autocomplete='off'>";
    page += "<button type='submit'>Connect</button>";
    page += "</form></body></html>";
    return page;
}

void wifiPortalRun() {
    WiFi.mode(WIFI_AP);
    IPAddress apIP(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID);
    Serial.printf("[Portal] AP '%s' up at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

    DNSServer dnsServer;
    dnsServer.start(DNS_PORT, "*", apIP); // answers every query with our own IP, triggering the OS captive-portal popup

    WebServer webServer(80);

    webServer.on("/", HTTP_GET, [&webServer]() {
        webServer.send(200, "text/html", buildPortalPage());
    });

    webServer.on("/save", HTTP_POST, [&webServer, &dnsServer]() {
        String ssid = webServer.arg("ssid");
        String password = webServer.arg("password");

        if (ssid.length() == 0) {
            webServer.send(400, "text/html", "<p>Please choose or enter a network name.</p>");
            return;
        }

        wifiStoreSave(ssid, password);

        webServer.send(200, "text/html",
            "<!doctype html><html><body style='font-family:sans-serif;background:#0a0d12;"
            "color:#edeff3;text-align:center;padding-top:60px'>"
            "<p>Connecting to \"" + htmlEscape(ssid) + "\"&hellip;</p>"
            "<p style='color:#7c8492;font-size:13px'>The desk unit will restart. If the "
            "password was wrong, setup mode reopens automatically.</p></body></html>");
        delay(500); // let the response flush before tearing the AP down

        dnsServer.stop();
        webServer.stop();
        WiFi.softAPdisconnect(true);

        // Best-effort test connect on the new creds before restarting —
        // either way we restart, reusing the normal boot-time retry/portal
        // logic instead of a separate in-portal success/failure branch.
        WiFi.mode(WIFI_STA);
        connectWiFi(20000);
        ESP.restart();
    });

    webServer.onNotFound([&webServer]() {
        // Funnels every OS's captive-portal probe path (/generate_204,
        // /hotspot-detect.html, /ncsi.txt, /connecttest.txt, etc.) to the
        // setup form, which is what triggers the OS's auto-popup.
        webServer.sendHeader("Location", "/", true);
        webServer.send(302, "text/plain", "");
    });

    webServer.begin();

    while (true) {
        dnsServer.processNextRequest();
        webServer.handleClient();
        yield();
    }
}
