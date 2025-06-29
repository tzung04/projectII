#ifndef tools
#define tools

#include <Preferences.h>

String CMD_TOPIC = "";
Preferences preferences;

String generateRandomTopic(int length = 8) {
  const char* charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  String result = "";
  for (int i = 0; i < length; i++) {
    result += charset[random(0, strlen(charset))];
  }
  return result;
}

void initCmdTopic() {
  preferences.begin("config", false);

  if (preferences.isKey("topic")) {
    CMD_TOPIC = preferences.getString("topic");
    Serial.println("Loaded saved topic: " + CMD_TOPIC);
  } else {
    String suffix = generateRandomTopic();
    CMD_TOPIC = "https://ntfy.sh/esp32_" + suffix;
    preferences.putString("topic", CMD_TOPIC);
    Serial.println("New topic generated: " + CMD_TOPIC);
  }

  preferences.end();
}

void resetCmdTopic() {
  preferences.begin("config", false);
  preferences.remove("topic");
  preferences.end();
  ESP.restart();
}

// encode
String urlencode(String str) 
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
      }
      yield();
    }
    return encodedString;
}

#endif
