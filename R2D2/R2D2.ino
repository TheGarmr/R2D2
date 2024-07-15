#include "WiFiManager.h"
#include "NTPClient.h"
#include "TM1637Display.h"
#include "DFRobotDFPlayerMini.h"
#include "AiEsp32RotaryEncoder.h"

//========================CONFIGURATIONS==========================================

#define DISPLAY_CLK_PIN 21
#define DISPLAY_DIO_PIN 22
#define ROTARY_ENCODER_CLK_R_PIN 25
#define ROTARY_ENCODER_DT_PIN 26
#define ROTARY_ENCODER_SW_PIN 27
#define RED_LED_PIN 32
#define WHITE_LED_PIN 33
#define ROTARY_ENCODER_VCC_PIN -1 // VCC pin not used
#define ROTARY_ENCODER_STEPS 4    // Rotary Encoder

const char *WIFI_SSID = "R2D2";
const char *WIFI_PASSWORD = "";
const char *NTP_POOL_ADDRESS = "ua.pool.ntp.org";
const long UTC_OFFSET_SECONDS = 3600;             // Offset in seconds
const int VOLUME_LEVEL = 15;                      // From 0 to 30
const int MS_TO_HANDLE_IF_BUTTON_IS_PRESSED = 50; // if 50ms have passed since last LOW pulse, it means that the button has been pressed, released and pressed again
const int TIMEZONE_OFFSET_HOURS = 3;              // UTC + value in hours (Summer time)
const int DISPLAY_BACKLIGHT_LEVEL = 0;            // Set display brightness (0 to 7)
const byte MP3_RX_PIN = 16;                       // Connects to mp3 module's TX
const byte MP3_TX_PIN = 17;                       // Connects to mp3 module's RX
//===================================================================================

//========================VARIABLES==================================================
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_CLK_R_PIN,
                                                          ROTARY_ENCODER_DT_PIN,
                                                          ROTARY_ENCODER_SW_PIN,
                                                          ROTARY_ENCODER_VCC_PIN,
                                                          ROTARY_ENCODER_STEPS);

void printErrorDetails(uint8_t type, int value);

void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}

#define FPSerial Serial1
const int freq = 5000;
const int resolution = 8;
bool wifi_is_connected;
unsigned long colon_previous_millis = 0;
unsigned long last_time_button_pressed = 0;
int encoder_pin_state = 0;
int timer_seconds = 0;
int timer_minutes = 0;
float timer_counter = 0;
float value_from_encoder = 0;
float red_led_brightness = 0;
bool show_colon = true;

DFRobotDFPlayerMini mp3_df_player;
WiFiUDP wifi_udp_client;
NTPClient ntp_client(wifi_udp_client, NTP_POOL_ADDRESS, UTC_OFFSET_SECONDS *TIMEZONE_OFFSET_HOURS);
TM1637Display digits_display(DISPLAY_CLK_PIN, DISPLAY_DIO_PIN);
//========================================================================================

//========================SETUP===========================================================
void setup()
{
  setupLeds();
  digits_display.setBrightness(DISPLAY_BACKLIGHT_LEVEL);
  showTime(0, 0);

  setupWiFi();
  ntp_client.begin();
  setupDFPlayer();
  setupEncoder();

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
  Serial.begin(9600);

  WiFiManager wifiManager;
  wifiManager.setHostname("R2D2");
  wifiManager.setTimeout(180);
  // fetches ssid and password and tries to connect, if connections succeeds it starts an access point with the name called "R2D2" and waits in a blocking loop for configuration
  wifi_is_connected = wifiManager.autoConnect(WIFI_SSID, WIFI_PASSWORD);
  if (!wifi_is_connected)
  {
    Serial.println("failed to connect and timeout occurred");
    ESP.restart(); // reset and try again
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

  if (!mp3_df_player.begin(FPSerial, /*isACK = */ true, /*doReset = */ true))
  {
    // Use serial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    while (true)
    {
      delay(0); // Code to compatible with ESP8266 watch dog.
    }
  }
  Serial.println(F("DFPlayer Mini online."));
  mp3_df_player.volume(VOLUME_LEVEL);
  mp3_df_player.play(1);
}

//========================================================================================

//========================LOOP============================================================
void loop()
{
  if (secondChanged())
  {
    show_colon = !show_colon;
  }
  showTime();

  if (mp3_df_player.available() == false)
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

  delay(1);
  handleWinterTime();
}
//========================================================================================

bool setupTimer()
{
  showTime(00, 00);
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(WHITE_LED_PIN, HIGH);
  mp3_df_player.play(2);
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
    showTime(timer_minutes, timer_seconds);
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
  mp3_df_player.play(8);

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
      showTime(timer_minutes, timer_seconds);
      delay(70);
      digitalWrite(RED_LED_PIN, LOW);
    }

    if (timer_counter <= 0)
    {
      mp3_df_player.play(3);
      for (int i = 0; i < 9; i++)
      {
        animationsForEndOfCountdown();
      }

      mp3_df_player.play(5);
      for (int i = 0; i < 9; i++)
      {
        animationsForEndOfCountdown();
      }

      mp3_df_player.play(7);
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
  ntp_client.update();
  showTime(ntp_client.getHours(), ntp_client.getMinutes());
}

void showTime(int hour, int minutes)
{
  uint8_t colonOptions = show_colon ? 0b01000000 : 0b00000000;
  digits_display.showNumberDecEx(hour, colonOptions, true, 2, 0);
  digits_display.showNumberDecEx(minutes, colonOptions, true, 2, 2);
}

void handleWinterTime()
{

  Serial.print("Time: ");
  Serial.println(ntp_client.getFormattedTime());
  unsigned long epochTime = ntp_client.getEpochTime();
  struct tm *ptm = gmtime((time_t *)&epochTime);
  int currentYear = ptm->tm_year + 1900;
  Serial.print("Year: ");
  Serial.println(currentYear);

  int monthDay = ptm->tm_mday;
  Serial.print("Month day: ");
  Serial.println(monthDay);

  int currentMonth = ptm->tm_mon + 1;
  Serial.print("Month: ");
  Serial.println(currentMonth);

  if ((currentMonth * 30 + monthDay) >= 121 && (currentMonth * 30 + monthDay) < 331)
  {
    ntp_client.setTimeOffset(UTC_OFFSET_SECONDS * TIMEZONE_OFFSET_HOURS);
  } // Change daylight saving time - Summer - change 31/03 at 00:00
  else
  {
    ntp_client.setTimeOffset((UTC_OFFSET_SECONDS * TIMEZONE_OFFSET_HOURS) - 3600);
  } // Change daylight saving time - Winter - change 31/10 at 00:00
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