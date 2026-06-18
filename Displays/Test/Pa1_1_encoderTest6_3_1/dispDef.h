// Pa1_1_encoderTest6_3_1 - Display-Definitionen (CYD28)
// abgeleitet aus Udisp6_3_0/dispDef.h (nur der CYD28-Block wird gebraucht).
#if defined(CYD28)
  #define screenWidth  320
  #define screenHight  240
  #define ID CYD
  // Encoder am CYD28
  #define ENC_A 22
  #define ENC_B 27
  #define taste 35
  #define counterInitialValue 2048
#else
  #error "Dieser Test ist fuer das CYD28-Display ausgelegt (#define CYD28)."
#endif
