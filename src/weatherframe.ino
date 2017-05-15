#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <DoubleResetDetector.h>

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson


#define DRD_TIMEOUT 10
// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

// Accesspoint name for setup
#define SETUP_AP_NAME "Weatherframe"

// period of data refresh
#define DATA_REFRESH 10 * 60 * 1000
// When to consider old data expired
#define DATA_EXPIRES 2 * 3600 * 1000


DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

// ---
#include <Adafruit_NeoPixel.h>

// Neopixel data pin
#define NEOPIN            D1
#define NEOCONFIG         NEO_GRB + NEO_KHZ800
// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      4

#define ANIMDELAY      50

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, NEOPIN, NEOCONFIG);


//wunderground location see https://www.wunderground.com/weather/api/d/docs?d=autocomplete-api
char location[50] = "/q/zmw:00000.32.10384"; // Default Berlin, DE
char name[30]; 

uint32_t BLACK = pixels.Color(0,0,0), WHITE = pixels.Color(255,255,255),
  GRAY = pixels.Color(64,64,64), BLUE = pixels.Color(128,128,255), RED = pixels.Color(255,0,0),
  ORANGE = pixels.Color(255,128,0), YELLOW = pixels.Color(255,255,0);



//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}



void setup_wifi() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();

  //clean FS, for testing
  //SPIFFS.format();

  // default value
  sprintf(name, "Weatherframe-%d", ESP.getChipId());

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(location, json["location"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read


  // The extra parameters to be configured (can be either global or just in the setup)
  
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_name("name", "name", name, 50);
  WiFiManagerParameter custom_location("location", "location", location, 50);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_name);
  wifiManager.addParameter(&custom_location);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  bool configSuccess;
  if(drd.detectDoubleReset()) {
    for(int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, ORANGE); pixels.show();
    configSuccess = wifiManager.startConfigPortal(SETUP_AP_NAME, "");
  } else {
    for(int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, YELLOW); pixels.show();
    configSuccess = wifiManager.autoConnect(SETUP_AP_NAME, "");
  }
  if(!configSuccess)
    for(int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, RED); pixels.show();

  // fade result
  for(int i = 255; i>0; i--) {
    pixels.setBrightness(i);
    pixels.show();
    delay(10);
  }

  if (!configSuccess) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
  }

  for(int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(0, BLACK); pixels.show();
  pixels.setBrightness(200);

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(name, custom_name.getValue());
  strcpy(location, custom_location.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["location"] = location;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  MDNS.begin(name);

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

}

void setup() {
  setup_pixels();
  setup_wifi();
  setup_ota();
}

void loop() {
  // put your main code here, to run repeatedly:
  //handle_pixels();
  handle_drd();
  ArduinoOTA.handle();
  handle_weather();
  delay(1000);
}

void setup_pixels() {
  pixels.begin();
}

void handle_pixels() {
  // For a set of NeoPixels the first NeoPixel is 0, second is 1, all the way up to the count of pixels minus one.
  Serial.println("loop");
  uint32_t c[3] = {pixels.Color(255,0,0), pixels.Color(0,255,0), pixels.Color(0,0,255)};
  for (int j = 0;  j < sizeof(c)/sizeof(uint32_t); j++) {
      for (int i=0; i < NUMPIXELS; i++) {
        for(int r = 0; r < 10; r++) {
          for (int h=0; h < NUMPIXELS; h++) {
            pixels.setPixelColor(h, i == h ? c[j] : pixels.Color(0,0,0));
          }
          pixels.show();
          delay(ANIMDELAY);
        }
      }
    }
}

void pulse() {
  return;
  for(int j = 0; j < 3; j++) {
    for(int i = 0; i < 255; i++) {
      pixels.setBrightness(i);
      pixels.show();
      delay(10);
    }
    for(int i = 255; i > 0; i--) {
      pixels.setBrightness(i);
      pixels.show();
      delay(10);
    }
  }
}

unsigned long last_weather = -DATA_REFRESH;
uint32_t last_c1, last_c2;

void handle_weather() {
  if (millis() - last_weather < DATA_REFRESH) {
    return;
  }
  Serial.print("Refreshing forecast");
  Serial.print("Waiting for wifi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  // new colors
  uint32_t c1,c2;

  HTTPClient http;

  Serial.print("[HTTP] begin...\n");
  // configure traged server and url
  String url =String("http://api.wunderground.com/api/b729c988631690fa/forecast/q/") + location + ".json";
  Serial.print("Fetching forecast from "); Serial.println(url);
  http.begin(url);

  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();

  // HTTP header has been send and Server response header has been handled
  Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  // httpCode will be negative on error
  // file found at server
  if(httpCode == HTTP_CODE_OK) {
    //String payload = http.getString();
    DynamicJsonBuffer jsonBuffer;

    JsonObject& root = jsonBuffer.parseObject(http.getStream());

    JsonObject& forecast = root["forecast"]["simpleforecast"]["forecastday"][0];
    const char* weather = forecast["icon"];
    Serial.print("Weather for ");
    forecast["date"].printTo(Serial);
    Serial.println();
    String w = String(weather);

    // default is orange
    c1=ORANGE; c2=ORANGE;

    if (w.equals("chanceflurries")) {c1= BLACK; c2= WHITE;};
    if (w.equals("chancerain"))     {c1= BLACK; c2= BLUE;};
    if (w.equals("chancesleet"))  	{c1= BLACK; c2= GRAY;};
    if (w.equals("chancesnow"))	    {c1= BLACK; c2= WHITE;};
    if (w.equals("chancetstorms"))	{c1= BLUE;  c2= RED;};
    if (w.equals("clear"))          {c1=YELLOW; c2= BLUE;};
    if (w.equals("cloudy"))        	{c1= GRAY;  c2= BLUE;};
    if (w.equals("flurries"))	      {c1= WHITE; c2= WHITE;};
    if (w.equals("fog"))	          {c1= GRAY;  c2= GRAY;};
    if (w.equals("hazy"))	          {c1 =GRAY;  c2= GRAY;};
    if (w.equals("mostlycloudy"))	  {c1= GRAY;  c2= GRAY;};
    if (w.equals("mostlysunny"))	  {c1=YELLOW; c2= WHITE;};
    if (w.equals("partlycloudy"))	  {c1=YELLOW; c2= WHITE;};
    if (w.equals("partlysunny"))	  {c1= GRAY;  c2= YELLOW;};
    if (w.equals("rain"))	          {c1= BLUE;  c2= BLUE;};
    if (w.equals("sleet"))          {c1= WHITE; c2= GRAY;};
    if (w.equals("snow"))	          {c1= WHITE; c2= WHITE;};
    if (w.equals("sunny"))          {c1=YELLOW; c2=YELLOW;};
    if (w.equals("tstorms"))	      {c1= BLUE;  c2= RED;};

    Serial.print("Weather is ");
    Serial.println(weather);
  } else {
    Serial.printf("[HTTP] GET... failed, error: %d, %s\n", httpCode, http.errorToString(httpCode).c_str());
    pixels.setBrightness(0);
    for(int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, RED);
    }
    pulse();
    pixels.show();
    if(millis() - last_weather > DATA_EXPIRES) {
      c1 = BLACK;
      c2 = BLACK;
    } else {
      // keep old values
      c1 = last_c1;
      c2 = last_c2;
    }
  }

  // DEBUG
  Serial.print("C1 "); Serial.println(c1, HEX);
  Serial.print("C2 "); Serial.println(c2, HEX);

  for (int i = 0; i < NUMPIXELS; i++) {
    uint32_t c = i < NUMPIXELS/2 ? c1:c2;
    Serial.print("Setting pixel "); Serial.print(i); Serial.print(" to "); Serial.println(c, HEX);
    pixels.setPixelColor(i, c);
  }
  pixels.setBrightness(255);
  pixels.show();
  // TEST
  pulse();
  if(last_c1 != c1 || last_c2 != c2) {
    pulse();
  }
  pixels.setBrightness(255);
  pixels.show();


  http.end();
  last_weather = millis();
}

void handle_drd() {
  drd.loop();
}

void setup_ota() {
  ArduinoOTA.setHostname(name);
  ArduinoOTA.onStart([]() {
    String type = "unknown";
    /*
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
    */

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
}
