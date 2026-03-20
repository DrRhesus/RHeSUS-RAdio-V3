// ============================================================
//  RHesus LIFE v2.7 — Conway's Game of Life / M5Cardputer
//  FIXED: Palette change via DEL, help overlay no flicker,
//         grid respects overlay areas (info & help)
// ============================================================

#include <M5Cardputer.h>

// ─── Neon palette ────────────────────────────────────────────
#define NEON_GREEN    0x07E0
#define NEON_LIME     0x47E6
#define NEON_CYAN     0x07FF
#define NEON_TEAL     0x0410
#define NEON_MAGENTA  0xF81F
#define NEON_YELLOW   0xFFE0
#define NEON_ORANGE   0xFD20
#define NEON_PINK     0xFC18
#define GRAY_BRIGHT   0xBDF7
#define GRAY_MID      0x7BEF
#define GRAY_DIM      0x39E7
#define COL_BLACK     0x0000
#define COL_WHITE     0xFFFF

const uint16_t neonPalette[] = {
  NEON_GREEN, NEON_CYAN, NEON_LIME, NEON_TEAL,
  NEON_MAGENTA, NEON_YELLOW, NEON_ORANGE, GRAY_BRIGHT
};
const int PALETTE_SIZE = 8;
int      paletteIdx = 0;
uint16_t rcolor     = NEON_GREEN;

uint16_t godColor() {
  uint8_t r = ((rcolor >> 11) & 0x1F);
  uint8_t g = ((rcolor >> 5)  & 0x3F);
  uint8_t b =  (rcolor        & 0x1F);
  r = constrain(r + 14, 0, 31);
  g = constrain(g + 28, 0, 63);
  b = constrain(b + 14, 0, 31);
  return ((uint16_t)r<<11)|((uint16_t)g<<5)|(uint16_t)b;
}

uint16_t demonColor() {
  uint8_t r = ((rcolor >> 11) & 0x1F);
  uint8_t g = ((rcolor >> 5)  & 0x3F);
  uint8_t b =  (rcolor        & 0x1F);
  r = constrain(r - 8, 0, 31);
  g = constrain(g - 16, 0, 63);
  b = constrain(b - 8, 0, 31);
  if (r < 4 && g < 8 && b < 4) { r = 8; g = 16; b = 8; }
  return ((uint16_t)r<<11)|((uint16_t)g<<5)|(uint16_t)b;
}

// ─── Grid ─────────────────────────────────────────────────────
#define GRIDX 240
#define GRIDY 135

uint8_t grid[GRIDX][GRIDY];
uint8_t newgrid[GRIDX][GRIDY];
uint8_t godgrid[GRIDX][GRIDY];
uint8_t demongrid[GRIDX][GRIDY];

// ─── Camera ───────────────────────────────────────────────────
int res   = 1;
int camX  = 0, camY  = 0;
int viewW = GRIDX, viewH = GRIDY;

void recalcView() {
  viewW = 240 / res;
  viewH = 135 / res;
  camX  = constrain(camX, 0, GRIDX - viewW);
  camY  = constrain(camY, 0, GRIDY - viewH);
}

// ─── State ────────────────────────────────────────────────────
int  genNum      = 0;
int  gens        = 9999;
int  popCount    = 0;
bool instantBoot = false;

// ─── Overlays ─────────────────────────────────────────────────
bool showInfoOverlay = false;
bool showHelpOverlay = false;

// ─── Brightness ───────────────────────────────────────────────
int screenBrightness = 80;

// ─── PANIC ────────────────────────────────────────────────────
bool panicMode = false;
#define BIRTH_NORMAL 0b000001000
#define SURV_NORMAL  0b000001100
#define BIRTH_PANIC  0b011001100
#define SURV_PANIC   0b100101110
uint16_t birthMask = BIRTH_NORMAL;
uint16_t survMask  = SURV_NORMAL;
unsigned long nextGlitchMs = 0;

// ─── GOD MODE ─────────────────────────────────────────────────
bool godMode = false;
#define GOD_LIFE 12

// ─── DEMON MODE ───────────────────────────────────────────────
bool demonMode = false;
#define DEMON_LIFE 24

// ─── Sound ────────────────────────────────────────────────────
bool          soundEnabled  = true;
int           soundType     = 0;
bool          zoomAudio     = false;
int           masterVol     = 14;
int32_t       birthCount    = 0;
int32_t       deathCount    = 0;
int32_t       vpBirthCount  = 0;
int32_t       vpDeathCount  = 0;
unsigned long lastSoundMs   = 0;
unsigned long speakerFreeMs = 0;
#define SOUND_THROTTLE_MS 55

const uint32_t PENTA[] = {220,261,311,392,466,523,622,784,932,1046};
const uint32_t MINOR[] = {220,261,293,349,392,440,523,587,659,784};
const int SCALE_SZ = 10;

const char* soundTypeNames[] = {
  "Bioacoustic","Electronic","Chaotic","Melodic",
  "Pulses","Spatial","Resonant","Glitchcore"
};

void safePlayTone(uint32_t freq, uint32_t dur_ms) {
  if (!soundEnabled) return;
  unsigned long now = millis();
  if (now < speakerFreeMs) return;
  freq = constrain(freq, 120, 10000);
  M5Cardputer.Speaker.tone(freq, dur_ms);
  speakerFreeMs = now + dur_ms + 6;
}

void dispatchSound(int32_t b, int32_t d) {
  if (!soundEnabled) return;
  unsigned long now = millis();
  if (now < speakerFreeMs || (now - lastSoundMs) < SOUND_THROTTLE_MS) return;
  bool doBirth = b > d + 10;
  bool doDeath = d > b + 10;
  bool doEq    = !doBirth && !doDeath && b > 5;
  switch (soundType) {
    case 0:
      if (doBirth)      safePlayTone(constrain(500+b*4,500,3500),25);
      else if (doDeath) safePlayTone(constrain(380-d/3,120,380),25);
      else if (doEq)    safePlayTone(680,18);
      break;
    case 1:
      if (doBirth)      safePlayTone(constrain(1000+b*8,1000,8000),14);
      else if (doDeath) safePlayTone(constrain(900-d*2,200,900),14);
      else if (doEq)    safePlayTone(1200,10);
      break;
    case 2:
      if (b+d>10) safePlayTone(120+(uint32_t)random(3880),18);
      break;
    case 3:
      if (doBirth)      safePlayTone(PENTA[constrain((int)(b/30),0,SCALE_SZ-1)],35);
      else if (doDeath) safePlayTone(PENTA[constrain(SCALE_SZ-1-(int)(d/30),0,SCALE_SZ-1)]/2,35);
      else if (doEq)    safePlayTone(PENTA[(millis()/500)%SCALE_SZ],25);
      break;
    case 4:
      if (doBirth)      safePlayTone(constrain(800+b*3,800,4000),8);
      else if (doDeath) safePlayTone(constrain(300+d*2,300,800),8);
      else if (doEq)    safePlayTone(500,6);
      break;
    case 5:
      if (doBirth)      safePlayTone(constrain(400+b*5,400,2000),50);
      else if (doDeath) safePlayTone(constrain(800-d*2,250,800),50);
      else if (doEq)    safePlayTone(600,40);
      break;
    case 6:
      if (doBirth)      safePlayTone(MINOR[constrain((int)(b/25),0,SCALE_SZ-1)],55);
      else if (doDeath) safePlayTone(MINOR[constrain((int)(d/25),0,SCALE_SZ-1)]/2,55);
      break;
    case 7:
      if (doBirth)      safePlayTone(250+(uint32_t)(b*7)%3000,12);
      else if (doDeath) safePlayTone(200+(uint32_t)(d*3)%600,14);
      else if (doEq)    safePlayTone((uint32_t)(millis()*7)%1800+250,10);
      break;
  }
  lastSoundMs = now;
}

void playNukeSound() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.setVolume(min(masterVol+80,255));
  for (int f=850;f>120;f-=28) { M5Cardputer.Speaker.tone(f,18); delay(18); }
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(masterVol);
  speakerFreeMs = millis()+60;
}

void playPanicJingle() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.setVolume(constrain(masterVol+40,1,200));
  delay(10);
  for (int f=600;f>180;f-=30) { M5Cardputer.Speaker.tone(f,25); delay(25); }
  delay(80);
  M5Cardputer.Speaker.tone(220,200); delay(220);
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(masterVol);
  speakerFreeMs = millis()+100;
}

void playGodSound() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.setVolume(constrain(masterVol+50,1,200));
  const uint32_t gn[] = {523,659,784,1046,1318,1568,2093,1568,1046};
  const uint32_t gd[] = { 70, 60,  55,   70,   55,   60,   90,  60,  120};
  for (int i=0;i<9;i++) { M5Cardputer.Speaker.tone(gn[i],gd[i]); delay(gd[i]+15); }
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(masterVol);
  speakerFreeMs = millis()+60;
}

void playDemonSound() {
  if (!soundEnabled) return;
  M5Cardputer.Speaker.setVolume(constrain(masterVol+50,1,200));
  const uint32_t dn[] = {261,196,164,130,110,98,87,98,130};
  const uint32_t dd[] = { 80, 70,  65,   80,   65,   70,  100,  70,  140};
  for (int i=0;i<9;i++) { M5Cardputer.Speaker.tone(dn[i],dd[i]); delay(dd[i]+15); }
  M5Cardputer.Speaker.stop();
  M5Cardputer.Speaker.setVolume(masterVol);
  speakerFreeMs = millis()+60;
}

void playZoomSound(bool in) { safePlayTone(in?1600:420,35); }

// ═══════════════════════════════════════════════════════════════
//   PIXEL FONT 5x7
// ═══════════════════════════════════════════════════════════════
const uint8_t FONT_R[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
const uint8_t FONT_H[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
const uint8_t FONT_E[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
const uint8_t FONT_S[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
const uint8_t FONT_U[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
const uint8_t FONT_L[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
const uint8_t FONT_I[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
const uint8_t FONT_F[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
const uint8_t FONT_D[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
const uint8_t FONT_M[7]={0x11,0x1B,0x15,0x11,0x11,0x11,0x11};
const uint8_t FONT_O[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
const uint8_t FONT_N[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11};

void drawPixelLetter(const uint8_t* bmp, int ox, int oy, int cs, uint16_t c) {
  for (int row=0;row<7;row++)
    for (int col=0;col<5;col++)
      if (bmp[row]&(0x10>>col))
        M5Cardputer.Display.fillRect(ox+col*(cs+1),oy+row*(cs+1),cs,cs,c);
}

// ═══════════════════════════════════════════════════════════════
//   EFFECTS
// ═══════════════════════════════════════════════════════════════
void glitchEffect(int durationMs) {
  unsigned long t = millis();
  while (millis()-t < (unsigned long)durationMs) {
    for (int p=0;p<15;p++)
      M5Cardputer.Display.drawPixel(random(240),random(135),neonPalette[random(PALETTE_SIZE)]);
    M5Cardputer.Display.fillRect(random(240),random(135),random(5,90),random(1,5),neonPalette[random(PALETTE_SIZE)]);
    if (random(3)==0)
      M5Cardputer.Display.fillRect(0,random(135),240,2,random(2)?COL_BLACK:GRAY_DIM);
    delay(10);
  }
}

void nukeEffect() {
  M5Cardputer.Display.fillScreen(NEON_GREEN); delay(50);
  M5Cardputer.Display.fillScreen(COL_BLACK);  delay(35);
  M5Cardputer.Display.fillScreen(GRAY_BRIGHT);delay(35);
  M5Cardputer.Display.fillScreen(COL_BLACK);
  for (int y=0;y<135;y+=3) {
    M5Cardputer.Display.drawFastHLine(0,y,  240,NEON_GREEN);
    M5Cardputer.Display.drawFastHLine(0,y+1,240,NEON_TEAL);
    M5Cardputer.Display.drawFastHLine(0,y+2,240,COL_BLACK);
    delay(4);
  }
  M5Cardputer.Display.fillScreen(COL_BLACK);
  M5Cardputer.Display.setTextSize(4);
  M5Cardputer.Display.setTextColor(NEON_GREEN,COL_BLACK);
  String msg="NUKE!";
  M5Cardputer.Display.drawString(msg,(240-M5Cardputer.Display.textWidth(msg))/2,51);
  glitchEffect(380);
  M5Cardputer.Display.fillScreen(COL_BLACK);
}

void panicActivateEffect() {
  M5Cardputer.Display.fillScreen(COL_BLACK);
  for (int fr=0;fr<18;fr++) {
    for (int c=0;c<30;c++) {
      char ch=(char)(33+random(93));
      M5Cardputer.Display.setTextSize(1);
      M5Cardputer.Display.setTextColor(random(2)?NEON_GREEN:GRAY_DIM,COL_BLACK);
      M5Cardputer.Display.drawChar(ch,random(240),random(135));
    }
    delay(25);
  }
  glitchEffect(300);
  for (int b=0;b<5;b++) {
    M5Cardputer.Display.fillScreen(COL_BLACK);
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(b%2?NEON_GREEN:GRAY_MID,COL_BLACK);
    M5Cardputer.Display.drawString("PANIC!",46,50);
    delay(110);
  }
  glitchEffect(250);
  M5Cardputer.Display.fillScreen(COL_BLACK);
}

void panicDeactivateEffect() {
  glitchEffect(250);
  M5Cardputer.Display.fillScreen(COL_BLACK);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(NEON_TEAL,COL_BLACK);
  M5Cardputer.Display.drawString("NORMAL",75,55);
  delay(350);
  M5Cardputer.Display.fillScreen(COL_BLACK);
}

void godEffect() {
  int cx=120, cy=67;
  for (int r=0;r<8;r++) {
    uint16_t gc = godColor();
    for (int i=0;i<12;i++) {
      int len=random(20,80);
      float ang=random(360)*0.01745f;
      for (int d=0;d<len;d++) {
        int px=cx+(int)(d*cos(ang));
        int py=cy+(int)(d*sin(ang));
        if (px>=0&&px<240&&py>=0&&py<135)
          M5Cardputer.Display.drawPixel(px,py,d%3==0?COL_WHITE:gc);
      }
    }
    delay(30);
  }
  for (int b=0;b<4;b++) {
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(b%2?COL_WHITE:godColor(),COL_BLACK);
    String msg="GOD";
    M5Cardputer.Display.drawString(msg,(240-M5Cardputer.Display.textWidth(msg))/2,50);
    delay(120);
  }
  M5Cardputer.Display.fillScreen(COL_BLACK);
}

void demonEffect() {
  uint16_t dc = demonColor();
  for (int p=0;p<6;p++) {
    int corners[4][2] = {{0,0},{239,0},{0,134},{239,134}};
    for (int c=0;c<4;c++) {
      for (int r=10;r<100;r+=15) {
        for (int a=0;a<90;a+=5) {
          float rad = a * 0.01745f;
          int px = corners[c][0] + (c%2==0 ? (int)(r*cos(rad)) : -(int)(r*cos(rad)));
          int py = corners[c][1] + (c<2 ? (int)(r*sin(rad)) : -(int)(r*sin(rad)));
          if (px>=0&&px<240&&py>=0&&py<135)
            M5Cardputer.Display.drawPixel(px,py,dc);
        }
      }
    }
    delay(40);
    M5Cardputer.Display.fillScreen(COL_BLACK);
  }
  for (int b=0;b<5;b++) {
    M5Cardputer.Display.setTextSize(3);
    M5Cardputer.Display.setTextColor(b%2?dc:GRAY_DIM,COL_BLACK);
    String msg="DEMON";
    M5Cardputer.Display.drawString(msg,(240-M5Cardputer.Display.textWidth(msg))/2,50);
    delay(100);
  }
  M5Cardputer.Display.fillScreen(COL_BLACK);
}

// ═══════════════════════════════════════════════════════════════
//   SPLASH SCREEN  — FIXED: LIFE (era LIE, faltaba la F)
// ═══════════════════════════════════════════════════════════════
void drawRHesusLogo() {
  M5Cardputer.Display.fillScreen(COL_BLACK);
  M5Cardputer.Display.drawRect(0,0,240,135,NEON_GREEN);
  M5Cardputer.Display.drawRect(2,2,236,131,NEON_TEAL);

  int cs=2, lw=5*(cs+1)-1, gap=4;
  const uint8_t* w1[]={FONT_R,FONT_H,FONT_E,FONT_S,FONT_U,FONT_S};
  int tw1=6*lw+5*gap, ox1=(240-tw1)/2, oy1=35;

  int cs2=4, lw2=5*(cs2+1)-1, gap2=6;
  // FIXED: 4 letras L-I-F-E (antes solo habia 3: L-I-E)
  const uint8_t* w2[]={FONT_L,FONT_I,FONT_F,FONT_E};
  int tw2=4*lw2+3*gap2, ox2=(240-tw2)/2, oy2=35+21+8;

  M5Cardputer.Speaker.setVolume(178);
  for (int l=0;l<6;l++) {
    drawPixelLetter(w1[l],ox1+l*(lw+gap),oy1,cs,GRAY_DIM);
    if (soundEnabled){M5Cardputer.Speaker.tone(200+l*40,30);delay(35);}
    delay(60);
    drawPixelLetter(w1[l],ox1+l*(lw+gap),oy1,cs,NEON_GREEN);
    if (soundEnabled){M5Cardputer.Speaker.tone(400+l*60,25);}
    delay(50);
  }
  M5Cardputer.Speaker.stop(); delay(80);

  // FIXED: bucle hasta 4 letras (antes era 3)
  for (int l=0;l<4;l++) {
    drawPixelLetter(w2[l],ox2+l*(lw2+gap2),oy2,cs2,GRAY_MID);
    if (soundEnabled){M5Cardputer.Speaker.tone(500+l*90,30);delay(35);}
    delay(60);
    drawPixelLetter(w2[l],ox2+l*(lw2+gap2),oy2,cs2,NEON_GREEN);
    if (soundEnabled){M5Cardputer.Speaker.tone(700+l*120,25);}
    delay(55);
  }
  M5Cardputer.Speaker.stop(); delay(180);

  if (soundEnabled) {
    M5Cardputer.Speaker.setVolume(178);
    const uint32_t jn[]={110,130,164,130,110,87,110};
    const uint32_t jd[]={120, 80,160, 80,100,180,220};
    for (int n=0;n<7;n++){M5Cardputer.Speaker.tone(jn[n],jd[n]);delay(jd[n]+20);}
    M5Cardputer.Speaker.stop();
    M5Cardputer.Speaker.setVolume(masterVol);
    speakerFreeMs=millis()+40;
  }

  delay(150);
  M5Cardputer.Display.setTextSize(1);
  int helpY = oy2 + 35 + 10;

  for (int anim=0;anim<6;anim++) {
    int dx = (anim%2==0) ? 1 : -1;
    M5Cardputer.Display.setTextColor(GRAY_DIM, COL_BLACK);
    M5Cardputer.Display.drawString("H FOR HELP!", (240-M5Cardputer.Display.textWidth("H FOR HELP!"))/2+1, helpY+1);
    M5Cardputer.Display.setTextColor(NEON_PINK, COL_BLACK);
    M5Cardputer.Display.drawString("H FOR HELP!", (240-M5Cardputer.Display.textWidth("H FOR HELP!"))/2+dx, helpY);
    if (soundEnabled && anim==0) M5Cardputer.Speaker.tone(1200,60);
    if (soundEnabled && anim==2) M5Cardputer.Speaker.tone(1400,60);
    if (soundEnabled && anim==4) M5Cardputer.Speaker.tone(1600,80);
    delay(120);
  }
  M5Cardputer.Display.setTextColor(GRAY_DIM,COL_BLACK);
  M5Cardputer.Display.drawString("H FOR HELP!",(240-M5Cardputer.Display.textWidth("H FOR HELP!"))/2+1,helpY+1);
  M5Cardputer.Display.setTextColor(NEON_PINK,COL_BLACK);
  M5Cardputer.Display.drawString("H FOR HELP!",(240-M5Cardputer.Display.textWidth("H FOR HELP!"))/2,helpY);
  M5Cardputer.Speaker.stop();

  delay(250);
  for (int fl=0;fl<4;fl++) {
    uint16_t c1=fl%2?NEON_GREEN:GRAY_DIM, c2=fl%2?NEON_GREEN:GRAY_MID;
    for (int l=0;l<6;l++) drawPixelLetter(w1[l],ox1+l*(lw+gap),oy1,cs,c1);
    // FIXED: bucle hasta 4 letras (antes era 3)
    for (int l=0;l<4;l++) drawPixelLetter(w2[l],ox2+l*(lw2+gap2),oy2,cs2,c2);
    if (soundEnabled&&fl%2==0) M5Cardputer.Speaker.tone(440,80);
    delay(130);
  }
  M5Cardputer.Speaker.stop();
  delay(500);
  M5Cardputer.Display.fillScreen(COL_BLACK);
}

// ═══════════════════════════════════════════════════════════════
//   GRID — Skip drawing under overlays
// ═══════════════════════════════════════════════════════════════
void drawGridFull() {
  for (int gx=camX;gx<camX+viewW;gx++)
    for (int gy=camY;gy<camY+viewH;gy++) {
      int sx=(gx-camX)*res, sy=(gy-camY)*res;
      if (showInfoOverlay && sx<136 && sy<82) continue;
      if (showHelpOverlay && sx>=8 && sx<232 && sy>=4 && sy<131) continue;
      int wx=((gx%GRIDX)+GRIDX)%GRIDX, wy=((gy%GRIDY)+GRIDY)%GRIDY;
      uint16_t c = COL_BLACK;
      if (grid[wx][wy]==1) {
        if (godgrid[wx][wy]>0) c = godColor();
        else if (demongrid[wx][wy]>0) c = demonColor();
        else c = rcolor;
      }
      M5Cardputer.Display.fillRect(sx,sy,res,res,c);
    }
}

void drawGridDiff() {
  popCount=0;
  for (int gx=camX;gx<camX+viewW;gx++)
    for (int gy=camY;gy<camY+viewH;gy++) {
      int sx=(gx-camX)*res, sy=(gy-camY)*res;
      if (showInfoOverlay && sx<136 && sy<82) continue;
      if (showHelpOverlay && sx>=8 && sx<232 && sy>=4 && sy<131) continue;
      int wx=((gx%GRIDX)+GRIDX)%GRIDX, wy=((gy%GRIDY)+GRIDY)%GRIDY;
      if (grid[wx][wy]==1) popCount++;
      if (grid[wx][wy]!=newgrid[wx][wy] || godgrid[wx][wy]>0 || demongrid[wx][wy]>0) {
        uint16_t c = COL_BLACK;
        if (newgrid[wx][wy]==1) {
          if (godgrid[wx][wy]>0) c = godColor();
          else if (demongrid[wx][wy]>0) c = demonColor();
          else c = rcolor;
        }
        M5Cardputer.Display.fillRect(sx,sy,res,res,c);
      }
    }
}

void initGrid() {
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(COL_BLACK);
  for (int16_t x=0;x<GRIDX;x++)
    for (int16_t y=0;y<GRIDY;y++) {
      newgrid[x][y]=0; godgrid[x][y]=0; demongrid[x][y]=0;
      grid[x][y]=(random(3)==1)?1:0;
    }
  genNum=0;
  recalcView();
  drawGridFull();
}

inline int nbCount(int16_t x, int16_t y) {
  int16_t xm=(x-1+GRIDX)%GRIDX, xp=(x+1)%GRIDX;
  int16_t ym=(y-1+GRIDY)%GRIDY, yp=(y+1)%GRIDY;
  return grid[xm][y]+grid[xm][ym]+grid[x][ym]+
         grid[xp][ym]+grid[xp][y]+grid[xp][yp]+
         grid[x][yp]+grid[xm][yp];
}

void computeCA() {
  birthCount=0;deathCount=0;vpBirthCount=0;vpDeathCount=0;
  for (int16_t x=0;x<GRIDX;x++)
    for (int16_t y=0;y<GRIDY;y++) {
      int n=nbCount(x,y);
      uint8_t cur=grid[x][y];
      bool inVP=(x>=camX&&x<camX+viewW&&y>=camY&&y<camY+viewH);
      uint16_t bm = birthMask;
      uint16_t sm = survMask;
      if (godgrid[x][y]>0) {
        sm = 0b000011110;
      } else if (demongrid[x][y]>0) {
        sm = 0b000111110;
      }
      if (cur==1) {
        if (sm&(1<<n)) {newgrid[x][y]=1;}
        else {
          newgrid[x][y]=0;
          deathCount++;
          if(inVP) vpDeathCount++;
          if(godgrid[x][y]>0) godgrid[x][y]--;
          if(demongrid[x][y]>0) demongrid[x][y]--;
        }
      } else {
        if (bm&(1<<n)) {
          newgrid[x][y]=1;
          birthCount++;
          if(inVP) vpBirthCount++;
        } else {
          newgrid[x][y]=0;
        }
      }
    }
}

void advanceGrid() {
  for (int16_t x=0;x<GRIDX;x++)
    for (int16_t y=0;y<GRIDY;y++)
      grid[x][y]=newgrid[x][y];
}

// ═══════════════════════════════════════════════════════════════
//   NUKE
// ═══════════════════════════════════════════════════════════════
void nukeWorld(int &gen) {
  showInfoOverlay=false; showHelpOverlay=false;
  playNukeSound(); nukeEffect();
  gen=-1;
  paletteIdx=(paletteIdx+1)%PALETTE_SIZE;
  rcolor=neonPalette[paletteIdx];
  panicMode=false; godMode=false; demonMode=false;
  birthMask=BIRTH_NORMAL; survMask=SURV_NORMAL;
  memset(godgrid,0,sizeof(godgrid));
  memset(demongrid,0,sizeof(demongrid));
  initGrid();
}

// ═══════════════════════════════════════════════════════════════
//   PANIC
// ═══════════════════════════════════════════════════════════════
void togglePanic(bool &vc) {
  panicMode=!panicMode;
  if (panicMode) {
    birthMask=BIRTH_PANIC;
    survMask=SURV_PANIC;
    playPanicJingle();
    panicActivateEffect();
  } else {
    birthMask=BIRTH_NORMAL;
    survMask=SURV_NORMAL;
    panicDeactivateEffect();
  }
  vc=true;
}

void maybePanicGlitch() {
  if (!panicMode) return;
  unsigned long now=millis();
  if (now<nextGlitchMs) return;
  nextGlitchMs=now+(unsigned long)random(350,1600);
  M5Cardputer.Display.fillRect(random(240),random(135),random(20,110),2,neonPalette[random(PALETTE_SIZE)]);
  for (int p=0;p<6;p++)
    M5Cardputer.Display.drawPixel(random(240),random(135),neonPalette[random(PALETTE_SIZE)]);
}

// ═══════════════════════════════════════════════════════════════
//   GOD MODE
// ═══════════════════════════════════════════════════════════════
void invokeGod(bool &vc) {
  playGodSound();
  godEffect();
  int injected=0;
  for (int attempt=0; attempt<500 && injected<80; attempt++) {
    int gx=camX+random(viewW), gy=camY+random(viewH);
    int wx=((gx%GRIDX)+GRIDX)%GRIDX, wy=((gy%GRIDY)+GRIDY)%GRIDY;
    if (grid[wx][wy]==0 && nbCount(wx,wy)<=1) {
      grid[wx][wy]=1;
      godgrid[wx][wy]=GOD_LIFE;
      injected++;
    }
  }
  Serial.println("God injected: "+String(injected));
  vc=true;
}

// ═══════════════════════════════════════════════════════════════
//   DEMON MODE
// ═══════════════════════════════════════════════════════════════
void invokeDemon(bool &vc) {
  playDemonSound();
  demonEffect();
  int injected=0;
  for (int attempt=0; attempt<500 && injected<60; attempt++) {
    int gx=camX+random(viewW), gy=camY+random(viewH);
    int wx=((gx%GRIDX)+GRIDX)%GRIDX, wy=((gy%GRIDY)+GRIDY)%GRIDY;
    if (grid[wx][wy]==0 && nbCount(wx,wy)>=2 && nbCount(wx,wy)<=4) {
      grid[wx][wy]=1;
      demongrid[wx][wy]=DEMON_LIFE;
      injected++;
    }
  }
  Serial.println("Demon injected: "+String(injected));
  vc=true;
}

// ═══════════════════════════════════════════════════════════════
//   INFO OVERLAY
// ═══════════════════════════════════════════════════════════════
void drawInfoOverlay(int gen) {
  M5Cardputer.Display.fillRect(0,0,136,82,COL_BLACK);
  M5Cardputer.Display.drawRect(0,0,136,82,rcolor);
  M5Cardputer.Display.drawRect(1,1,134,80,GRAY_DIM);
  M5Cardputer.Display.setTextSize(1);
  struct {const char* lbl; String val;} rows[]={
    {"GEN",  String(gen)},
    {"POP",  String(popCount)},
    {"ZOOM", String(res)+"x"},
    {"CAM",  "X"+String(camX)+" Y"+String(camY)},
    {"BORN", String(birthCount)},
    {"DEAD", String(deathCount)},
    {"SND",  soundEnabled?soundTypeNames[soundType]:"---"},
    {"MODE", panicMode?"!PANIC!":godMode?"~GOD~":demonMode?"[DEMON]":"normal"},
  };
  for (int i=0;i<8;i++) {
    M5Cardputer.Display.setTextColor(GRAY_MID,COL_BLACK);
    M5Cardputer.Display.drawString(rows[i].lbl,4,4+i*9);
    uint16_t vc=(i==7&&panicMode)?NEON_GREEN:(i==7&&godMode)?godColor():(i==7&&demonMode)?demonColor():rcolor;
    M5Cardputer.Display.setTextColor(vc,COL_BLACK);
    M5Cardputer.Display.drawString(rows[i].val,42,4+i*9);
  }
  int bw=constrain((int)(birthCount/6),0,90);
  int dw=constrain((int)(deathCount/6),0,90);
  M5Cardputer.Display.fillRect(42,75,90,3,GRAY_DIM);
  M5Cardputer.Display.fillRect(42,75,bw, 3,rcolor);
  M5Cardputer.Display.fillRect(42,78,90,3,GRAY_DIM);
  M5Cardputer.Display.fillRect(42,78,dw, 3,GRAY_MID);
}

// ═══════════════════════════════════════════════════════════════
//   HELP OVERLAY
// ═══════════════════════════════════════════════════════════════
void drawHelpOverlay() {
  M5Cardputer.Display.fillRect(8,4,224,127,COL_BLACK);
  M5Cardputer.Display.drawRect(8,4,224,127,rcolor);
  M5Cardputer.Display.drawRect(9,5,222,125,GRAY_DIM);
  M5Cardputer.Display.setTextSize(1);
  const char* lines[]={
    "BtnA (BtNG0)  Nuke / New world",
    "= / -            Zoom IN / OUT",
    "Arrows           Move around",
    "DEL              Next color palette",
    "1-9              Volume",
    "0                Sound type(8types)",
    "S                Mute / Unmute",
    "P                PANIC start/stop",
    "G/D              GOD/DEMON MODE",
    "I                Real Time info",
    "Z                Zone audio on/off",
    "W                Brightness",
  };
  for (int i=0;i<12;i++) {
    M5Cardputer.Display.setTextColor(i%2==0?rcolor:GRAY_MID,COL_BLACK);
    M5Cardputer.Display.drawString(lines[i],14,10+i*11);
  }
}

// ═══════════════════════════════════════════════════════════════
//   SETUP
// ═══════════════════════════════════════════════════════════════
void setup() {
  auto cfg=M5.config();
  M5Cardputer.begin(cfg,true);
  Serial.begin(9600);
  Serial.println("RHesus LIFE v2.7");
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(screenBrightness);
  M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(masterVol);
  birthMask=BIRTH_NORMAL; survMask=SURV_NORMAL;
  recalcView();
  if (!instantBoot) drawRHesusLogo();
}

// ═══════════════════════════════════════════════════════════════
//   MAIN LOOP
// ═══════════════════════════════════════════════════════════════
void loop() {
  M5Cardputer.update();
  initGrid();

  for (int gen=0;gen<gens;gen++) {
    M5Cardputer.update();
    bool vc=false;

    if (M5Cardputer.BtnA.wasPressed()) { nukeWorld(gen); vc=true; }

    if (M5Cardputer.Keyboard.isChange()) {

      Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

      if (M5Cardputer.Keyboard.isKeyPressed('=')) {
        if (res<8){res++;recalcView();playZoomSound(true);M5Cardputer.Display.fillScreen(COL_BLACK);vc=true;}
      }
      if (M5Cardputer.Keyboard.isKeyPressed('-')) {
        if (res>1){res--;recalcView();playZoomSound(false);M5Cardputer.Display.fillScreen(COL_BLACK);vc=true;}
      }

      int pH=max(1,viewW/6), pV=max(1,viewH/6);
      if (M5Cardputer.Keyboard.isKeyPressed(',')) {camX=constrain(camX-pH,0,GRIDX-viewW);M5Cardputer.Display.fillScreen(COL_BLACK);vc=true;}
      if (M5Cardputer.Keyboard.isKeyPressed('/')) {camX=constrain(camX+pH,0,GRIDX-viewW);M5Cardputer.Display.fillScreen(COL_BLACK);vc=true;}
      if (M5Cardputer.Keyboard.isKeyPressed(';')) {camY=constrain(camY-pV,0,GRIDY-viewH);M5Cardputer.Display.fillScreen(COL_BLACK);vc=true;}
      if (M5Cardputer.Keyboard.isKeyPressed('.')) {camY=constrain(camY+pV,0,GRIDY-viewH);M5Cardputer.Display.fillScreen(COL_BLACK);vc=true;}

      if (status.del) {
        paletteIdx=(paletteIdx+1)%PALETTE_SIZE;
        rcolor=neonPalette[paletteIdx];
        safePlayTone(900,28);
        vc=true;
        M5Cardputer.Display.fillScreen(COL_BLACK);
      }

      for (char k='1';k<='9';k++)
        if (M5Cardputer.Keyboard.isKeyPressed(k)) {
          masterVol=(int)(k-'0')*14;
          M5Cardputer.Speaker.setVolume(masterVol);
          safePlayTone(1000,35);
        }

      if (M5Cardputer.Keyboard.isKeyPressed('0')) {
        soundType=(soundType+1)%8;
        safePlayTone(300+soundType*200,45);
      }

      if (M5Cardputer.Keyboard.isKeyPressed('s')) {
        soundEnabled=!soundEnabled;
        if (soundEnabled){M5Cardputer.Speaker.setVolume(masterVol);M5Cardputer.Speaker.tone(1100,80);delay(90);}
        else{M5Cardputer.Speaker.tone(280,80);delay(90);M5Cardputer.Speaker.stop();}
        speakerFreeMs=millis()+50;
      }

      if (M5Cardputer.Keyboard.isKeyPressed('p')) togglePanic(vc);
      if (M5Cardputer.Keyboard.isKeyPressed('g')) invokeGod(vc);
      if (M5Cardputer.Keyboard.isKeyPressed('d')) invokeDemon(vc);

      if (M5Cardputer.Keyboard.isKeyPressed('z')) {
        zoomAudio=!zoomAudio;
        safePlayTone(zoomAudio?1800:600,40);
      }

      if (M5Cardputer.Keyboard.isKeyPressed('w')) {
        screenBrightness=constrain(screenBrightness+25,25,255);
        if (screenBrightness>250) screenBrightness=25;
        M5Cardputer.Display.setBrightness(screenBrightness);
        safePlayTone(1200,20);
      }

      if (status.enter) {
        if (showInfoOverlay||showHelpOverlay) vc=true;
        showInfoOverlay=false; showHelpOverlay=false;
      }
    }

    // Hold-to-show overlays
    {
      bool hi=M5Cardputer.Keyboard.isKeyPressed('i');
      bool hh=M5Cardputer.Keyboard.isKeyPressed('h');
      bool prevI=showInfoOverlay, prevH=showHelpOverlay;
      showInfoOverlay=hi;
      showHelpOverlay=hh&&!hi;
      if (prevI&&!showInfoOverlay) vc=true;
      if (prevH&&!showHelpOverlay) vc=true;
    }

    // Simulate
    delay(panicMode?8:16);
    computeCA(); genNum=gen;

    // Sound
    dispatchSound(zoomAudio?vpBirthCount:birthCount,
                  zoomAudio?vpDeathCount:deathCount);

    // Panic glitch
    maybePanicGlitch();

    // Draw grid respecting overlays
    if (vc) {
      drawGridFull();
    } else {
      drawGridDiff();
    }

    // Info updates every 200ms, help every frame (static content, no flicker)
    static unsigned long lastInfoMs=0;
    unsigned long nowO=millis();
    if (showInfoOverlay&&(nowO-lastInfoMs>=200)) {
      drawInfoOverlay(gen); lastInfoMs=nowO;
    }
    if (showHelpOverlay) {
      drawHelpOverlay();
    }

    advanceGrid();
  }
}
