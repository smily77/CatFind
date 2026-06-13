void adjustStepSize(int stepSize) {
    switch (stepSize) {
      case fullStep:
        rotor.digitalWrite(MS1, LOW);
        rotor.digitalWrite(MS2, LOW);
        rotor.digitalWrite(MS3, LOW);
      break;
      case halfStep:
        rotor.digitalWrite(MS1, HIGH);
        rotor.digitalWrite(MS2, LOW);
        rotor.digitalWrite(MS3, LOW);
      break;
      case quaterStep:
        rotor.digitalWrite(MS1, LOW);
        rotor.digitalWrite(MS2, HIGH);
        rotor.digitalWrite(MS3, LOW);
      break;
      case eighthStep:
        rotor.digitalWrite(MS1, HIGH);
        rotor.digitalWrite(MS2, HIGH);
        rotor.digitalWrite(MS3, LOW);
      break;
      case sixteenthStep:
        rotor.digitalWrite(MS1, HIGH);
        rotor.digitalWrite(MS2, HIGH);
        rotor.digitalWrite(MS3, HIGH);
      break;

    }
}

void rotorStep(boolean richtung, int stepSize) {
  int i=0;
  adjustStepSize(stepSize);
  rotor.digitalWrite(DIR, richtung);
  while(i < stepSize) {
    rotor.digitalWrite(STEP, HIGH);
    rotor.digitalWrite(STEP, LOW);
    winkel.getCumulativePosition();
    i++;
  }
  if (richtung == right) {
      globalPosition++;
  }
    else {
      globalPosition--; 
   }
}

boolean leftReed(){
  return !digitalRead(reedLeft);
}

boolean rightReed(){
  return !digitalRead(reedRight);
}
