//========================SETUP===========================================================
void setup()
{
  setupLeds();
  setupDFPlayer();
  digits_display.setBrightness(DISPLAY_BACKLIGHT_LEVEL);
  showLoadingOnDigitsDisplay();
  setupWiFi();
  setupEncoder();
  setupWebServer();
  ElegantOTA.begin(&async_web_server);

  if (wifi_is_connected)
  {
    ntp_client_updated_on_startup = ntp_client.update();
    turn_on_display();
  }

  Serial.println("\n Starting");
}

void setupLeds()
{
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(WHITE_LED_PIN, OUTPUT);
  ledcAttach(RED_LED_PIN, freq, resolution);
}

void setupWiFi()
{
  WiFiManager wifiManager;
  wifiManager.setHostname(HOSTNAME);
  wifiManager.setTimeout(WIFI_CONNECTION_TIMEOUT_IN_MS);
  wifi_is_connected = wifiManager.autoConnect(AP_SSID, AP_PASSWORD);
  if (!wifi_is_connected)
  {
    Serial.println("failed to connect to wifi");
  }
  if (MDNS.begin(HOSTNAME))
  {
    Serial.println("MDNS responder started");
  }
}

void setupEncoder()
{
  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  rotaryEncoder.setBoundaries(0, 3500, false); // minValue, maxValue, circleValues true|false (when max go to min and vice versa)
  rotaryEncoder.setAcceleration(250);
}

void setupDFPlayer()
{
  FPSerial.begin(9600, SERIAL_8N1, MP3_RX_PIN, MP3_TX_PIN);
  Serial.println();
  Serial.println(F("DFRobot DFPlayer Mini Demo"));
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  mp3_player_is_connected = mp3_df_player.begin(FPSerial, /*isACK = */ true, /*doReset = */ true);
  mp3_df_player.volume(VOLUME_LEVEL);
  if (mp3_player_is_connected)
  {
    Serial.println(F("DFPlayer Mini online."));
    mp3_df_player.play(1);
  }
  else
  {
    // Use serial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
  }
}

void setupWebServer()
{
  async_web_server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                      { request->send(200, "text/html", "I'm online and running. Version: " + String(VERSION) + "<br><a href=\"/update\">OTA Update</a>"); });

  async_web_server.on("/version", HTTP_GET, [](AsyncWebServerRequest *request)
                      { request->send(200, "text/plain", String(VERSION)); });

  async_web_server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
                      {
              request->send(200, "text/plain", "rebooting");
              AsyncWebServerRequest* req = request;
              req->onDisconnect([]()
              {
                  turn_off_display();
                  delay(500);
                  ESP.restart();
              }); });
  async_web_server.begin();
}

//========================================================================================