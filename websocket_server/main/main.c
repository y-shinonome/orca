/*
An example showing the ESP32 as a
WebSocket server.

Demonstrates:
the ESP32 as a WiFi Access Point,
embedding files (html, js, etc.) for a server,
WebSockets,
LED control.

All example options are in "Example Options"

All WebSocket Server options are in:
Component config ---> WebSocket Server

Connect an LED to pin 2 (default)
Connect to the Access Point,
default name: "ESP32 Test"
password: "hello world"

go to 192.168.4.1 in a browser

Note that there are two regular server tasks.
The first gets incoming clients, then passes them
to a queue for the second task to actually handle.
I found that connections were dropped much less frequently
this way, especially when handling large requests. It
does use more RAM though.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "lwip/api.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "esp_adc_cal.h"
#include "nvs_flash.h"
#include "driver/ledc.h"

#include "string.h"
#include "math.h"

#include "websocket_server.h"

#define LED_PIN CONFIG_LED_PIN
#define AP_SSID CONFIG_AP_SSID
#define AP_PSSWD CONFIG_AP_PSSWD
#define DEFAULT_VREF 1100

static QueueHandle_t client_queue;
const static int client_queue_size = 10;

static ledc_channel_config_t motor_L;
static ledc_channel_config_t motor_R;
static ledc_channel_config_t LED;

// handles WiFi events
static esp_err_t event_handler(void* ctx, system_event_t* event) {
  const char* TAG = "event_handler";
  switch(event->event_id) {
    case SYSTEM_EVENT_AP_START:
      //ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "esp32"));
      ESP_LOGI(TAG,"Access Point Started");
      break;
    case SYSTEM_EVENT_AP_STOP:
      ESP_LOGI(TAG,"Access Point Stopped");
      break;
    case SYSTEM_EVENT_AP_STACONNECTED:
      ESP_LOGI(TAG,"STA Connected, MAC=%02x:%02x:%02x:%02x:%02x:%02x AID=%i",
        event->event_info.sta_connected.mac[0],event->event_info.sta_connected.mac[1],
        event->event_info.sta_connected.mac[2],event->event_info.sta_connected.mac[3],
        event->event_info.sta_connected.mac[4],event->event_info.sta_connected.mac[5],
        event->event_info.sta_connected.aid);
      break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
      ESP_LOGI(TAG,"STA Disconnected, MAC=%02x:%02x:%02x:%02x:%02x:%02x AID=%i",
        event->event_info.sta_disconnected.mac[0],event->event_info.sta_disconnected.mac[1],
        event->event_info.sta_disconnected.mac[2],event->event_info.sta_disconnected.mac[3],
        event->event_info.sta_disconnected.mac[4],event->event_info.sta_disconnected.mac[5],
        event->event_info.sta_disconnected.aid);
      break;
    case SYSTEM_EVENT_AP_PROBEREQRECVED:
      ESP_LOGI(TAG,"AP Probe Received");
      break;
    case SYSTEM_EVENT_AP_STA_GOT_IP6:
      ESP_LOGI(TAG,"Got IP6=%01x:%01x:%01x:%01x",
        event->event_info.got_ip6.ip6_info.ip.addr[0],event->event_info.got_ip6.ip6_info.ip.addr[1],
        event->event_info.got_ip6.ip6_info.ip.addr[2],event->event_info.got_ip6.ip6_info.ip.addr[3]);
      break;
    default:
      ESP_LOGI(TAG,"Unregistered event=%i",event->event_id);
      break;
  }
  return ESP_OK;
}

// sets up WiFi
static void wifi_setup() {
  const char* TAG = "wifi_setup";

  ESP_LOGI(TAG,"starting tcpip adapter");
  tcpip_adapter_init();
  nvs_flash_init();
  ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
  //tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_AP,"esp32");
  tcpip_adapter_ip_info_t info;
  memset(&info, 0, sizeof(info));
  IP4_ADDR(&info.ip, 192, 168, 4, 1);
  IP4_ADDR(&info.gw, 192, 168, 4, 1);
  IP4_ADDR(&info.netmask, 255, 255, 255, 0);
  ESP_LOGI(TAG,"setting gateway IP");
  ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
  //ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_AP,"esp32"));
  //ESP_LOGI(TAG,"set hostname to \"%s\"",hostname);
  ESP_LOGI(TAG,"starting DHCPS adapter");
  ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
  //ESP_ERROR_CHECK(tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_AP,hostname));
  ESP_LOGI(TAG,"starting event loop");
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

  ESP_LOGI(TAG,"initializing WiFi");
  wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

  wifi_config_t wifi_config = {
    .ap = {
      .ssid = AP_SSID,
      .password= AP_PSSWD,
      .channel = 0,
      .authmode = WIFI_AUTH_WPA2_PSK,
      .ssid_hidden = 0,
      .max_connection = 4,
      .beacon_interval = 100
    }
  };

  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG,"WiFi set up");
}

static void motor_L_duty(uint16_t duty) {
  static uint16_t val;
  static uint16_t max = (1L<<8)-1;
  if(duty > 100) return;
  val = (duty * max) / 100;
  ledc_set_duty(motor_L.speed_mode,motor_L.channel,val);
  ledc_update_duty(motor_L.speed_mode,motor_L.channel);
}

static void motor_R_duty(uint16_t duty) {
  static uint16_t val;
  static uint16_t max = (1L<<8)-1;
  if(duty > 100) return;
  val = (duty * max) / 100;
  ledc_set_duty(motor_R.speed_mode,motor_R.channel,val);
  ledc_update_duty(motor_R.speed_mode,motor_R.channel);
}

static void LED_duty(uint16_t duty) {
  static uint16_t val;
  static uint16_t max = (1L<<8)-1;
  if(duty > 100) return;
  val = (duty * max) / 100;
  ledc_set_duty(LED.speed_mode,LED.channel,val);
  ledc_update_duty(LED.speed_mode,LED.channel);
}

static void pwm_setup() {
  const static char* TAG = "pwm_setup";

  ledc_timer_config_t ledc_timer = {
    .duty_resolution = LEDC_TIMER_8_BIT,
    .freq_hz         = 400,
    .speed_mode      = LEDC_HIGH_SPEED_MODE,
    .timer_num       = LEDC_TIMER_0
  };

  motor_L.channel = LEDC_CHANNEL_0;
  motor_L.duty = 40;
  motor_L.gpio_num = 12,
  motor_L.speed_mode = LEDC_HIGH_SPEED_MODE;
  motor_L.timer_sel = LEDC_TIMER_0;

  motor_R.channel = LEDC_CHANNEL_1;
  motor_R.duty = 40;
  motor_R.gpio_num = 13,
  motor_R.speed_mode = LEDC_HIGH_SPEED_MODE;
  motor_R.timer_sel = LEDC_TIMER_0;

  LED.channel = LEDC_CHANNEL_2;
  LED.duty = 0;
  LED.gpio_num = 17,
  LED.speed_mode = LEDC_HIGH_SPEED_MODE;
  LED.timer_sel = LEDC_TIMER_0;

  ledc_timer_config(&ledc_timer);
  ledc_channel_config(&motor_L);
  ledc_channel_config(&motor_R);
  ledc_channel_config(&LED);

  motor_L_duty(40);
  motor_R_duty(40);
  LED_duty(0);
  ESP_LOGI(TAG,"led is off and ready, 10 bits");
}

// handles websocket events
void websocket_callback(uint8_t num,WEBSOCKET_TYPE_t type,char* msg,uint64_t len) {
  const static char* TAG = "websocket_callback";
  int L_duty_value;
  int R_duty_value;
  int LED_duty_value;

  switch(type) {
    case WEBSOCKET_CONNECT:
      ESP_LOGI(TAG,"client %i connected!",num);
      break;
    case WEBSOCKET_DISCONNECT_EXTERNAL:
      ESP_LOGI(TAG,"client %i sent a disconnect message",num);
      motor_L_duty(40);
      motor_R_duty(40);
      LED_duty(0);
      break;
    case WEBSOCKET_DISCONNECT_INTERNAL:
      ESP_LOGI(TAG,"client %i was disconnected",num);
      break;
    case WEBSOCKET_DISCONNECT_ERROR:
      ESP_LOGI(TAG,"client %i was disconnected due to an error",num);
      motor_L_duty(40);
      motor_R_duty(40);
      LED_duty(0);
      break;
    case WEBSOCKET_TEXT:
      if(len) { // if the message length was greater than zero
        switch(msg[0]) {
          case 'M':
            if(sscanf(msg,"M%i:%i",&L_duty_value,&R_duty_value)) {
              // ESP_LOGI(TAG,"LED value: %i : %i",L,R);
              motor_L_duty(L_duty_value);
              motor_R_duty(R_duty_value);
              ws_server_send_text_all_from_callback(msg,len); // broadcast it!
            }
            break;
          case 'L':
            if(sscanf(msg,"L%i",&LED_duty_value)) {
              ESP_LOGI(TAG,"LED value: %i",LED_duty_value);
              LED_duty(LED_duty_value);
              ws_server_send_text_all_from_callback(msg,len); // broadcast it!
            }
            break;
          default:
	          ESP_LOGI(TAG, "got an unknown message with length %i", (int)len);
            printf(msg);
	          break;
        }
      }
      break;
    case WEBSOCKET_BIN:
      ESP_LOGI(TAG,"client %i sent binary message of size %i:\n%s",num,(uint32_t)len,msg);
      break;
    case WEBSOCKET_PING:
      ESP_LOGI(TAG,"client %i pinged us with message of size %i:\n%s",num,(uint32_t)len,msg);
      break;
    case WEBSOCKET_PONG:
      ESP_LOGI(TAG,"client %i responded to the ping",num);
      break;
  }
}

// serves any clients
static void http_serve(struct netconn *conn) {
  const static char* TAG = "http_server";
  const static char HTML_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/html\n\n";
  const static char ERROR_HEADER[] = "HTTP/1.1 404 Not Found\nContent-type: text/html\n\n";
  const static char JS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/javascript\n\n";
  const static char CSS_HEADER[] = "HTTP/1.1 200 OK\nContent-type: text/css\n\n";
  //const static char PNG_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/png\n\n";
  const static char ICO_HEADER[] = "HTTP/1.1 200 OK\nContent-type: image/x-icon\n\n";
  //const static char PDF_HEADER[] = "HTTP/1.1 200 OK\nContent-type: application/pdf\n\n";
  //const static char EVENT_HEADER[] = "HTTP/1.1 200 OK\nContent-Type: text/event-stream\nCache-Control: no-cache\nretry: 3000\n\n";
  struct netbuf* inbuf;
  static char* buf;
  static uint16_t buflen;
  static err_t err;

  // default page
  extern const uint8_t index_html_start[] asm("_binary_index_html_start");
  extern const uint8_t index_html_end[] asm("_binary_index_html_end");
  const uint32_t index_html_len = index_html_end - index_html_start;

  // main.js
  extern const uint8_t main_js_start[] asm("_binary_main_js_start");
  extern const uint8_t main_js_end[] asm("_binary_main_js_end");
  const uint32_t main_js_len = main_js_end - main_js_start;

  // index.css
  extern const uint8_t index_css_start[] asm("_binary_index_css_start");
  extern const uint8_t index_css_end[] asm("_binary_index_css_end");
  const uint32_t index_css_len = index_css_end - index_css_start;

  // favicon.ico
  extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
  extern const uint8_t favicon_ico_end[] asm("_binary_favicon_ico_end");
  const uint32_t favicon_ico_len = favicon_ico_end - favicon_ico_start;

  // error page
  extern const uint8_t error_html_start[] asm("_binary_error_html_start");
  extern const uint8_t error_html_end[] asm("_binary_error_html_end");
  const uint32_t error_html_len = error_html_end - error_html_start;

  netconn_set_recvtimeout(conn,1000); // allow a connection timeout of 1 second
  ESP_LOGI(TAG,"reading from client...");
  err = netconn_recv(conn, &inbuf);
  ESP_LOGI(TAG,"read from client");
  if(err==ERR_OK) {
    netbuf_data(inbuf, (void**)&buf, &buflen);
    if(buf) {

      // default page
      if     (strstr(buf,"GET / ")
          && !strstr(buf,"Upgrade: websocket")) {
        ESP_LOGI(TAG,"Sending /");
        netconn_write(conn, HTML_HEADER, sizeof(HTML_HEADER)-1,NETCONN_NOCOPY);
        netconn_write(conn, index_html_start,index_html_len,NETCONN_NOCOPY);
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
      }

      // default page websocket
      else if(strstr(buf,"GET / ")
           && strstr(buf,"Upgrade: websocket")) {
        ESP_LOGI(TAG,"Requesting websocket on /");
        ws_server_add_client(conn,buf,buflen,"/",websocket_callback);
        netbuf_delete(inbuf);
      }

      else if(strstr(buf,"GET /main.js ")) {
        ESP_LOGI(TAG,"Sending /main.js");
        netconn_write(conn, JS_HEADER, sizeof(JS_HEADER)-1,NETCONN_NOCOPY);
        netconn_write(conn, main_js_start, main_js_len,NETCONN_NOCOPY);
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
      }

      else if(strstr(buf,"GET /index.css ")) {
        ESP_LOGI(TAG,"Sending /index.css");
        netconn_write(conn, CSS_HEADER, sizeof(CSS_HEADER)-1,NETCONN_NOCOPY);
        netconn_write(conn, index_css_start, index_css_len,NETCONN_NOCOPY);
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
      }

      else if(strstr(buf,"GET /favicon.ico ")) {
        ESP_LOGI(TAG,"Sending favicon.ico");
        netconn_write(conn,ICO_HEADER,sizeof(ICO_HEADER)-1,NETCONN_NOCOPY);
        netconn_write(conn,favicon_ico_start,favicon_ico_len,NETCONN_NOCOPY);
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
      }

      else if(strstr(buf,"GET /")) {
        ESP_LOGI(TAG,"Unknown request, sending error page: %s",buf);
        netconn_write(conn, ERROR_HEADER, sizeof(ERROR_HEADER)-1,NETCONN_NOCOPY);
        netconn_write(conn, error_html_start, error_html_len,NETCONN_NOCOPY);
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
      }

      else {
        ESP_LOGI(TAG,"Unknown request");
        netconn_close(conn);
        netconn_delete(conn);
        netbuf_delete(inbuf);
      }
    }
    else {
      ESP_LOGI(TAG,"Unknown request (empty?...)");
      netconn_close(conn);
      netconn_delete(conn);
      netbuf_delete(inbuf);
    }
  }
  else { // if err==ERR_OK
    ESP_LOGI(TAG,"error on read, closing connection");
    netconn_close(conn);
    netconn_delete(conn);
    netbuf_delete(inbuf);
  }
}

// handles clients when they first connect. passes to a queue
static void server_task(void* pvParameters) {
  const static char* TAG = "server_task";
  struct netconn *conn, *newconn;
  static err_t err;
  client_queue = xQueueCreate(client_queue_size,sizeof(struct netconn*));

  conn = netconn_new(NETCONN_TCP);
  netconn_bind(conn,NULL,80);
  netconn_listen(conn);
  ESP_LOGI(TAG,"server listening");
  do {
    err = netconn_accept(conn, &newconn);
    ESP_LOGI(TAG,"new client");
    if(err == ERR_OK) {
      xQueueSendToBack(client_queue,&newconn,portMAX_DELAY);
      //http_serve(newconn);
    }
  } while(err == ERR_OK);
  netconn_close(conn);
  netconn_delete(conn);
  ESP_LOGE(TAG,"task ending, rebooting board");
  esp_restart();
}

// receives clients from queue, handles them
static void server_handle_task(void* pvParameters) {
  const static char* TAG = "server_handle_task";
  struct netconn* conn;
  ESP_LOGI(TAG,"task starting");
  for(;;) {
    xQueueReceive(client_queue,&conn,portMAX_DELAY);
    if(!conn) continue;
    http_serve(conn);
  }
  vTaskDelete(NULL);
}

// static void count_task(void* pvParameters) {
//   const static char* TAG = "count_task";
//   char out[20];
//   int len;
//   int clients;
//   const static char* word = "%i";
//   uint8_t n = 0;
//   const int DELAY = 1000 / portTICK_PERIOD_MS; // 1 second

//   ESP_LOGI(TAG,"starting task");
//   for(;;) {
//     len = sprintf(out,word,n);
//     clients = ws_server_send_text_all(out,len);
//     if(clients > 0) {
//       //ESP_LOGI(TAG,"sent: \"%s\" to %i clients",out,clients);
//     }
//     n++;
//     vTaskDelay(DELAY);
//   }
// }

static void batteryVoltageMeasurement_task(void* pvParameters) {
  const int DELAY = 200 / portTICK_PERIOD_MS;
  static const adc_unit_t unit = ADC_UNIT_1;
  static const adc_channel_t channel = ADC_CHANNEL_0;
  static const adc_atten_t atten = ADC_ATTEN_DB_6;
  static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

  adc1_config_width(width);
  adc1_config_channel_atten(channel, atten);

  esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);

  for(;;) {
    uint32_t adcReading = 0;
    int sampleCount = 0;
    uint32_t current_adc;
    for (int i = 0; i < 16; i++) {
      current_adc = 0;
      current_adc = adc1_get_raw((adc1_channel_t)channel);
      if(current_adc > 0) {
        adcReading += current_adc;
        sampleCount += 1;
      }
      vTaskDelay(2/portTICK_PERIOD_MS);
    }

    if(sampleCount > 0) {
      adcReading /= sampleCount;
    }

    uint32_t voltage = esp_adc_cal_raw_to_voltage(adcReading, adc_chars);
    uint32_t batteryVoltage = voltage * 5.168;

    char msg[10];
    sprintf(msg, "V%d", batteryVoltage);

    int len =strlen(msg);
    
    int clients = ws_server_send_text_all(msg, len);
    if(clients > 0) {
      //ESP_LOGI(TAG,"sent: \"%s\" to %i clients",out,clients);
    }
    vTaskDelay(DELAY);
  }
}

static void motorCurrentMeasurement_task(void* pvParameters) {
  const int DELAY = 100 / portTICK_PERIOD_MS;
  static const adc_unit_t unit = ADC_UNIT_1;
  static const adc_channel_t motor_L = ADC_CHANNEL_3;
  static const adc_channel_t motor_R = ADC_CHANNEL_5;
  static const adc_atten_t atten = ADC_ATTEN_DB_11;
  static const adc_bits_width_t width = ADC_WIDTH_BIT_12;

  adc1_config_width(width);
  adc1_config_channel_atten(motor_L, atten);
  adc1_config_channel_atten(motor_R, atten);

  esp_adc_cal_characteristics_t *adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);

  for(;;) {
    uint32_t adcReading_L = 0;
    uint32_t adcReading_R = 0;
    int sampleCount_L = 0;
    int sampleCount_R = 0;
    uint32_t currentAdcReading_L;
    uint32_t currentAdcReading_R;

    for (int i = 0; i < 64; i++) {
      currentAdcReading_L = 0;
      currentAdcReading_R = 0;

      currentAdcReading_L = adc1_get_raw((adc1_channel_t)motor_L);
      currentAdcReading_R = adc1_get_raw((adc1_channel_t)motor_R);

      if(currentAdcReading_L > 0) {
        adcReading_L += currentAdcReading_L;
        sampleCount_L += 1;
      }
      if(currentAdcReading_R > 0) {
        adcReading_R += currentAdcReading_R;
        sampleCount_R += 1;
      }
      vTaskDelay(2/portTICK_PERIOD_MS);
    }

    if(sampleCount_L > 0) {
      adcReading_L /=  sampleCount_L;
    }
    if(sampleCount_R > 0) {
      adcReading_R /=  sampleCount_R;
    }

    uint32_t voltage_L = esp_adc_cal_raw_to_voltage(adcReading_L, adc_chars);
    uint32_t amperage_L = round(voltage_L * 6800 / 2400);
    uint32_t voltage_R = esp_adc_cal_raw_to_voltage(adcReading_R, adc_chars);
    uint32_t amperage_R = round(voltage_R * 6800 / 2400);

    char msg[20];
    sprintf(msg, "A%d,%d", amperage_L, amperage_R);
    int len = strlen(msg);

    int clients = ws_server_send_text_all(msg, len);
    if(clients > 0) {
      //ESP_LOGI(TAG,"sent: \"%s\" to %i clients",out,clients);
    }
    vTaskDelay(DELAY);
  }
}

  void app_main() {
  wifi_setup();
  pwm_setup();
  ws_server_start();
  xTaskCreate(&server_task,"server_task",3000,NULL,9,NULL);
  xTaskCreate(&server_handle_task,"server_handle_task",4000,NULL,6,NULL);
  // xTaskCreate(&count_task,"count_task",6000,NULL,2,NULL);
  xTaskCreate(&batteryVoltageMeasurement_task,"batteryVoltageMeasurement_task",2000,NULL,1,NULL);
  xTaskCreate(&motorCurrentMeasurement_task,"motorCurrentMeasurement_task",2000,NULL,1,NULL);
}
