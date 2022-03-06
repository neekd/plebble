/*
       __           ___           ___           ___     
     /__/\         /  /\         /  /\         /__/|    
     \  \:\       /  /:/_       /  /:/_       |  |:|    
      \  \:\     /  /:/ /\     /  /:/ /\      |  |:|    
  _____\__\:\   /  /:/ /:/_   /  /:/ /:/_   __|  |:|    
 /__/::::::::\ /__/:/ /:/ /\ /__/:/ /:/ /\ /__/\_|:|____
 \  \:\~~\~~\/ \  \:\/:/ /:/ \  \:\/:/ /:/ \  \:\/:::::/
  \  \:\  ~~~   \  \::/ /:/   \  \::/ /:/   \  \::/~~~~ 
   \  \:\        \  \:\/:/     \  \:\/:/     \  \:\     
    \  \:\        \  \::/       \  \::/       \  \:\    
     \__\/         \__\/         \__\/         \__\/ 
*/

#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID "Put your WiFi SSID here"
#define WIFI_PASSWD "Put your WiFi password here"

extern const unsigned char bitcoinIcon[512];

U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
TTGOClass *twatch = nullptr;
GxEPD_Class *ePaper = nullptr;
PCF8563_Class *rtc = nullptr;
AXP20X_Class *power = nullptr;
Button2 *btn = nullptr;
uint32_t loopMillis = 0;
int16_t x, y;
unsigned long lastTime = 0;
String latestPrice = "";
String medianFeeRate = "";
int lastBlockHeight = 0;
unsigned long timerDelay = 600000;

void initWiFi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWD);
    Serial.println("Connecting to WiFi ..");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(1000);
    }
    Serial.println(WiFi.status());
    Serial.println(WiFi.localIP());
}

String getPrice()
{
    HTTPClient http;
    http.begin("https://api.bittrex.com/v3/markets/btc-usd/ticker");
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
        StaticJsonBuffer<300> JSONBuffer;
        JsonObject &parsed = JSONBuffer.parseObject(http.getString());

        if (!parsed.success())
        {
            Serial.println("Price parsing failed");
            http.end();
            return "";
        }

        http.end();
        return parsed["lastTradeRate"];
    }
    else
    {
        Serial.print("Error code: ");
        Serial.println(http.errorToString(httpResponseCode));
        http.end();
        return "";
    }
}

String getMedianFeeRate()
{
    HTTPClient http;
    http.begin("https://mempool.space/api/v1/fees/mempool-blocks");
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
        StaticJsonBuffer<1200> JSONBuffer;
        JsonArray &parsed = JSONBuffer.parseArray(http.getString());

        if (!parsed.success())
        {
            Serial.println("Fee parsing failed");
            http.end();
            return "";
        }

        http.end();
        return parsed[0]["medianFee"];
    }
    else
    {
        Serial.print("Error code: ");
        Serial.println(http.errorToString(httpResponseCode));
        http.end();
        return "";
    }
}

int getBlockHeight()
{
    HTTPClient http;
    http.begin("https://mempool.space/api/blocks/tip/height");
    int httpResponseCode = http.GET();

    if (httpResponseCode > 0)
    {
        int blockHeight = http.getString().toInt();
        http.end();
        return blockHeight;
    }
    else
    {
        Serial.print("Error code: ");
        Serial.println(http.errorToString(httpResponseCode));
        http.end();
        return -1;
    }
}

void setupDisplay()
{
    u8g2Fonts.begin(*ePaper);
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
}

void mainPage(bool fullScreen)
{
    uint16_t tbw, tbh;
    static int16_t lastX, lastY;
    static uint16_t lastW, lastH;
    static uint8_t hh = 0, mm = 0;
    static uint8_t lastWeek = 0;
    static uint8_t lastDay = 0;
    char clockBuff[64] = "00:00";
    char priceBuff[64] = "$0.00";
    char feeBuff[64] = "~0 sat/vB";
    const char *weekChars[] = {"Sun", "Mon", "Tues", "Wednes", "Thurs", "Fri", "Satur"};

    RTC_Date d = rtc->getDateTime();
    if (mm == d.minute && !fullScreen)
    {
        return;
    }

    mm = d.minute;
    hh = d.hour;
    if (lastDay != d.day)
    {
        lastDay = d.day;
        lastWeek = rtc->getDayOfWeek(d.day, d.month, d.year);
        fullScreen = true;
    }

    snprintf(clockBuff, sizeof(clockBuff), "%02d:%02d", hh, mm);

    if (fullScreen)
    {

        // icon
        ePaper->drawBitmap(15, 5, bitcoinIcon, 64, 64, GxEPD_BLACK);

        // blockheight
        u8g2Fonts.setCursor(90, 45);
        u8g2Fonts.setFont(u8g2_font_inr16_mn);
        u8g2Fonts.printf("%d", lastBlockHeight);

        // median fee
        u8g2Fonts.setFont(u8g2_font_inr16_mr);
        snprintf(feeBuff, sizeof(feeBuff), "~%.0f sat/vB", medianFeeRate.toFloat());
        tbh = u8g2Fonts.getFontAscent() - u8g2Fonts.getFontDescent();
        tbw = u8g2Fonts.getUTF8Width(feeBuff);
        x = ((ePaper->width() - tbw) / 2);
        y = ((ePaper->height() - tbh) / 2) + 5;
        u8g2Fonts.setCursor(x, y);
        u8g2Fonts.print(feeBuff);

        // price
        u8g2Fonts.setFont(u8g2_font_inr19_mr);
        snprintf(priceBuff, sizeof(priceBuff), "$%.2f", latestPrice.toFloat());
        tbh = u8g2Fonts.getFontAscent() - u8g2Fonts.getFontDescent();
        tbw = u8g2Fonts.getUTF8Width(priceBuff);
        x = ((ePaper->width() - tbw) / 2);
        y = ((ePaper->height() - tbh) / 2) + 50;
        u8g2Fonts.setCursor(x, y);
        u8g2Fonts.print(priceBuff);

        // clock
        lastX = 110;
        lastY = 175;
        u8g2Fonts.setFont(u8g2_font_inr16_mn);
        u8g2Fonts.setCursor(lastX, lastY);
        u8g2Fonts.print(clockBuff);
        lastH = u8g2Fonts.getFontAscent() - u8g2Fonts.getFontDescent();
        lastW = u8g2Fonts.getUTF8Width(clockBuff);

        // day of the week
        u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312a);
        u8g2Fonts.setCursor(25, lastY - 2);
        u8g2Fonts.printf("%sday", weekChars[lastWeek]);

        ePaper->update();
    }
    else
    {
        u8g2Fonts.setFont(u8g2_font_inb16_mf);
        ePaper->fillRect(lastX, lastY - u8g2Fonts.getFontAscent() - 3, lastW, lastH, GxEPD_WHITE);
        ePaper->fillScreen(GxEPD_WHITE);
        ePaper->setTextColor(GxEPD_BLACK);
        lastW = u8g2Fonts.getUTF8Width(clockBuff);
        u8g2Fonts.setCursor(lastX, lastY);
        u8g2Fonts.print(clockBuff);
        ePaper->updateWindow(lastX, lastY - u8g2Fonts.getFontAscent() - 3, lastW, lastH, false);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    // Get watch object
    twatch = TTGOClass::getWatch();
    twatch->begin();
    rtc = twatch->rtc;
    power = twatch->power;
    btn = twatch->button;
    ePaper = twatch->ePaper;

    // Use compile time as RTC input time
    rtc->check();

    // Connect to WiFi
    initWiFi();

    // Get Latest Values
    latestPrice = getPrice();
    lastBlockHeight = getBlockHeight();
    medianFeeRate = getMedianFeeRate();

    // Initialize the ink screen
    setupDisplay();

    // Initialize the interface
    mainPage(true);

    // Set CPU frequency
    setCpuFrequencyMhz(120);
}

void loop()
{
    Serial.begin(115200);

    btn->loop();

    // Update Clock every second
    if ((millis() - loopMillis) > 1000)
    {
        loopMillis = millis();
        // Partial refresh
        mainPage(false);
    }

    // Update values on screen every 10 minutes
    if ((millis() - lastTime) > timerDelay)
    {
        // Check WiFi connection status
        if (WiFi.status() == WL_CONNECTED)
        {
            latestPrice = getPrice();
            lastBlockHeight = getBlockHeight();
            medianFeeRate = getMedianFeeRate();
            mainPage(true);
            Serial.println("Updated");
        }
        else
        {
            Serial.println(WiFi.status());
            WiFi.disconnect(true);
            initWiFi();
        }
        lastTime = millis();
    }
}

const unsigned char bitcoinIcon[512] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1f, 0xfc, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0xff, 0xff, 0xc0, 0x00, 0x00,
    0x00, 0x00, 0x07, 0xff, 0xff, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x1f, 0xff, 0xff, 0xfc, 0x00, 0x00,
    0x00, 0x00, 0x3f, 0xff, 0xff, 0xff, 0x00, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00,
    0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00,
    0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00,
    0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00,
    0x00, 0x0f, 0xff, 0xfe, 0x3f, 0xff, 0xf8, 0x00,
    0x00, 0x1f, 0xff, 0xfe, 0x39, 0xff, 0xfc, 0x00,
    0x00, 0x3f, 0xff, 0xfe, 0x38, 0xff, 0xfe, 0x00,
    0x00, 0x3f, 0xff, 0x3c, 0x31, 0xff, 0xfe, 0x00,
    0x00, 0x7f, 0xff, 0x00, 0x71, 0xff, 0xff, 0x00,
    0x00, 0x7f, 0xfe, 0x00, 0x11, 0xff, 0xff, 0x00,
    0x00, 0xff, 0xff, 0x80, 0x01, 0xff, 0xff, 0x80,
    0x00, 0xff, 0xff, 0xe0, 0x00, 0x7f, 0xff, 0x80,
    0x01, 0xff, 0xff, 0xe0, 0x00, 0x1f, 0xff, 0xc0,
    0x01, 0xff, 0xff, 0xe0, 0xf0, 0x0f, 0xff, 0xc0,
    0x01, 0xff, 0xff, 0xc0, 0xfc, 0x0f, 0xff, 0xc0,
    0x03, 0xff, 0xff, 0xc0, 0xfe, 0x07, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0xc1, 0xfe, 0x07, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0xc1, 0xfe, 0x07, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x81, 0xfc, 0x07, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x80, 0x38, 0x0f, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x80, 0x00, 0x0f, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x80, 0x00, 0x3f, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x03, 0x80, 0x3f, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x03, 0xf0, 0x1f, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x07, 0xf8, 0x0f, 0xff, 0xe0,
    0x03, 0xff, 0xff, 0x07, 0xfc, 0x0f, 0xff, 0xe0,
    0x03, 0xff, 0xfe, 0x07, 0xfc, 0x0f, 0xff, 0xe0,
    0x03, 0xff, 0xe0, 0x07, 0xfc, 0x0f, 0xff, 0xe0,
    0x01, 0xff, 0xe0, 0x07, 0xf8, 0x0f, 0xff, 0xc0,
    0x01, 0xff, 0xc0, 0x00, 0x00, 0x0f, 0xff, 0xc0,
    0x01, 0xff, 0xf8, 0x00, 0x00, 0x1f, 0xff, 0xc0,
    0x00, 0xff, 0xff, 0x00, 0x00, 0x1f, 0xff, 0x80,
    0x00, 0xff, 0xff, 0x18, 0x00, 0x3f, 0xff, 0x80,
    0x00, 0x7f, 0xff, 0x1c, 0x00, 0xff, 0xff, 0x00,
    0x00, 0x7f, 0xff, 0x18, 0x7f, 0xff, 0xff, 0x00,
    0x00, 0x3f, 0xfe, 0x18, 0xff, 0xff, 0xfe, 0x00,
    0x00, 0x3f, 0xff, 0x38, 0xff, 0xff, 0xfe, 0x00,
    0x00, 0x1f, 0xff, 0xf8, 0xff, 0xff, 0xfc, 0x00,
    0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xf8, 0x00,
    0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xf0, 0x00,
    0x00, 0x03, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00,
    0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xc0, 0x00,
    0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00,
    0x00, 0x00, 0x3f, 0xff, 0xff, 0xfe, 0x00, 0x00,
    0x00, 0x00, 0x1f, 0xff, 0xff, 0xfc, 0x00, 0x00,
    0x00, 0x00, 0x07, 0xff, 0xff, 0xf0, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xff, 0xff, 0x80, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x1f, 0xfc, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
