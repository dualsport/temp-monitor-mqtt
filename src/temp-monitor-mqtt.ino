#include "Particle.h"
#include "Adafruit_DHT_Particle.h"
#include "MQTT.h"

char program_name[] = "particle-temp-monitor-dht22-mqtt";
String device_id = System.deviceID();

// Buffersizes for storing credentials in EEPROM
// Null byte at end reduces allowed string size by one
const int eeprom_start_addr = 0;
const int mqtt_server_buff_size = 64;
const int mqtt_username_buff_size = 32;
const int mqtt_password_buff_size = 32;

char mqtt_server[mqtt_server_buff_size];
char mqtt_username[mqtt_username_buff_size];
char mqtt_password[mqtt_password_buff_size];

const int mqtt_server_offset = eeprom_start_addr;
const int mqtt_username_offset = mqtt_server_offset + mqtt_server_buff_size;
const int mqtt_password_offset = mqtt_username_offset + mqtt_username_buff_size;

// Default repeat time in seconds
// Example 900 will repeat every 15 minutes at :00, :15, :30, :45
int log_period = 1800;
const int log_period_offset = mqtt_password_offset + mqtt_password_buff_size;

#define DHTPIN D4     // what pin we're connected to

// Uncomment whatever type you're using!
//#define DHTTYPE DHT11     // DHT 11 
#define DHTTYPE DHT22       // DHT 22 (AM2302)
//#define DHTTYPE DHT21     // DHT 21 (AM2301)

// Connect pin 1 (on the left) of the sensor to +3.3V
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor

DHT dht(DHTPIN, DHTTYPE);

// time sync interval in seconds
// simple interval, repeat every n seconds
const int sync_interval = 43200;
time_t next_sync;

time_t current_time;
time_t next_read = 0;
time_t last_publish = 0;

int attempts = 0;
int led1 = D7; //onboard led

// MQTT
void callback(char* topic, byte* payload, unsigned int length);
// mqtt_server is null at this point, setBroker in setup() after load_mqtt_config()
MQTT client(mqtt_server, 1883, callback);

// Below callback handles received messages
void callback(char* topic, byte* payload, unsigned int length) {
}

void setup() {
    load_mqtt_config();
    client.setBroker(mqtt_server, 1883);

    Particle.publish("status", "start", PRIVATE);
    Particle.publish("program_name", program_name, PRIVATE);
    Particle.publish("device_id", device_id.c_str(), PRIVATE);

    Particle.function("current_conditions", current);
    Particle.function("set_mqtt_server", set_mqtt_server);
    Particle.function("set_mqtt_useranme", set_mqtt_useranme);
    Particle.function("set_mqtt_password", set_mqtt_password);
    Particle.function("set log interval seconds" , set_log_interval);

    Particle.variable("Log period in seconds", log_period);
    Particle.variable("Program name", program_name);
    Particle.variable("Device ID", device_id.c_str());
    Particle.variable("MQTT Server", mqtt_server);
    //Particle.variable("MQTT Username", mqtt_username);
    //Particle.variable("MQTT Password", mqtt_password);

    // MQTT connect
    client.connect(device_id.c_str(), mqtt_username, mqtt_password);
    delay(50);
    // MQTT publish
    if (client.isConnected()) {
        client.publish(String::format("%s/message", device_id.c_str()),"MQTT Connected");
        String message = String::format("Succes for server %s", mqtt_server);
        Particle.publish("MQTT Connection Status", message, PRIVATE);
    }
    else {
        String message = String::format("Failed for server %s", mqtt_server);
        Particle.publish("MQTT Connection Status", message, PRIVATE);
    }

    dht.begin();

    pinMode(led1, OUTPUT);
    digitalWrite(led1, LOW);

    current_time = Time.now();
    next_read = current_time - (current_time % log_period) + log_period;
    next_sync = current_time + sync_interval;
    delay(2000);
}

void loop() {
    if (client.isConnected()) {
        client.loop();
    }
    if (Time.now() >= next_read) {
        current_time = Time.now();
        digitalWrite(led1, HIGH);
        // Reading temperature or humidity takes about 250 milliseconds!
        // Sensor readings may also be up to 2 seconds 'old'
        float t_c = dht.getTempCelcius();
        //float t_f = (t_c * 9 / 5) + 32;
        float dp_c = dht.getDewPoint();
        //float dp_f = (dp_c * 9 / 5) + 32;
        float h = dht.getHumidity();

        // Check if any reads failed and if so try again.
        if (isnan(h) || isnan(t_c) || isnan(dp_c)) {
            Particle.publish("status", "sensor read failed", PRIVATE);
            if (attempts < 3) {
                // Wait a moment then try reading again
                delay(5000);
                attempts++;
            }
            else {
                //give up, try again next log_period
                next_read = current_time - (current_time % log_period) + log_period;
            }
        }
        else {
            // publish readings
            mqtt_publish("temperature", t_c, "c");
            mqtt_publish("dewpoint", dp_c, "c");
            mqtt_publish("humidity", h, "pct");
        
            last_publish = current_time;
            next_read = current_time - (current_time % log_period) + log_period;
        }
    
        attempts = 0;
        digitalWrite(led1, LOW);
    }
    // sync time
    if(Time.now() >= next_sync) {
        current_time = Time.now();
        Particle.syncTime();
        delay(5000);
        if (Particle.syncTimePending()) {
            Particle.publish("status", "time sync failed", PRIVATE);
        }
        else {
            current_time = Time.now();
            next_sync = current_time + sync_interval;
            Particle.publish("status", "time sync success", PRIVATE);
        }
    }
}

void mqtt_publish(const char *metric, float value, const char *unit) {
    if (!client.isConnected()) {
        client.connect(device_id.c_str(), mqtt_username, mqtt_password);
        delay(50);
    }
    if (client.isConnected()) {
        client.publish(String::format("%s/readings/%s", device_id.c_str(), metric),
                       String::format("{\"data\":{\"value\":%4.2f,\"unit\":\"%s\"}}", value, unit));
    }
    else {
        Particle.publish("status", "Unable to publish to MQTT server - disconnected.", PRIVATE);
    }
}

void load_mqtt_config() {
    {
        char stringBuf[mqtt_server_buff_size];
        EEPROM.get(mqtt_server_offset, stringBuf);
        stringBuf[sizeof(stringBuf) - 1] = 0; // make sure it's null terminated
        for (int i = 0; i < sizeof(stringBuf); i++) mqtt_server[i] = stringBuf[i];
        client.setBroker(mqtt_server, 1883);
    }
    {
        char stringBuf[mqtt_username_buff_size];
        EEPROM.get(mqtt_username_offset, stringBuf);
        stringBuf[sizeof(stringBuf) - 1] = 0;
        for (int i = 0; i < sizeof(stringBuf); i++) mqtt_username[i] = stringBuf[i];
    }
    {
        char stringBuf[mqtt_password_buff_size];
        EEPROM.get(mqtt_password_offset, stringBuf);
        stringBuf[sizeof(stringBuf) - 1] = 0;
        for (int i = 0; i < sizeof(stringBuf); i++) mqtt_password[i] = stringBuf[i];
    }
    {
        int period;
        EEPROM.get(log_period_offset, period);
        if(period > 0) {
            log_period = period;
            // reset next log time
            next_read = current_time - (current_time % log_period) + log_period;
        }
    }
}

int current(String unit) {
    digitalWrite(led1, HIGH);
    String result = "Invalid unit given. Allowed units are 'c' or 'f' for celsius or fahrenheit.";
    if (unit == "c") {
        float t_c = dht.getTempCelcius();
        float dp_c = dht.getDewPoint();
        float h = dht.getHumidity();
        result = String::format("{\"Temp_C\": %4.2f, \"DewPt_C\": %4.2f, \"RelHum\": %4.2f}", t_c, dp_c, h);
    }
    else if (unit == "f") {
        float t_f = dht.getTempFarenheit();
        float dp_c = dht.getDewPoint();
        float dp_f = (dp_c * 9 / 5) + 32;
        float h = dht.getHumidity();
        result = String::format("{\"Temp_F\": %4.2f, \"DewPt_F\": %4.2f, \"RelHum\": %4.2f}", t_f, dp_f, h);
    }
    Particle.publish("current_conditions", result, PRIVATE);
    digitalWrite(led1, LOW);
    return 1;
}
        
int set_mqtt_server(String new_mqtt_server) {
  int addr = mqtt_server_offset;
  char stringBuf[mqtt_server_buff_size];
  // getBytes handles truncating the string if it's longer than the buffer.
  new_mqtt_server.getBytes((unsigned char *)stringBuf, sizeof(stringBuf));
  EEPROM.put(addr, stringBuf);
  load_mqtt_config();
  return 1;
}

int set_mqtt_useranme(String new_user_name) {
  int addr = mqtt_username_offset;
  char stringBuf[mqtt_username_buff_size];
  new_user_name.getBytes((unsigned char *)stringBuf, sizeof(stringBuf));
  EEPROM.put(addr, stringBuf);
  load_mqtt_config();
  return 1;
}

int set_mqtt_password(String new_password) {
  int addr = mqtt_password_offset;
  char stringBuf[mqtt_password_buff_size];
  new_password.getBytes((unsigned char *)stringBuf, sizeof(stringBuf));
  EEPROM.put(addr, stringBuf);
  load_mqtt_config();
  return 1;
}

int set_log_interval(String str_interval) {
  int addr = log_period_offset;
  int interval = str_interval.toInt();
  if(interval > 0) {
    EEPROM.put(addr, interval);
    load_mqtt_config();
  }
  return interval;
}
