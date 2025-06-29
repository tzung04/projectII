#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include <base64.h>
#include <WiFiManager.h>
#include <HTTPClient.h>  
#include "esp_http_server.h"

#include "index_html.h"
#include "tools.h"
#include "secret.h"



WiFiClientSecure clientTCP;
String url_to_signal;

// GPIOs
#define LOCK_PIN     12
#define BUTTON_PIN   13
#define LED_PIN      4

// Camera config (AI Thinker module)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

int lockState = 0;
String msg="";
String img_url="";
bool buttonPressed = false;
unsigned long lastCheck = 0;
const unsigned long checkInterval = 1000;

bool isStreaming = false;
volatile bool stream_paused = false;
unsigned long activeClients = 0;
unsigned long lastClientLeft = 0;


void setup() {
  Serial.begin(115200);

  randomSeed(esp_random());
  initCmdTopic();

  pinMode(LOCK_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LOCK_PIN, LOW); 
  digitalWrite(LED_PIN, LOW);


  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.resetSettings();
  bool res;
  res = wm.autoConnect("ESP32_AP","password");
  if(!res){
    Serial.println("Failed to connect");
  }else{
    Serial.println("Connected!");
    Serial.print("wifi address: ");
    Serial.println(WiFi.localIP());
  }


  // Camera config
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 5;  //0-63 lower number means higher quality
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;  //0-63 lower number means higher quality
    config.fb_count = 1;
  }
  
  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  // Drop down frame size for higher initial frame rate
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);
  s->set_quality(s, 15);

  // send notification
  msg = "ESP32 is ready!\n";
  msg += "Send command on:\n";
  msg += CMD_TOPIC;
  msg += "\nList commands:\nUnlock, Lock, Takephoto, Startstream, Resettopic.";

  message_to_signal(msg); 
}
// control the door
String unlockDoor() {
  if(lockState == 0){
    digitalWrite(LOCK_PIN, HIGH);
    lockState = 1;
    Serial.println("Door unlocked!");
    message_to_signal("Door unlocked!");
    return "Door unlocked!";
  }else{
    Serial.println("Door already unlock!");
    message_to_signal("Door already unlock!");
    return "Door already unlock!";
  }
}
String lockDoor() {
  if(lockState == 1){
    digitalWrite(LOCK_PIN, LOW);
    lockState = 0;
    Serial.println("Door locked!");
    message_to_signal("Door locked!");
    return "Door locked!";
  }else{
    Serial.println("Door already lock!");
    message_to_signal("Door already lock!");
    return "Door already lock!";
  }
}

// ntfy.sh
String getCommand() {
  HTTPClient http;
  String cmd_url = CMD_TOPIC + "/raw";
  http.begin(cmd_url);
  int code = http.GET();
  String cmd = "";
  if (code == 200) {
    cmd = http.getString();
    cmd.trim();
    if (cmd != "") Serial.println("Command: " + cmd);
  }
  http.end();
  return cmd;
}

void handleCmd(String str){
  if (str == "Unlock") {
    unlockDoor();
  } else if (str == "Lock") {
    lockDoor();
  } else if (str == "Takephoto") {
    img_url = uploadPhoto();
    image_to_signal(img_url);
    img_url = "";
  } else if (str == "Resettopic"){
    msg = "ESP32 will reset to reset cmd topic!";
    message_to_signal(msg);
    resetCmdTopic();
  } else if (str == "Startstream"){
      if (!isStreaming) {               
        startServer();
        isStreaming = true;
      }
    msg = "Start stream at:\nhttp://";
    msg += WiFi.localIP().toString();
    msg += "/";
    message_to_signal(msg);
  } else if (str != ""){
    message_to_signal("Wrong command! List commands:\nUnlock, Lock, Takephoto, Startstream, Resettopic.");
  }
}

// ImgBB
String uploadPhoto(){

  stream_paused = true; // pause stream
  delay(100);  

  String getAll = "";
  String getBody = "";
  String imageUrl = "";

  camera_fb_t * fb = NULL;
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  for (int i = 0; i < 3; i++) {
  camera_fb_t *temp_fb = esp_camera_fb_get();
  if (temp_fb) esp_camera_fb_return(temp_fb);
  delay(100);
  }
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    digitalWrite(LED_PIN, LOW);
    delay(1000);
    ESP.restart();
    return "Camera capture failed";
  }
  delay(500);
  digitalWrite(LED_PIN, LOW);
  Serial.println("Connect to api.imgbb.com");
  clientTCP.setInsecure(); 
  clientTCP.setTimeout(10000);

  if (clientTCP.connect("api.imgbb.com", 443)) {
    Serial.println("Connection successful");
    
    String head = "--dungdapchai\r\nContent-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--dungdapchai--\r\n";

    uint16_t imageLen = fb->len;
    uint16_t extraLen = head.length() + tail.length();
    uint16_t totalLen = imageLen + extraLen;

    Serial.printf("imageLen:%d - extraLen: %d - totalLen: %d\n", imageLen, extraLen, totalLen);
  
    clientTCP.println("POST /1/upload?expiration=600&key="+String(imgbb_api_key)+" HTTP/1.1");
    clientTCP.println("Host: api.imgbb.com");
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=dungdapchai");
    clientTCP.println();
    clientTCP.print(head);
  
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    Serial.printf("fbLen: %d\n", fbLen);
    int count = 0;
    for (size_t n=0;n<fbLen;n=n+1024) {
      count++;
      Serial.printf("Sending part %d\n", count);
      if (n+1024<fbLen) {
        clientTCP.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen%1024>0) {
        size_t remainder = fbLen%1024;
        clientTCP.write(fbBuf, remainder);
      }
    }
    
    clientTCP.print(tail);
    
    esp_camera_fb_return(fb);
    
    delay(100);
    stream_paused = false; // continue stream

    unsigned long now = millis();
    boolean state = false;
    
    while (millis() - now < 10000){ 
      Serial.print(".");
      delay(100);      
      while (clientTCP.available()){
          char c = clientTCP.read();
          if (c == '\n'){
            if (getAll.length()==0) state=true; 
            getAll = "";
          } 
          else if (c != '\r'){
            getAll += String(c);
          }
          if (state==true){
            getBody += String(c);
          }
       }
       if (getBody.length()>0) break;
    }
    clientTCP.stop();
    int index = getBody.indexOf("i.ibb");// image url include i.ibb
    if (index >= 0) {
      int end = getBody.indexOf("\"", index);
      if (end > index) {
        String tmp = getBody.substring(index, end);
        imageUrl = "https://" + tmp;
      } else {
        Serial.println("Not foung end point!");
      }
    } else {
      Serial.println("Not found URL in JSON");
    }
    imageUrl.replace("\\", "");
    Serial.print("Image URL: ");
    Serial.println(imageUrl);
    getBody = imageUrl;

  }
  else {
    getBody="Connected to api.imgbb.com failed.";
    Serial.println("Connected to api.imgbb.com failed.");
  }
  return getBody;
}
// Signal app
void  message_to_signal(String message)    
{ 
  url_to_signal = header +"&text="+ urlencode(message);
  postData(); 
}

void image_to_signal(String img_url1) 
{
  url_to_signal = header + "&image=" + urlencode(img_url1);
  postData(); 
}

void postData()     
{
  int httpCode;    
  HTTPClient http; 
  http.begin(url_to_signal);  
  httpCode = http.POST(url_to_signal); 
  if (httpCode == 200)     
  {
    Serial.println("Sent ok."); 
  }
  else                     
  {
    Serial.println("Error."); 
  }
  http.end();      
}

void loop() {
  if(digitalRead(BUTTON_PIN) == LOW && buttonPressed == false){
    buttonPressed = true;
    msg = "Someone at the door!\n";
    msg += "Click on to action:\n";
    msg += CMD_TOPIC;
    msg += "List commands:\nUnlock, Lock, Takephoto, Startstream, Resettopic.";
    message_to_signal(msg);
    img_url = uploadPhoto();
    image_to_signal(img_url);
    img_url = "";
  }
  if(digitalRead(BUTTON_PIN) == HIGH){
    buttonPressed = false;
  }
  // wait and process command from ntfy.sh
  if (millis() - lastCheck > checkInterval) {
    String cmd = getCommand();
    handleCmd(cmd);
    lastCheck = millis();
  }
  if (isStreaming && activeClients == 0 &&
    millis() - lastClientLeft > 30000) {
    stopServer();
    isStreaming = false;
  }
}
// handle stream
httpd_handle_t stream_httpd = NULL;

esp_err_t stream_handler(httpd_req_t *req) {
  activeClients++;
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];

  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) return res;

  while (true) {
    if (stream_paused) {
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      continue;
    }

    size_t hlen = snprintf(part_buf, sizeof(part_buf),
                       "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                       fb->len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, "\r\n", 2);
    }

    esp_camera_fb_return(fb);
    if (res != ESP_OK || httpd_req_to_sockfd(req) == -1) break;

    vTaskDelay(66 / portTICK_PERIOD_MS); // 15fps
  }

  activeClients--;
  if (activeClients == 0) lastClientLeft = millis();
  return res;
}

void startServer() {
  lastClientLeft = millis();
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = [](httpd_req_t *req) {
      httpd_resp_set_type(req, "text/html");
      httpd_resp_send(req, index_html, strlen(index_html));
      return ESP_OK;
    }
  };

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler
  };


  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }

  Serial.println("Webserver started");
}

void stopServer() {
  if (stream_httpd) {
    httpd_stop(stream_httpd);
    stream_httpd = NULL;
    Serial.println("Webserver stopped");
  }
}
