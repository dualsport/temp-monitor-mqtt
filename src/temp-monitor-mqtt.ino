#include "Particle.h"
#include "Adafruit_DHT_Particle.h"
#include "MQTT.h"

char mqtt_server[] = "mqtt-redcat.local";
char mqtt_user[] = "redcatiot";
char mqtt_pwd[] = "iaea123:)";
char program_name[] = "particle-temp-monitor-dht22-mqtt";
String device_id = System.deviceID();

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

// Repeat time in seconds
// Example 900 will repeat every 15 minutes at :00, :15, :30, :45
const int period = 60;

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
MQTT client(mqtt_server, 1883, callback);

// Below callback handles received messages
void callback(char* topic, byte* payload, unsigned int length) {
}

void setup() {
    Particle.publish("status", "start", PRIVATE);
    Particle.publish("program_name", program_name, PRIVATE);
    Particle.publish("device_id", device_id.c_str(), PRIVATE);
    Particle.function("current_conditions", current);
    Particle.function("program_name", publish_name);
    Particle.function("device_id", publish_device_id);

    Particle.variable("publish_period_seconds", period);
    Particle.variable("last_publish", last_publish);
    Particle.variable("next_publish", next_read);

    // MQTT connect
    client.connect(device_id.c_str(), mqtt_user, mqtt_pwd);
    delay(50);
    // MQTT publish
    if (client.isConnected()) {
        client.publish(String::format("%s/message", device_id.c_str()),"MQTT Startup");
        // client.subscribe("inTopic/message");
    }

    dht.begin();

    pinMode(led1, OUTPUT);
    digitalWrite(led1, LOW);

    current_time = Time.now();
    next_read = current_time - (current_time % period) + period;
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
            // Particle.publish("status", "read failed", PRIVATE);
            if (attempts < 3) {
                // Wait a moment then try reading again
                delay(5000);
                attempts++;
            }
            else {
                //give up, try again next period
                next_read = current_time - (current_time % period) + period;
            }
        }
        else {
            // publish readings
            mqtt_publish("temperature", t_c, "c");
            mqtt_publish("dewpoint", dp_c, "c");
            mqtt_publish("humidity", h, "pct");
        
            last_publish = current_time;
            next_read = current_time - (current_time % period) + period;
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
        client.connect(device_id.c_str(), mqtt_user, mqtt_pwd);
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
        
int publish_name(String args) {
    digitalWrite(led1, HIGH);
    Particle.publish("program_name", program_name, PRIVATE);
    digitalWrite(led1, LOW);
    return 1;
}

int publish_device_id(String args) {
    digitalWrite(led1, HIGH);
    Particle.publish("device_id", device_id.c_str(), PRIVATE);
    digitalWrite(led1, LOW);
    return 1;
}
