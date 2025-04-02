#include "AiEsp32RotaryEncoder.h"
#include "DFRobotDFPlayerMini.h"
#include "NTPClient.h"
#include "TM1637Display.h"
#include "WiFiManager.h"
#include <AsyncTCP.h>
#include <ElegantOTA.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <EEPROM.h>

#define FPSerial Serial1
#define EEPROM_WINTER_TIME_ADDR 0
#define EEPROM_SIZE 1
// #define ELEGANTOTA_USE_ASYNC_WEBSERVER 1 // Add this row to the ElegantOTA sources at the beginning of the ElegantOTA.h file

//========================PINS DEFINITIONS==============================================================================================================================
const byte DISPLAY_CLK_PIN = 21;          // Connects to display's CLK pin
const byte DISPLAY_DIO_PIN = 22;          // Connects to display's DIO pin
const byte ROTARY_ENCODER_CLK_R_PIN = 25; // Connects to encoder's DIO pin
const byte ROTARY_ENCODER_DT_PIN = 26;    // Connects to encoder's DT pin
const byte ROTARY_ENCODER_SW_PIN = 27;    // Connects to encoder's SW pin
const byte RED_LED_PIN = 32;              // Connects to RED LED + leg
const byte WHITE_LED_PIN = 33;            // Connects to WHITE LED + leg
const byte MP3_RX_PIN = 16;               // Connects to mp3 module's TX
const byte MP3_TX_PIN = 17;               // Connects to mp3 module's RX
//======================================================================================================================================================================

//========================CONFIGURATIONS================================================================================================================================
const char *VERSION = "2.1";                               // App version
const char *AP_SSID = "R2D2";                              // SSID that will appear at first launch or in case of saved network not found
const char *AP_PASSWORD = "";                              // SSID password that will appear at first launch or in case of saved network not found
const char *HOSTNAME = "R2D2";                             // Hostname of the device that will be shown
const char *NTP_POOL_ADDRESS = "ua.pool.ntp.org";          // NTP server address (change to what you need, choose at at https://www.ntppool.org/)
const unsigned int WIFI_CONNECTION_TIMEOUT_IN_MS = 10000;  // 10 seconds to connect to wifi
const unsigned int GMT_TIMEZONE_PLUS_HOUR = 2;             // GMT + 2 timezone (Ukrainian Winter time)
const unsigned int VOLUME_LEVEL = 15;                      // From 0 to 30
const unsigned int MS_TO_HANDLE_IF_BUTTON_IS_PRESSED = 50; // if 50ms have passed since last LOW pulse, it means that the button has been pressed, released and pressed again
const unsigned int DISPLAY_BACKLIGHT_LEVEL = 0;            // Set display brightness (0 to 7)
const unsigned int freq = 5000;
const unsigned int resolution = 8;
const unsigned long NTP_CLIENT_UPDATE_TIME_IN_MS = 3600000;// One hour
//======================================================================================================================================================================

//========================VARIABLES=====================================================================================================================================
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_CLK_R_PIN,
                                                          ROTARY_ENCODER_DT_PIN,
                                                          ROTARY_ENCODER_SW_PIN,
                                                          -1,
                                                          4);

void printErrorDetails(uint8_t type, int value);

void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}

bool ntp_client_updated_on_startup = false;
bool wifi_is_connected = false;
bool mp3_player_is_connected = false;
bool show_colon = true;
bool is_summer_time = false;
unsigned long colon_previous_millis = 0;
unsigned long last_time_button_pressed = 0;
int encoder_pin_state = 0;
int timer_seconds = 0;
int timer_minutes = 0;
float timer_counter = 0;
float value_from_encoder = 0;
float red_led_brightness = 0;

DFRobotDFPlayerMini mp3_df_player;
WiFiUDP wifi_udp_client;
NTPClient *ntp_client;
TM1637Display digits_display(DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);
AsyncWebServer async_web_server(80);

const char *indexPageHtml = R"rawliteral(
  <html>
  <head>
      <title>R2-D2 Control Panel</title>
      <style>
          @import url('https://fonts.googleapis.com/css2?family=Orbitron&display=swap');

          body {
              font-family: 'Orbitron', sans-serif;
              margin: 0;
              padding: 0;
              display: flex;
              flex-direction: column;
              justify-content: center;
              align-items: center;
              text-align: center;
              background-color: #000;
              color: #1effd6;
              min-height: 100vh;
          }
          h1 {
              font-size: 2.5rem;
              margin-bottom: 1rem;
              text-shadow: 0 0 10px #1effd6, 0 0 20px #1effd6;
          }
          a {
              font-size: 1.2rem;
              color: #1effd6;
              text-decoration: none;
              padding: 0.5rem 1rem;
              border: 2px solid #1effd6;
              border-radius: 5px;
              transition: background-color 0.3s, color 0.3s;
          }
          a:hover {
              background-color: #1effd6;
              color: #000;
          }
          .toggle-container {
              margin-top: 20px;
              display: flex;
              align-items: center;
              gap: 10px;
          }
          .toggle {
              position: relative;
              display: inline-block;
              width: 60px;
              height: 34px;
          }
          .toggle input {
              opacity: 0;
              width: 0;
              height: 0;
          }
          .slider {
              position: absolute;
              cursor: pointer;
              top: 0;
              left: 0;
              right: 0;
              bottom: 0;
              background-color: #444;
              transition: .4s;
              border-radius: 34px;
              box-shadow: 0 0 10px #1effd6;
          }
          .slider:before {
              position: absolute;
              content: "";
              height: 26px;
              width: 26px;
              left: 4px;
              bottom: 4px;
              background-color: white;
              transition: .4s;
              border-radius: 50%;
          }
          input:checked + .slider {
              background-color: #1effd6;
              box-shadow: 0 0 15px #1effd6;
          }
          input:checked + .slider:before {
              transform: translateX(26px);
          }
          .r2d2-img {
              width: 120px;
              height: auto;
              margin-bottom: 20px;
              filter: drop-shadow(0 0 10px #1effd6);
          }
      </style>
  </head>
  <body>
      <img class="r2d2-img" src="https://i.imgur.com/8PdU9nS.png" alt="R2-D2">
      <h1>R2-D2 Control Panel</h1>
      <a href="/update">OTA Update</a>
      <div class="toggle-container">
        <label>Winter Time</label>
        <label class="toggle">
            <input type="checkbox" id="timeToggle" onclick="confirmToggle()" %%TOGGLE_STATE%%>
            <span class="slider"></span>
        </label>
        <label>Summer Time</label>
    </div>
      <script>
        function confirmToggle() {
            if (confirm("Are you sure you want to change the time setting? The galaxy depends on it!")) {
                fetch("/toggle-time", { method: "POST" })
                    .then(response => location.reload());
            } else {
                document.getElementById("timeToggle").checked = !document.getElementById("timeToggle").checked;
            }
        }
    </script>
  </body>
  </html>
  )rawliteral";

//=======================================================================================================================================================================

//========================LOOP===========================================================================================================================================
void loop()
{
  checkWifiConnectionAndReconnectIfLost();
  if (secondChanged())
  {
    show_colon = !show_colon;
  }
  showTime();

  mp3_player_is_connected = mp3_df_player.available();
  if (!mp3_player_is_connected)
  {
    printErrorDetails(mp3_df_player.readType(), mp3_df_player.read());
  }

  encoder_pin_state = digitalRead(ROTARY_ENCODER_SW_PIN);

  // Change brightness of the red led from 0 to 254
  red_led_brightness = red_led_brightness < 255 ? red_led_brightness + 1 : 0;
  ledcWrite(0, red_led_brightness);
  digitalWrite(WHITE_LED_PIN, LOW);

  // If we detect LOW signal, button is pressed
  if (encoder_pin_state == LOW)
  {
    if (millis() - last_time_button_pressed > MS_TO_HANDLE_IF_BUTTON_IS_PRESSED)
    {
      Serial.println("Button pressed!");
      bool timerUsed = setupTimer();
      if (!timerUsed)
      {
        showTime();
      }
    }

    // Remember last button press event
    last_time_button_pressed = millis();
  }

  ElegantOTA.loop();
  delay(1);
}
//=======================================================================================================================================================================

void checkWifiConnectionAndReconnectIfLost()
{
  wifi_is_connected = WiFi.status() == WL_CONNECTED;

  if (!wifi_is_connected)
  {
    Serial.println("WiFi connection lost, trying to reconnect...");
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.hostname(HOSTNAME);
    WiFi.begin(AP_SSID, AP_PASSWORD);
    unsigned long startAttemptTime = millis();
    const unsigned long connectionTimeout = 10000;

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < connectionTimeout)
    {
      delay(500);
      Serial.print(".");
    }

    wifi_is_connected = WiFi.status() == WL_CONNECTED;
  }
}

bool setupTimer()
{
  showDigitsOnDisplay(00, 00, show_colon);
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(WHITE_LED_PIN, HIGH);
  if (mp3_player_is_connected)
  {
    mp3_df_player.play(2);
  }
  delay(500);

  encoder_pin_state = digitalRead(ROTARY_ENCODER_SW_PIN);
  while (digitalRead(ROTARY_ENCODER_SW_PIN) == HIGH)
  {
    if (rotaryEncoder.encoderChanged())
    {
      value_from_encoder = rotaryEncoder.readEncoder();
      timer_counter = value_from_encoder > 0 ? value_from_encoder : 0;
      Serial.println("Read from encoder:");
      Serial.println(value_from_encoder);
      Serial.println("Timer:");
      Serial.println(timer_counter);

      if (rotaryEncoder.isEncoderButtonClicked())
      {
        Serial.println("button pressed");
      }
    }

    timer_minutes = timer_counter > 0 ? (timer_counter / 60) : 0;
    timer_seconds = timer_counter > 0 ? (((timer_counter / 60) - timer_minutes) * 60) : 0;
    showDigitsOnDisplay(timer_minutes, timer_seconds, show_colon);
  }

  bool timerUsed = (timer_counter > 0);
  if (timerUsed)
  {
    startCountdown(timer_counter);
  }
  return timerUsed;
}

void startCountdown(float timer_counter)
{
  if (mp3_player_is_connected)
  {
    mp3_df_player.play(8);
  }

  delay(1000);
  encoder_pin_state = digitalRead(ROTARY_ENCODER_SW_PIN);

  while (encoder_pin_state == HIGH)
  {
    encoder_pin_state = digitalRead(ROTARY_ENCODER_SW_PIN);
    for (int i = 10; i > 0; i--)
    {
      timer_counter = timer_counter > 0 ? (timer_counter - 0.1) : 0;
      timer_minutes = timer_counter > 0 ? (timer_counter / 60) : 0;
      timer_seconds = timer_counter > 0 ? (((timer_counter / 60) - timer_minutes) * 60) : 0;
      showDigitsOnDisplay(timer_minutes, timer_seconds, show_colon);
      delay(70);
      digitalWrite(RED_LED_PIN, LOW);
    }

    if (timer_counter <= 0)
    {
      if (mp3_player_is_connected)
      {
        mp3_df_player.play(8);
      }
      for (int i = 0; i < 9; i++)
      {
        animationsForEndOfCountdown();
      }

      if (mp3_player_is_connected)
      {
        mp3_df_player.play(5);
      }
      for (int i = 0; i < 9; i++)
      {
        animationsForEndOfCountdown();
      }

      if (mp3_player_is_connected)
      {
        mp3_df_player.play(7);
      }
      for (int i = 0; i < 9; i++)
      {
        animationsForEndOfCountdown();
      }

      encoder_pin_state = LOW;
      timer_counter = 0;
    };
  }
}

void animationsForEndOfCountdown()
{
  ledcWrite(0, 255);
  waitMilliseconds(random(10, 150));
  digitalWrite(WHITE_LED_PIN, HIGH);
  waitMilliseconds(random(10, 150));
  ledcWrite(0, 0);
  waitMilliseconds(random(10, 150));
  digitalWrite(WHITE_LED_PIN, LOW);
  waitMilliseconds(random(10, 150));
}

void waitMilliseconds(uint16_t msWait)
{
  uint32_t start = millis();

  while ((millis() - start) < msWait)
  {
    delay(1);
  }
}

void showTime()
{
  bool ntp_client_updated = false;
  if (wifi_is_connected)
  {
    ntp_client_updated = ntp_client->update();
  }
  if (!ntp_client_updated && !ntp_client_updated_on_startup)
  {
    turn_off_display();
  }
  else
  {
    turn_on_display();
    if (secondChanged())
    {
      show_colon = !show_colon;
    }
    showDigitsOnDisplay(ntp_client->getHours(), ntp_client->getMinutes(), show_colon);
  }
}

void showDigitsOnDisplay(int hour, int minutes, bool showColon)
{
  uint8_t colonOptions = showColon ? 0b01000000 : 0b00000000;
  digits_display.showNumberDecEx(hour, colonOptions, true, 2, 0);
  digits_display.showNumberDecEx(minutes, colonOptions, true, 2, 2);
}

void printErrorDetails(uint8_t type, int value)
{
  switch (type)
  {
  case TimeOut:
    Serial.println(F("Time Out!"));
    break;
  case WrongStack:
    Serial.println(F("Stack Wrong!"));
    break;
  case DFPlayerCardInserted:
    Serial.println(F("Card Inserted!"));
    break;
  case DFPlayerCardRemoved:
    Serial.println(F("Card Removed!"));
    break;
  case DFPlayerCardOnline:
    Serial.println(F("Card Online!"));
    break;
  case DFPlayerUSBInserted:
    Serial.println("USB Inserted!");
    break;
  case DFPlayerUSBRemoved:
    Serial.println("USB Removed!");
    break;
  case DFPlayerPlayFinished:
    Serial.print(F("Number:"));
    Serial.print(value);
    Serial.println(F(" Play Finished!"));
    break;
  case DFPlayerError:
    Serial.print(F("DFPlayerError:"));
    switch (value)
    {
    case Busy:
      Serial.println(F("Card not found"));
      break;
    case Sleeping:
      Serial.println(F("Sleeping"));
      break;
    case SerialWrongStack:
      Serial.println(F("Get Wrong Stack"));
      break;
    case CheckSumNotMatch:
      Serial.println(F("Check Sum Not Match"));
      break;
    case FileIndexOut:
      Serial.println(F("File Index Out of Bound"));
      break;
    case FileMismatch:
      Serial.println(F("Cannot Find File"));
      break;
    case Advertise:
      Serial.println(F("In Advertise"));
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

bool secondChanged()
{
  unsigned long currentMillis = millis();

  if (currentMillis - colon_previous_millis >= 1000)
  {
    colon_previous_millis = currentMillis;
    return true;
  }

  return false;
}

void showLoadingOnDigitsDisplay()
{
  turn_on_display();
  uint8_t frames[][4] = {
      {0x01 | 0x02, 0x01 | 0x02, 0x01 | 0x02, 0x01 | 0x02}, // Upper horizontal and right upper
      {0x02 | 0x04, 0x02 | 0x04, 0x02 | 0x04, 0x02 | 0x04}, // Right upper and right lower
      {0x04 | 0x08, 0x04 | 0x08, 0x04 | 0x08, 0x04 | 0x08}, // Right lower and bottom horizontal
      {0x08 | 0x10, 0x08 | 0x10, 0x08 | 0x10, 0x08 | 0x10}, // Bottom horizontal and left lower
      {0x10 | 0x20, 0x10 | 0x20, 0x10 | 0x20, 0x10 | 0x20}, // Left lower and left upper
      {0x20 | 0x01, 0x20 | 0x01, 0x20 | 0x01, 0x20 | 0x01}, // Left upper and upper horizontal
  };

  static int currentFrame = 0; // Current animation frame
  digits_display.setSegments(frames[currentFrame]);
  currentFrame = (currentFrame + 1) % 6; // Move to the next frame
  delay(500);
  turn_off_display();
}

void turn_off_display()
{
  digits_display.clear();
  digits_display.setBrightness(0, false);
}

void turn_on_display()
{
  digits_display.setBrightness(DISPLAY_BACKLIGHT_LEVEL, true);
}
