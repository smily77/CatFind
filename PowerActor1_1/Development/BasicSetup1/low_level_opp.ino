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
