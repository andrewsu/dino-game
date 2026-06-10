// Dino game for Arduino UNO R4 WiFi built-in 8x12 LED matrix
// Connect a momentary button between pin 2 and GND (uses INPUT_PULLUP)

#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

// ── Layout ────────────────────────────────────────────────
// Row 7 = ground, rows 0–6 = play area
// Dino: 4×4 px sprite at cols 0–3, standing rows 3–6
// Obstacle: 1–3 px tall, scrolls right→left
// ─────────────────────────────────────────────────────────

const int BUTTON_PIN  = 2;
const int GROUND_ROW  = 7;
const int DINO_COL    = 0;
const int DINO_H      = 4;
const int STAND_TOP   = GROUND_ROW - DINO_H;  // row 3

// Two run frames — rows 1 & 3 swap to animate legs/arms
// Pattern supplied by user:
//   0011   head
//   1010   upper body / arm
//   1111   torso
//   0101   legs
const uint8_t DINO_PIXELS[2][4][4] = {
  {                    // frame 0
    {0, 0, 1, 1},
    {1, 0, 1, 0},
    {1, 1, 1, 1},
    {0, 1, 0, 1},
  },
  {                    // frame 1 — rows 1 & 3 swapped
    {0, 0, 1, 1},
    {0, 1, 0, 1},
    {1, 1, 1, 1},
    {1, 0, 1, 0},
  }
};

// Jump arc: how many rows above STAND_TOP the dino top rises each phase
const int JUMP_ARC[]  = { 1, 2, 3, 3, 3, 3, 2, 1 };
const int JUMP_FRAMES = 8;

// ── State ─────────────────────────────────────────────────
uint8_t frame[8][12];

int  dinoTopRow = STAND_TOP;
int  jumpPhase  = -1;   // -1 = grounded
int  runFrame   = 0;    // 0 or 1

int  obstacleCol = 11;
int  obstacleH   = 2;   // 1–3 rows tall

int  score     = 0;
bool gameOver  = false;

unsigned long gameSpeed = 150;  // ms per game tick
unsigned long lastTick  = 0;

// ── Draw helpers ──────────────────────────────────────────

void clearFrame() {
  memset(frame, 0, sizeof(frame));
}

void drawGround() {
  for (int c = 0; c < 12; c++) frame[GROUND_ROW][c] = 1;
}

void drawDino() {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (!DINO_PIXELS[runFrame][r][c]) continue;
      int absRow = dinoTopRow + r;
      int absCol = DINO_COL   + c;
      if (absRow >= 0 && absRow < 8 && absCol < 12)
        frame[absRow][absCol] = 1;
    }
  }
}

void drawObstacle() {
  if (obstacleCol < 0 || obstacleCol >= 12) return;
  int obsTop = GROUND_ROW - obstacleH;
  for (int r = obsTop; r < GROUND_ROW; r++)
    frame[r][obstacleCol] = 1;
}

void renderFrame() {
  clearFrame();
  drawGround();
  drawDino();
  drawObstacle();
  matrix.renderBitmap(frame, 8, 12);
}

// ── Pixel-accurate collision ───────────────────────────────

bool checkCollision() {
  int relCol = obstacleCol - DINO_COL;
  if (relCol < 0 || relCol >= 4) return false;

  int obsTop = GROUND_ROW - obstacleH;
  for (int r = obsTop; r < GROUND_ROW; r++) {
    int relRow = r - dinoTopRow;
    if (relRow >= 0 && relRow < 4 && DINO_PIXELS[runFrame][relRow][relCol])
      return true;
  }
  return false;
}

// ── Obstacle ──────────────────────────────────────────────

void spawnObstacle() {
  obstacleCol = 11;
  obstacleH   = random(1, 4);  // 1, 2, or 3
}

// ── Game tick ─────────────────────────────────────────────

void gameTick() {
  // Advance jump arc
  if (jumpPhase >= 0) {
    jumpPhase++;
    if (jumpPhase >= JUMP_FRAMES) {
      jumpPhase  = -1;
      dinoTopRow = STAND_TOP;
    } else {
      dinoTopRow = STAND_TOP - JUMP_ARC[jumpPhase];
    }
  }

  // Alternate run frame every tick
  runFrame ^= 1;

  // Scroll obstacle
  obstacleCol--;
  if (obstacleCol < 0) {
    score++;
    if (gameSpeed > 60) gameSpeed -= 8;
    spawnObstacle();
  }

  if (checkCollision()) gameOver = true;
}

// ── Game over animation ────────────────────────────────────

void showGameOver() {
  Serial.print("Game Over! Score: ");
  Serial.println(score);
  for (int i = 0; i < 5; i++) {
    clearFrame();
    matrix.renderBitmap(frame, 8, 12);
    delay(250);
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 12; c++)
        frame[r][c] = 1;
    matrix.renderBitmap(frame, 8, 12);
    delay(250);
  }
  clearFrame();
  matrix.renderBitmap(frame, 8, 12);
}

// ── Reset ─────────────────────────────────────────────────

void resetGame() {
  dinoTopRow = STAND_TOP;
  jumpPhase  = -1;
  runFrame   = 0;
  score      = 0;
  gameOver   = false;
  gameSpeed  = 150;
  spawnObstacle();
  lastTick   = millis();
}

// ── Arduino entry points ───────────────────────────────────

void setup() {
  matrix.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(9600);
  randomSeed(analogRead(A0));
  spawnObstacle();
  lastTick = millis();
}

void loop() {
  if (gameOver) {
    showGameOver();
    while (digitalRead(BUTTON_PIN) == HIGH) {}
    delay(200);
    resetGame();
    return;
  }

  // Jump input — checked every loop for immediacy
  if (digitalRead(BUTTON_PIN) == LOW && jumpPhase == -1) {
    jumpPhase  = 0;
    dinoTopRow = STAND_TOP - JUMP_ARC[0];
    delay(50);  // debounce
  }

  // Game tick at fixed interval
  unsigned long now = millis();
  if (now - lastTick >= gameSpeed) {
    lastTick = now;
    gameTick();
  }

  renderFrame();
  delay(10);
}
