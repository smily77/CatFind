void writelnComment(String comment) {
  if (DEBUG) Serial << comment  <<  endl;
  //tft << comment << endl;
  //gfx -> println(comment);
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
  //tft << comment;
  //gfx -> print(comment);
}

bool readLidar() {
static int recordPos;
bool newLidarData = false;
  rawData[recordPos] = Serial2.read();
  recordPos++;
  if (rawData[0] != 0x54) recordPos=0;
  if ((recordPos > 1) && (rawData[1] != 0x2C)) recordPos=0;
  if (recordPos > 46) {
    recordPos = 0;
    calculateLidarData();
    newLidarData = true;
  }
  return newLidarData;
}

void calculateLidarData() {
int currentSpeed; // evtl. Ausgabe?
float angleStep;
  currentSpeed = int(rawData[3] << 8 | rawData[2]) / 6; // U/min
  float  minAngle = float(rawData[5] << 8 | rawData[4]) / 100; // Start Angle
  float  maxAngle = float(rawData[43] << 8 | rawData[42]) / 100; // end Angle
  if (maxAngle - minAngle > 0) angleStep = (maxAngle - minAngle) / (dataPointPerSentence -1);
    else angleStep = (maxAngle + (360 - minAngle)) / (dataPointPerSentence -1);
    for (int ii=0; ii < dataPointPerSentence;ii++) {
      mData[ii].dist = int(rawData[3*ii+1+6]<<8|rawData[3*ii+6]);
      mData[ii].quality = int(rawData[3*ii+2+6]);
      mData[ii].winkel = minAngle + ii * angleStep;
      if (mData[ii].winkel >= 360) mData[ii].winkel = mData[ii].winkel-360;
      mData[ii].winkel = 360 - mData[ii].winkel;       // Achtung Richtungswechsel
      //float angleInRadians = mData[ii].winkel * PI / 180.0;
      //mData[ii].x = mData[ii].dist * cos(angleInRadians)+ownX;
      //mData[ii].y = mData[ii].dist * -sin(angleInRadians)+ownY;
  }  
}

// Prüft, ob der Winkel und die Distanz eines LiDAR-Messdatums innerhalb eines bestimmten Bereichs liegen
bool isDataInRange(const messDaten& data, float startAngle, float endAngle, int minDistance, int maxDistance) {
    float currentAngle = data.winkel; // Der aktuelle Winkel der Messung
    int currentDistance = data.dist; // Die aktuelle Distanz der Messung
    
    // Korrektur des Winkels für einen vollständigen 360-Grad-Kreis
    currentAngle = fmod(currentAngle + 360, 360);
    startAngle = fmod(startAngle + 360, 360);
    endAngle = fmod(endAngle + 360, 360);
    
    // Prüfung, ob der Winkel im Bereich liegt
    bool angleInRange = false;
    if (startAngle <= endAngle) {
        angleInRange = currentAngle >= startAngle && currentAngle <= endAngle;
    } else {
        // Der Bereich überschreitet den 0-Grad-Punkt
        angleInRange = currentAngle >= startAngle || currentAngle <= endAngle;
    }
    
    // Prüfung, ob die Distanz im Bereich liegt
    bool distanceInRange = (currentDistance >= minDistance && currentDistance <= maxDistance);
    
    return angleInRange && distanceInRange;
}

void initLidar(){
  Serial2.begin(230400, SERIAL_8N1, dataInPin);
}

void lidarMotor(boolean mot) {
  digitalWrite(lidarPwr,mot);
}

void initHw(){
  Serial.begin(115200);
  initPixel();
  pinMode(extRelais,OUTPUT);
  pinMode(laser,OUTPUT);
  pinMode(lidarPwr,OUTPUT);
  digitalWrite(extRelais,LOW);
  digitalWrite(laser,LOW);
  digitalWrite(lidarPwr,LOW);
}
