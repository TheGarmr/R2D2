void handle_web_root_query(AsyncWebServerRequest *request)
{
    String html = indexPageHtml;
    html.replace("%%TOGGLE_STATE%%", is_summer_time ? "checked" : "");

    request->send(200, "text/html", html);
}

void handle_reboot_query(AsyncWebServerRequest *request)
{
    request->send(200, "text/plain", "rebooting");
    AsyncWebServerRequest *req = request;
    req->onDisconnect([]()
                      {
                  turn_off_display();
                  delay(500);
                  ESP.restart(); });
}

void handle_toggle_time_query(AsyncWebServerRequest *request)
{
    is_summer_time = !is_summer_time;
    EEPROM.write(EEPROM_WINTER_TIME_ADDR, is_summer_time);
    EEPROM.commit();
    EEPROM.end();
    request->send(200, "text/plain", "OK");
    ESP.restart();
}