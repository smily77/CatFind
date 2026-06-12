#define hostName "LD06"
#define fireDuration 500

// allg. HW
#define extRelais 18
#define ext1 22
#define ext2 21 // Servo
#define lidarPwr 27
#define laser 4

// lidar
#define pwmPin 26
#define dataInPin 32
#define pwmFreq 30000
#define pwmSpeedChannel 0
#define pwmResolution 8
#define dataPointPerSentence 12 //const int
#define maxSentenceLength 46
struct messDaten{
 int quality;
 int dist;
 float winkel;
 float x;
 float y;
};
//Pixel
#define COLOR_ORDER GRB
#define pixelNum 29
#define pixelPin 23
#define maxPix 30
#define minPix 0
#define LED_TYPE WS2811
uint8_t BRIGHTNESS = 5;
