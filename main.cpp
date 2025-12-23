#include <Arduino.h>

/*
  распиновка под st7735s 
  PA5  → SCK  (SCL)      — такт spi
  PA7  → MOSI (SDA)      — данные в дисплей
  PA4  → CS              — выбор дисплея
  PA3  → DC  (A0 / RS)   — команда/данные
  PA2  → RST             — ресет контроллера дисплея
  PB1  → LED/BL          — подсветка

  кнопки:
  PA8  → влево
  PA9  → вниз (ускорение)
  PA10 → вправо
  PA11 → поворот (коротко) / reset (держать 5 сек)
*/

// пины под дисплей
#define TFT_SCLK  PA5
#define TFT_MOSI  PA7
#define TFT_CS    PA4
#define TFT_DC    PA3
#define TFT_RST   PA2
#define TFT_BLK   PB1

// размеры тетрис-поля: 16x20 клеток, каждая по 8 пикс
// 16*8 = 128 (ширина экрана), 20*8 = 160 (высота экрана)
#define CELL_SIZE   8
#define FIELD_W     16
#define FIELD_H     20

// цвета в rgb565
#define COLOR_BG      0x0000  // фон (чёрный)
#define COLOR_BORDER  0xFFFF  // рамка (белый)
#define COLOR_PIECE   0x07E0  // блоки (зелёный)

// поле: 0 пусто, 1 занято
// тут храним только “приклеенные” блоки, активная фигура рисуется отдельно
uint8_t field[FIELD_H][FIELD_W];

inline void H(uint8_t p){ digitalWrite(p, HIGH); }
inline void L(uint8_t p){ digitalWrite(p, LOW);  }

inline void SCLK_H(){ H(TFT_SCLK);}  inline void SCLK_L(){ L(TFT_SCLK); }
inline void MOSI_H(){ H(TFT_MOSI);}  inline void MOSI_L(){ L(TFT_MOSI); }
inline void CS_H()  { H(TFT_CS);}    inline void CS_L()  { L(TFT_CS);   }
inline void DC_H()  { H(TFT_DC);}    inline void DC_L()  { L(TFT_DC);   }
inline void RST_H() { H(TFT_RST);}   inline void RST_L() { L(TFT_RST);  }

// ---------- битбэнг spi ----------
// вручную дёргаем sclk/mosi, чтобы отправить байт
// задержки небольшие, дисплей успевает
void spiWrite(uint8_t b){
  for (int i = 7; i >= 0; --i){
    if (b & (1 << i)) MOSI_H(); else MOSI_L();
    SCLK_H();
    delayMicroseconds(2);
    SCLK_L();
    delayMicroseconds(2);
  }
}

// отправка команды/данных в st7735
// cs держим низким на время байта, dc выбирает режим
void cmd(uint8_t c){ CS_L(); DC_L(); spiWrite(c); CS_H(); }
void dat(uint8_t d){ CS_L(); DC_H(); spiWrite(d); CS_H(); }
void dat16(uint16_t d){ dat(d >> 8); dat(d & 0xFF); }

// смещения
const uint8_t XOFF = 0, YOFF = 0;

// задаём окно вывода (address window)
void setAW(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1){
  cmd(0x2A);           // column addr set
  dat16(x0 + XOFF);
  dat16(x1 + XOFF);

  cmd(0x2B);           // row addr set
  dat16(y0 + YOFF);
  dat16(y1 + YOFF);

  cmd(0x2C);           // memory write
}

// ---------- init st7735s ----------
// набор команд из типового init’а 
void st7735s_init(){
  // аппаратный ресет дисплея
  RST_H(); delay(5);
  RST_L(); delay(20);
  RST_H(); delay(120);

  cmd(0x11); delay(120); // sleep out

  // bunch of power/frmctrl configs
  cmd(0xB1); dat(0x01); dat(0x2C); dat(0x2D);
  cmd(0xB2); dat(0x01); dat(0x2C); dat(0x2D);
  cmd(0xB3); dat(0x01); dat(0x2C); dat(0x2D); dat(0x01); dat(0x2C); dat(0x2D);
  cmd(0xB4); dat(0x07);
  cmd(0xC0); dat(0xA2); dat(0x02); dat(0x84);
  cmd(0xC1); dat(0xC5);
  cmd(0xC2); dat(0x0A); dat(0x00);
  cmd(0xC3); dat(0x8A); dat(0x2A);
  cmd(0xC4); dat(0x8A); dat(0xEE);
  cmd(0xC5); dat(0x0E);

  cmd(0x3A); dat(0x05);   // 16-bit (rgb565)
  cmd(0x36); dat(0xC8);   // ориентация + bgr
  cmd(0x29); delay(20);   // display on

  setAW(0, 0, 127, 159);
}

// ---------- примитивы рисования ----------

// заливка всего экрана одним цветом
void fillScreen(uint16_t color){
  setAW(0, 0, 127, 159);
  CS_L(); DC_H();
  for (uint32_t i = 0; i < 128UL * 160UL; ++i){
    spiWrite(color >> 8);
    spiWrite(color & 0xFF);
  }
  CS_H();
}

// горизонтальная линия 
void drawHLine(int x, int y, int w, uint16_t color){
  if (w <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w - 1 > 127) w = 128 - x;
  if (y < 0 || y > 159) return;

  setAW(x, y, x + w - 1, y);
  CS_L(); DC_H();
  for (int i = 0; i < w; ++i){
    spiWrite(color >> 8);
    spiWrite(color & 0xFF);
  }
  CS_H();
}

// вертикальная линия
void drawVLine(int x, int y, int h, uint16_t color){
  if (h <= 0) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h - 1 > 159) h = 160 - y;
  if (x < 0 || x > 127) return;

  setAW(x, y, x, y + h - 1);
  CS_L(); DC_H();
  for (int i = 0; i < h; ++i){
    spiWrite(color >> 8);
    spiWrite(color & 0xFF);
  }
  CS_H();
}

// прямоугольник рамкой
void drawRect(int x, int y, int w, int h, uint16_t color){
  if (w <= 0 || h <= 0) return;
  drawHLine(x, y, w, color);
  drawHLine(x, y + h - 1, w, color);
  drawVLine(x, y, h, color);
  drawVLine(x + w - 1, y, h, color);
}

// заливка прямоугольника
void fillRect(int x, int y, int w, int h, uint16_t color){
  if (w <= 0 || h <= 0) return;
  if (x < 0){ w += x; x = 0; }
  if (y < 0){ h += y; y = 0; }
  if (x + w > 128) w = 128 - x;
  if (y + h > 160) h = 160 - y;
  if (w <= 0 || h <= 0) return;

  setAW(x, y, x + w - 1, y + h - 1);
  CS_L(); DC_H();
  for (uint32_t i = 0; i < (uint32_t)w * h; ++i){
    spiWrite(color >> 8);
    spiWrite(color & 0xFF);
  }
  CS_H();
}

// рамка экрана
inline void drawBorder(){
  drawRect(0, 0, 128, 160, COLOR_BORDER);
}

/* ---------- маленький “шрифт” 3x5 для hud ----------
   тут по сути набор битовых масок, где 1 это пиксель, 0 пусто
   рисуем каждую точку квадратиком 2x2, чтобы было видно
*/

const uint8_t GLYPH_0[5] = {0b111,0b101,0b101,0b101,0b111};
const uint8_t GLYPH_1[5] = {0b010,0b110,0b010,0b010,0b111};
const uint8_t GLYPH_2[5] = {0b111,0b001,0b111,0b100,0b111};
const uint8_t GLYPH_3[5] = {0b111,0b001,0b111,0b001,0b111};
const uint8_t GLYPH_4[5] = {0b101,0b101,0b111,0b001,0b001};
const uint8_t GLYPH_5[5] = {0b111,0b100,0b111,0b001,0b111};
const uint8_t GLYPH_6[5] = {0b111,0b100,0b111,0b101,0b111};
const uint8_t GLYPH_7[5] = {0b111,0b001,0b010,0b010,0b010};
const uint8_t GLYPH_8[5] = {0b111,0b101,0b111,0b101,0b111};
const uint8_t GLYPH_9[5] = {0b111,0b101,0b111,0b001,0b111};

const uint8_t GLYPH_S[5] = {0b111,0b100,0b111,0b001,0b111};
const uint8_t GLYPH_C[5] = {0b111,0b100,0b100,0b100,0b111};
const uint8_t GLYPH_O[5] = {0b111,0b101,0b101,0b101,0b111};
const uint8_t GLYPH_R[5] = {0b110,0b101,0b110,0b101,0b101};
const uint8_t GLYPH_E[5] = {0b111,0b100,0b111,0b100,0b111};

const uint8_t GLYPH_G[5] = {0b111,0b100,0b101,0b101,0b111};
const uint8_t GLYPH_A[5] = {0b010,0b101,0b111,0b101,0b101};
const uint8_t GLYPH_M[5] = {0b101,0b111,0b111,0b101,0b101};
const uint8_t GLYPH_SPACE[5] = {0,0,0,0,0};

// выбираем нужный символ по букве
const uint8_t* getGlyph(char ch){
  switch(ch){
    case '0': return GLYPH_0;
    case '1': return GLYPH_1;
    case '2': return GLYPH_2;
    case '3': return GLYPH_3;
    case '4': return GLYPH_4;
    case '5': return GLYPH_5;
    case '6': return GLYPH_6;
    case '7': return GLYPH_7;
    case '8': return GLYPH_8;
    case '9': return GLYPH_9;
    case 'S': return GLYPH_S;
    case 'C': return GLYPH_C;
    case 'O': return GLYPH_O;
    case 'R': return GLYPH_R;
    case 'E': return GLYPH_E;
    case 'G': return GLYPH_G;
    case 'A': return GLYPH_A;
    case 'M': return GLYPH_M;
    case ' ': return GLYPH_SPACE;
    default:  return GLYPH_SPACE;
  }
}

// рисуем один символ
void drawCharSmall(int x, int y, char ch, uint16_t color){
  const uint8_t* g = getGlyph(ch);
  int pxSize = 2; 
  for (int row = 0; row < 5; ++row){
    uint8_t line = g[row];
    for (int col = 0; col < 3; ++col){
      if (line & (1 << (2 - col))){
        fillRect(x + col*pxSize, y + row*pxSize, pxSize, pxSize, color);
      }
    }
  }
}

// печатаем строку
void drawTextSmall(int x, int y, const char* s, uint16_t color){
  int cursorX = x;
  while (*s){
    drawCharSmall(cursorX, y, *s, color);
    cursorX += 7; // шаг между буквами (3*2 + пробелы)
    ++s;
  }
}

// печатаем число 
void drawNumberSmall(int x, int y, int value, uint16_t color){
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", value);
  drawTextSmall(x, y, buf, color);
}

/* ---------- фигуры (тетримино) ----------

  каждая фигура хранится как 4 блока внутри “квадрата 4x4”
  так удобно крутить по формуле и проверять коллизии

  pieces[] хранит базовую ориентацию (rot=0)
*/

enum {
  PIECE_O = 0,
  PIECE_I,
  PIECE_Z,
  PIECE_S,
  PIECE_L,
  PIECE_J,
  PIECE_T,
  PIECE_COUNT
};

struct PieceDef {
  uint8_t x[4];
  uint8_t y[4];
};

// базовые формы в 4x4
const PieceDef pieces[PIECE_COUNT] = {
  // O
  { {1,2,1,2}, {1,1,2,2} },
  // I 
  { {0,1,2,3}, {1,1,1,1} },
  // Z
  { {0,1,1,2}, {1,1,2,2} },
  // S
  { {1,2,0,1}, {1,1,2,2} },
  // L
  { {0,0,0,1}, {0,1,2,2} },
  // J
  { {1,1,1,0}, {0,1,2,2} },
  // T
  { {0,1,2,1}, {1,1,1,2} }
};

// текущая активная фигура
int curPiece = 0;
int nextPiece = 0;
int curX = 0, curY = 0;   // позиция “левый верх” квадрата 4x4 в координатах поля
uint8_t curRot = 0;       // 0..3
bool hasActive = false;
bool gameOver = false;

// игровой прогресс
int score = 0;
int linesCleared = 0;
int level = 1;

// тайминги падения/движения
unsigned long lastFallMs = 0;
unsigned long fallInterval = 500;          // скорость падения (мс на шаг)
const unsigned long baseFallInterval = 500;
const unsigned long minFallInterval  = 80;

unsigned long lastSideMoveMs = 0;
const unsigned long sideMoveInterval = 120; // автоповтор для влево/вправо

// логика кнопки rotate/reset на PA11
bool resetPrev = false;
unsigned long resetPressStart = 0;
bool resetLongDone = false;

void redrawField();
void drawHUD();
void drawNextPiecePreview();
void updateScoreAndLevel(int cleared);
void showGameOverScreen();
void rotateCurrentPiece();

// рисуем одну клетку поля по координатам в клетках
void drawCell(int cx, int cy, uint16_t color){
  fillRect(cx * CELL_SIZE, cy * CELL_SIZE, CELL_SIZE, CELL_SIZE, color);
}

// получаем координаты блока фигуры после поворота
// rot: 0..3, поворот по часовой
inline void getRotatedPos(const PieceDef &p, int idx, uint8_t rot, int &rx, int &ry){
  uint8_t x0 = p.x[idx];
  uint8_t y0 = p.y[idx];

  // для 4x4:
  // 90°: (x,y)->(3-y,x)
  // 180°: (x,y)->(3-x,3-y)
  // 270°: (x,y)->(y,3-x)
  switch (rot & 3){
    case 0: rx = x0;       ry = y0;       break;
    case 1: rx = 3 - y0;   ry = x0;       break;
    case 2: rx = 3 - x0;   ry = 3 - y0;   break;
    case 3: rx = y0;       ry = 3 - x0;   break;
  }
}

// проверяем коллизию: можно ли поставить фигуру в cx/cy с поворотом rot
// тут же проверяем выход за границы поля
bool canPlace(int piece, int cx, int cy, uint8_t rot){
  const PieceDef &p = pieces[piece];
  for (int i = 0; i < 4; ++i){
    int rx, ry;
    getRotatedPos(p, i, rot, rx, ry);
    int x = cx + rx;
    int y = cy + ry;

    // вылезли за поле -> нельзя
    if (x < 0 || x >= FIELD_W || y < 0 || y >= FIELD_H) return false;
    // врезались в уже занятые клетки -> нельзя
    if (field[y][x]) return false;
  }
  return true;
}

// рисуем фигуру (4 блока) указанным цветом
// когда двигаем/крутим, сначала рисуем старое место цветом фона, потом новое цветом фигуры
void drawPiece(int piece, int cx, int cy, uint8_t rot, uint16_t color){
  const PieceDef &p = pieces[piece];
  for (int i = 0; i < 4; ++i){
    int rx, ry;
    getRotatedPos(p, i, rot, rx, ry);
    int x = cx + rx;
    int y = cy + ry;
    if (x >= 0 && x < FIELD_W && y >= 0 && y < FIELD_H){
      drawCell(x, y, color);
    }
  }
  // рамку всегда возвращаем поверх
  drawBorder();
}

// “приклеиваем” фигуру в field[][] после того как дальше падать нельзя
void lockPiece(int piece, int cx, int cy, uint8_t rot){
  const PieceDef &p = pieces[piece];
  for (int i = 0; i < 4; ++i){
    int rx, ry;
    getRotatedPos(p, i, rot, rx, ry);
    int x = cx + rx;
    int y = cy + ry;
    if (x >= 0 && x < FIELD_W && y >= 0 && y < FIELD_H){
      field[y][x] = 1;
    }
  }
}

// чистим поле в нули
void clearField(){
  for (int r = 0; r < FIELD_H; ++r){
    for (int c = 0; c < FIELD_W; ++c){
      field[r][c] = 0;
    }
  }
}

void drawHUD(){
  fillRect(0, 0, 128, 18, COLOR_BG);
  drawTextSmall(2, 2, "SCORE", COLOR_BORDER);
  drawNumberSmall(2, 10, score, COLOR_BORDER);
}

// превью следующей фигуры (рисуем всегда в rot=0)
void drawNextPiecePreview(){
  // правый верхний угол, квадрат 32x32
  int px = 96;
  int py = 0;
  int w  = 32;
  int h  = 32;

  fillRect(px, py, w, h, COLOR_BG);
  drawRect(px, py, w, h, COLOR_BORDER);

  const PieceDef &p = pieces[nextPiece];
  int blockSize = 4; // мелкий размер блока для превью
  for (int i = 0; i < 4; ++i){
    int bx = px + 4 + p.x[i] * blockSize;
    int by = py + 4 + p.y[i] * blockSize;
    fillRect(bx, by, blockSize, blockSize, COLOR_PIECE);
  }
}

// полная перерисовка поля
void redrawField(){
  for (int y = 0; y < FIELD_H; ++y){
    int y0 = y * CELL_SIZE;
    int y1 = y0 + CELL_SIZE - 1;

    // окно: вся ширина поля, одна “полоса” по высоте
    setAW(0, y0, FIELD_W * CELL_SIZE - 1, y1);
    CS_L();
    DC_H();

    // каждый ряд клетки заполняем одинаковым цветом (фон или блок)
    for (int rowPix = 0; rowPix < CELL_SIZE; ++rowPix){
      for (int x = 0; x < FIELD_W; ++x){
        uint16_t col = field[y][x] ? COLOR_PIECE : COLOR_BG;
        uint8_t hi = col >> 8;
        uint8_t lo = col & 0xFF;

        // 8 пикс по ширине на клетку
        for (int px = 0; px < CELL_SIZE; ++px){
          spiWrite(hi);
          spiWrite(lo);
        }
      }
    }

    CS_H();
  }

  // поверх поля рисуем интерфейс
  drawHUD();
  drawNextPiecePreview();
  drawBorder();
}

// поиск заполненных линий и удаление
// идём снизу вверх
void clearFullLines(){
  int linesThisPass = 0;

  for (int y = FIELD_H - 1; y >= 0; --y){
    bool full = true;
    for (int x = 0; x < FIELD_W; ++x){
      if (!field[y][x]) { full = false; break; }
    }

    if (full){
      ++linesThisPass;

      // сдвигаем всё выше вниз на одну строку
      for (int yy = y; yy > 0; --yy){
        for (int x = 0; x < FIELD_W; ++x){
          field[yy][x] = field[yy-1][x];
        }
      }

      // верхнюю строку делаем пустой
      for (int x = 0; x < FIELD_W; ++x){
        field[0][x] = 0;
      }
      y++;
    }
  }

  if (linesThisPass > 0){
    updateScoreAndLevel(linesThisPass);
    redrawField(); 
  }
}

// очки/уровень/скорость
void updateScoreAndLevel(int cleared){
  if (cleared <= 0) return;

  switch (cleared){
    case 1: score += 100; break;
    case 2: score += 300; break;
    case 3: score += 500; break;
    case 4: score += 800; break;
    default: score += cleared * 100; break;
  }

  linesCleared += cleared;

  // каждые 10 линий +1 уровень, скорость быстрее
  int newLevel = 1 + linesCleared / 10;
  if (newLevel != level){
    level = newLevel;

    // интервал уменьшаем на 40мс за уровень, но не быстрее minFallInterval
    long tmp = (long)baseFallInterval - (level - 1) * 40;
    if (tmp < (long)minFallInterval) tmp = minFallInterval;
    fallInterval = (unsigned long)tmp;
  }
}

// game over экран
void showGameOverScreen(){
  fillScreen(0x0000);
  drawBorder();
  drawTextSmall(22, 60, "GAME OVER", COLOR_BORDER);
  drawTextSmall(22, 80, "SCORE", COLOR_BORDER);
  drawNumberSmall(22, 88, score, COLOR_BORDER);
}

// рандом фигуры (arduino random)
int randomPiece(){
  return random(0, PIECE_COUNT);
}

// поворот текущей фигуры
// делаем “попробовать повернуть”, если нельзя (стена/блоки) то просто игнор
void rotateCurrentPiece(){
  if (!hasActive || gameOver) return;

  uint8_t newRot = (curRot + 1) & 3;
  if (canPlace(curPiece, curX, curY, newRot)){
    drawPiece(curPiece, curX, curY, curRot, COLOR_BG);
    curRot = newRot;
    drawPiece(curPiece, curX, curY, curRot, COLOR_PIECE);
  }
}

// спавн новой фигуры
void spawnPiece(){
  curPiece = nextPiece;
  nextPiece = randomPiece();
  curRot = 0;

  // ставим 4x4 примерно по центру поля
  curX = FIELD_W / 2 - 2;
  curY = 0;

  lastFallMs = millis();

  // если сразу не помещается, значит всё, гейм овер
  if (!canPlace(curPiece, curX, curY, curRot)){
    gameOver = true;
    hasActive = false;
    showGameOverScreen();
    return;
  }

  drawPiece(curPiece, curX, curY, curRot, COLOR_PIECE);
  drawNextPiecePreview();
  hasActive = true;
}

/* ------------------ reset / setup / loop ------------------ */

// полный сброс игры
void resetGame() {
  gameOver = false;
  hasActive = false;

  score = 0;
  linesCleared = 0;
  level = 1;
  fallInterval = baseFallInterval;

  lastFallMs = millis();
  lastSideMoveMs = millis();

  // состояние PA11 читаем сразу, чтобы не словить “ложное нажатие” на старте
  resetPrev = !digitalRead(PA11); // активный low
  resetPressStart = 0;
  resetLongDone = false;

  clearField();

  fillScreen(COLOR_BG);
  drawBorder();

  // заранее генерим nextPiece, потом spawn подхватит
  nextPiece = randomPiece();
  redrawField();
}

void setup(){
#ifdef TFT_BLK
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);
#endif

  // дисплейные пины
  pinMode(TFT_SCLK, OUTPUT);
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(TFT_CS,   OUTPUT);
  pinMode(TFT_DC,   OUTPUT);
  pinMode(TFT_RST,  OUTPUT);

  // кнопки: все на pullup, нажато = 0
  pinMode(PA8,  INPUT_PULLUP);  // left
  pinMode(PA9,  INPUT_PULLUP);  // down (ускорение)
  pinMode(PA10, INPUT_PULLUP);  // right
  pinMode(PA11, INPUT_PULLUP);  // rotate/reset

  // стартовые уровни на линиях
  SCLK_L(); MOSI_L(); CS_H(); DC_H(); RST_H();
  delay(50);

  st7735s_init();

  randomSeed(analogRead(0));

  resetGame();
}

void loop(){
  unsigned long now = millis();

  // ---------- кнопка PA11: коротко = rotate, долго = reset ----------
  // читаем активный low
  bool resetNow = !digitalRead(PA11);

  // поймали момент “нажали”
  if (resetNow && !resetPrev) {
    resetPressStart = now;
    resetLongDone = false;
  }

  // удержание
  if (resetNow && resetPrev) {
    if (!resetLongDone && (now - resetPressStart >= 5000)) {
      resetGame();          // 5 сек держим -> новая игра
      resetLongDone = true; 
    }
  }

  // отпустили
  if (!resetNow && resetPrev) {
    unsigned long held = now - resetPressStart;
    if (!resetLongDone && held < 5000) {
      // короткое -> поворот
      rotateCurrentPiece();
    }
  }

  resetPrev = resetNow;

  // если game over, ничего не делаем, только ждём 
  if (gameOver){
    delay(50);
    return;
  }

  // если нет активной фигуры, создаём новую
  if (!hasActive){
    spawnPiece();
    if (gameOver) return;
  }

  // ---------- движение влево/вправо ----------
  // автоповтор через sideMoveInterval
  if (!digitalRead(PA8) && (now - lastSideMoveMs > sideMoveInterval)){
    if (canPlace(curPiece, curX - 1, curY, curRot)){
      drawPiece(curPiece, curX, curY, curRot, COLOR_BG);
      curX -= 1;
      drawPiece(curPiece, curX, curY, curRot, COLOR_PIECE);
    }
    lastSideMoveMs = now;
  }

  if (!digitalRead(PA10) && (now - lastSideMoveMs > sideMoveInterval)){
    if (canPlace(curPiece, curX + 1, curY, curRot)){
      drawPiece(curPiece, curX, curY, curRot, COLOR_BG);
      curX += 1;
      drawPiece(curPiece, curX, curY, curRot, COLOR_PIECE);
    }
    lastSideMoveMs = now;
  }

  // ---------- падение ----------
  // если держим вниз, делаем интервал меньше 
  unsigned long interval = fallInterval;
  if (!digitalRead(PA9)){
    interval = 60;
  }

  if (now - lastFallMs >= interval){
    lastFallMs = now;

    // пробуем опустить на 1 клетку
    if (canPlace(curPiece, curX, curY + 1, curRot)){
      drawPiece(curPiece, curX, curY, curRot, COLOR_BG);
      curY += 1;
      drawPiece(curPiece, curX, curY, curRot, COLOR_PIECE);
    } else {
      // дальше вниз нельзя -> приклеиваем и чистим линии
      lockPiece(curPiece, curX, curY, curRot);
      hasActive = false;
      clearFullLines();
    }
  }
}
