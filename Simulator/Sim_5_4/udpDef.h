IPAddress multiCastIP (239,0,0,57); 
constexpr uint16_t PORT = 8266; 

struct comMsgStruct {
  byte msgCode;
  int x;
  int y;
  float radius;
  float angle;
  int targetSpeed;
  int res;
  time_t timeStamp;
  byte sender;
  byte sensor;
};

#define HB 1
#define catObserved 2

#define testInfra  0
#define radarDome2Code 1
#define miniDomeCode 2
#define hlkS3W 3

#define CF3 14
