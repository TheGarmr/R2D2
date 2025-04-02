//========================SETUP===========================================================
void setup()
{
  EEPROM.begin(EEPROM_SIZE);
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
    setupNtpClient();
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
                      { handle_web_root_query(request); });

  async_web_server.on("/version", HTTP_GET, [](AsyncWebServerRequest *request)
                      { request->send(200, "text/plain", String(VERSION)); });

  async_web_server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request)
                      { handle_reboot_query(request); });

  async_web_server.on("/toggle-time", HTTP_POST, [](AsyncWebServerRequest *request)
                      { handle_toggle_time_query(request); });
  async_web_server.begin();
}

void setupNtpClient()
{
  is_summer_time = EEPROM.read(EEPROM_WINTER_TIME_ADDR) == 1;

  unsigned long gmt_offset = 3600 * (GMT_TIMEZONE_PLUS_HOUR + (is_summer_time ? 1 : 0));
  ntp_client = new NTPClient(wifi_udp_client, NTP_POOL_ADDRESS, gmt_offset, NTP_CLIENT_UPDATE_TIME_IN_MS);
  ntp_client->begin();
  ntp_client_updated_on_startup = ntp_client->update();
}

//========================================================================================