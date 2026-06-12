void initDisp() {
#if defined(CYD28)  
  gfx.init();               
  gfx.setRotation(3); 
  //Encoder
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(taste, INPUT_PULLUP);
#elif defined(CYD35)   
  gfx.init();
  gfx.setRotation(3);
  gfx.setBrightness(220);   // 0..255
#elif defined(Sunton7)
  gfx.begin();
#elif defined(Sunton5)
  // 8 Encoder begin
  terminal.begin(&I2Cone, ENCODER_ADDR, sdaWire, sclWire); 
  // 8 Encoder begin
  gfx.begin(); 
#elif defined(M5Core2)
  auto cfg = M5.config();
  M5.begin(cfg);
  // 8 Encoder begin
  terminal.begin(&I2Cone, ENCODER_ADDR, sda, scl);
  I2Cone.begin(sda,scl,100000);
  // 8 Encoder end
#elif defined(M5Tab5)
  auto cfg = M5.config();
  M5.begin(cfg);
  gfx.setRotation(3);
#elif defined(Wave7)
  gfx.init();
  gfx.setRotation(0);  
#endif

  spritFactor = stEnable ? 2 : 1;
  hasPSRAM = psramFound();
  gfx.setBrightness(255); 
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextFont(2);
  gfx.setTextColor(GELB);

  for (auto& c : cfg2) if (c.idx < noSprites) layer[c.idx].setColorDepth(c.depth);
    
  for (int i=0; i < noSprites; i++) {
        
    if (hasPSRAM) layer[i].setPsram(true);
    String msg = "Sprite " + String(i) + " Allocation ";
    if (!layer[i].createSprite(i == projectorLayer ? screenWidth : spritFactor * screenWidth, screenHight)) msg = msg + "Failed ";
      else msg = msg + "Ok ";
    msg = msg + " (PSRAM:" + String(hasPSRAM ? "on" : "off") + ")";
    writelnComment(msg);
    if (i != projectorLayer) {
      layer[i].setPaletteColor(0, SCHWARZ);
      layer[i].fillSprite(0);
    }  
    setPivotAndStore(layer[i],i, int((i == projectorLayer ? screenWidth : spritFactor * screenWidth)/2), screenHight-1);
    //layer[i].setPivot(int((i == projectorLayer ? screenWidth : spritFactor * screenWidth)/2),screenHight-1);
   }
  for (const auto &p : palCfg) {
    Serial << p.layerIdx << " - " << p.colorIdx << "  -  " << p.colorValue << " - " << (p.layerIdx < noSprites) << " - " << layer[p.layerIdx].getColorDepth() << endl;
    if (p.layerIdx < noSprites) layer[p.layerIdx].setPaletteColor(p.colorIdx, p.colorValue);    
  }

}


void pushScreens(int dispAngel,int dispShift, bool all) {
  if (stEnable && !hasPSRAM) {
    if (actionsActive) {
      gfx.fillScreen(0x8410);
      layer[limitLayer].pushRotateZoom(&gfx,int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
    }
    else {
      if (all) layer[limitLayer].pushRotateZoom(&gfx,int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1);
    }
    if (all) layer[gridLayer].pushRotateZoom(&gfx,int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
    layer[detectLayer].pushRotateZoom(&gfx,int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
    layer[hitLayer].pushRotateZoom(&gfx,int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
  }
  else if (!stEnable && !hasPSRAM) {
    if (all)  layer[limitLayer].pushRotateZoom(&gfx,int(screenWidth/2),screenHight,0,1,1);
    if (all)  layer[gridLayer].pushRotateZoom(&gfx,int(screenWidth/2),screenHight,0,1,1,0);
    layer[detectLayer].pushRotateZoom(&gfx,int(screenWidth/2),screenHight,0,1,1,0);
    layer[hitLayer].pushRotateZoom(&gfx,int(screenWidth/2),screenHight,0,1,1,0);
  }
  else if (hasPSRAM) { 
    if (actionsActive) layer[projectorLayer].fillScreen(GRAU); else layer[projectorLayer].fillScreen(SCHWARZ);
    layer[limitLayer].pushRotateZoom(&layer[projectorLayer],int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
    layer[gridLayer].pushRotateZoom(&layer[projectorLayer],int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
    layer[detectLayer].pushRotateZoom(&layer[projectorLayer],int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);
    layer[hitLayer].pushRotateZoom(&layer[projectorLayer],int(screenWidth/2)+dispShift,screenHight,dispAngel,1,1,0);

    layer[projectorLayer].pushRotateZoom(&gfx,int(screenWidth/2),screenHight,0,1,1);
  }
}

void drawMenue(int menuePos) {
  gfx.fillScreen(TFT_BLACK);
  layer[limitLayer].fillScreen(0);
  layer[limitLayer].setTextFont(4);
  for(uint8_t i=0; i < pages;i++) {
    if (menuePos != i) layer[limitLayer].setTextColor(1);
      else layer[limitLayer].setTextColor(2);
    layer[limitLayer].drawString(menueItems[i],int(screenWidth/2)+10,int(screenHight/pages*i));
  }
  layer[limitLayer].pushRotateZoom(&gfx,int(screenWidth/2),screenHight,0,1,1,0);
}
//------------------ DrawVector ----------------------------------------
template <class GFX>
void drawVectorLine(GFX& gfx, int32_t x0, int32_t y0, float angle_deg, uint32_t color) {

  angle_deg = (angle_deg - 2048)*360/4046-90;

  // Konvertiere Winkel von Grad in Radiant
  float angle_rad = angle_deg * M_PI / 180.0;

  // Bildschirm-/Sprite-Dimensionen erhalten
  int32_t maxX = gfx.width() - 1;
  int32_t maxY = gfx.height() - 1;

  // Maximale mögliche Länge berechnen (Diagonale des Bildschirms/Sprites)
  // Dies stellt sicher, dass wir immer über den Rand hinausgehen
  int32_t max_len = (int32_t)sqrt(maxX * maxX + maxY * maxY) + 1;

  // Endpunkt des Strahls berechnen
  int32_t x1_calc = x0 + (int32_t)(cos(angle_rad) * max_len);
  int32_t y1_calc = y0 + (int32_t)(sin(angle_rad) * max_len);
  
  // LovyanGFX übernimmt das Abschneiden am Rand
  gfx.drawLine(x0, y0, x1_calc, y1_calc, color);
}
//---------
/*
template <class GFX>
void drawVectorLine(GFX& g, int x0, int y0, float angle_deg, uint32_t color) {
  const int w = screenWidth*spritFactor;
  const int h = screenHight;
  angle_deg = (angle_deg - 2048)*360/4046;
  // Falls Startpunkt außerhalb: optional hier früh aussteigen
  if (x0 < 0 || x0 >= w || y0 < 0 || y0 >= h) return;

  // 0° = oben, Winkel im Uhrzeigersinn
  const float a  = angle_deg * (float)DEG_TO_RAD;
  const float dx = sinf(a);   // so ist 90° -> +x
  const float dy = -cosf(a);  // und 0° -> -y (nach oben)

  const float eps = 1e-6f;
  float best_t = INFINITY;

  auto consider = [&](float t) {
    if (t <= 0.0f || !isfinite(t)) return;
    if (t < best_t) best_t = t;
  };

  // Kollision mit linken/rechten Rand (x = 0 bzw. x = w-1)
  if (fabsf(dx) > eps) {
    float tL = (0.0f   - x0) / dx;
    float yL = y0 + tL * dy;
    if (yL >= 0.0f && yL <= (h - 1)) consider(tL);

    float tR = ((w-1) - x0) / dx;
    float yR = y0 + tR * dy;
    if (yR >= 0.0f && yR <= (h - 1)) consider(tR);
  }

  // Kollision mit oberen/unteren Rand (y = 0 bzw. y = h-1)
  if (fabsf(dy) > eps) {
    float tT = (0.0f   - y0) / dy;
    float xT = x0 + tT * dx;
    if (xT >= 0.0f && xT <= (w - 1)) consider(tT);

    float tB = ((h-1) - y0) / dy;
    float xB = x0 + tB * dx;
    if (xB >= 0.0f && xB <= (w - 1)) consider(tB);
  }

  if (!isfinite(best_t)) return; // kein Schnitt (sollte praktisch nicht vorkommen)

  int x1 = (int)lroundf(x0 + best_t * dx);
  int y1 = (int)lroundf(y0 + best_t * dy);

  // Zur Sicherheit in den gültigen Bereich klemmen
  x1 = (x1 < 0) ? 0 : (x1 > w-1 ? w-1 : x1);
  y1 = (y1 < 0) ? 0 : (y1 > h-1 ? h-1 : y1);

  g.drawLine(x0, y0, x1, y1, color);
}
*/
// -------------------------------------------------------------------------------------------------
void drawGrid() {
  int systemAngle = maxAngle * 4096 / 360;
  drawVectorLine(layer[gridLayer],PX(gridLayer,0),PY(gridLayer,0),2048+ systemAngle,1);
  drawVectorLine(layer[gridLayer],PX(gridLayer,0),PY(gridLayer,0),2048- systemAngle,1);
  
  layer[gridLayer].fillArc(PX(gridLayer,0), PY(gridLayer,0), 0,  int(deadZone/xScale), -maxAngle-1-90, maxAngle+1-90, 0);
  layer[gridLayer].drawArc(PX(gridLayer,0), PY(gridLayer,0), int(deadZone/xScale)-1, int(deadZone/xScale), -maxAngle-90, maxAngle-90, 1);
}

void calcScale(int angle, int dist) {
  angle = 90 - angle;
  int hyp = int((screenWidth/2)/sin(angle *2 * 3.141/360));
  if (hyp > screenHight) hyp = screenHight;
  xScale = int(maxDist/hyp);
  yScale = xScale;
}

// --------- Pivot Zentrums Funktionen -----------------
inline void setPivotAndStore(LGFX_Sprite& spr, int idx, int16_t px, int16_t py) {
  if (idx < 0 || idx >= noSprites) return;
  spr.setPivot(px, py);
  g_px[idx] = px;
  g_py[idx] = py;
}

// --- Pivot → Sprite (zeichnen) ---
inline int16_t PX(int idx, int16_t sx) {
  if (idx < 0 || idx >= noSprites) return sx;
  return static_cast<int16_t>(g_px[idx] + sx);
}
inline int16_t PY(int idx, int16_t sy) {
  if (idx < 0 || idx >= noSprites) return sy;
  return static_cast<int16_t>(g_py[idx] - sy);  // Display-y wächst nach unten
}
// --- Lesen in Pivot-Koordinaten ---
// Schnell: ohne Bounds-Check (wenn du sicher bist, dass Koords im Sprite liegen)
inline uint32_t readPixelP(LGFX_Sprite& spr, int idx, int16_t sx, int16_t sy) {
  const int16_t x = PX(idx, sx);
  const int16_t y = PY(idx, sy);
  return spr.readPixel(x, y);
}

// Hilfsfunktion: Bounds testen
inline bool spriteInBounds(const LGFX_Sprite& spr, int16_t x, int16_t y) {
  return (x >= 0) && (y >= 0) && (x < spr.width()) && (y < spr.height());
}

// Sicher: mit Bounds-Check (liefert 0 bei Out-of-Bounds)
inline uint32_t readPixelP_safe(LGFX_Sprite& spr, int idx, int16_t sx, int16_t sy) {
  const int16_t x = PX(idx, sx);
  const int16_t y = PY(idx, sy);
  if (!spriteInBounds(spr, x, y)) return 0;
  return spr.readPixel(x, y);
}

// Praktisch: schneller „ist gesetzt?“-Test (≠0)
inline bool anyPixelAtP(LGFX_Sprite& spr, int idx, int16_t sx, int16_t sy) {
  return readPixelP(spr, idx, sx, sy) != 0;
}
// ------------------------------------------------------
void writelnComment(String comment) {
  if (DEBUG) Serial << comment  <<  endl;
   gfx << comment << endl;
//  gfx.println(comment);
}

void writeComment(String comment) {
  if (DEBUG) Serial << comment;
  //tft << comment;
  gfx.print(comment);
}

void testOnce() {
  gfx.fillScreen(TFT_BLACK);
  gfx.setTextColor(TFT_GREEN, TFT_BLACK);
  gfx.setTextSize(2);

  // Begrüßung
  gfx.setCursor(10, 10);
  gfx.println("Hello LovyanGFX!");

  // Board-Name
  gfx.setCursor(10, 40);
  gfx.print("Detected: ");
//  lcd.println(lcd.getBoardName());

  // Prüfen ob Touch vorhanden
  gfx.setCursor(10, 70);
  if (gfx.touch())
    gfx.println("Touch: available");
  else
    gfx.println("Touch: not detected");

  delay(200);

  // kleiner Grafik-Test
  for (int r = 10; r < 100; r += 10) {
    gfx.drawCircle(gfx.width() / 2, gfx.height() / 2, r, random(0xFFFF));
    delay(200);
  }
}

void testLoop() {
  static int x = 0;
  gfx.fillRect(0, gfx.height() - 30, gfx.width(), 30, TFT_BLACK);
  gfx.setCursor(10, gfx.height() - 25);
  gfx.printf("Counter: %d", x++);

  // --- Touchscreen Test ---
  if (gfx.touch()) {
    uint16_t tx, ty;
    if (gfx.getTouch(&tx, &ty)) {    // Wenn Berührung erkannt
      gfx.fillCircle(tx, ty, 5, TFT_RED);  // roten Kreis zeichnen
      gfx.setCursor(10, gfx.height() - 60);
      gfx.printf("Touch at: %d,%d  ", tx, ty);
    }
  }
}
