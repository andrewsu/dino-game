// Dino game for Arduino UNO R4 WiFi built-in 8x12 LED matrix
// Connect a momentary button between pin 2 and GND (uses INPUT_PULLUP)

#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

// ── Layout ────────────────────────────────────────────────
// Row 7 = ground, rows 0–6 = play area
// Dino: 2 px tall, column 1 (standing rows 5–6)
// Obstacle: 1–2 px tall, scrolls right→left
// ─────────────────────────────────────────────────────────

const int BUTTON_PIN  = 2;
const int GROUND_ROW  = 7;
const int DINO_COL    = 1;
const int STAND_TOP   = GROUND_ROW - 2;  // row 5

// Jump arc: how far (in rows) the dino top rises above STAND_TOP each phase
const int JUMP_ARC[]  = { 2, 4, 5, 5, 4, 2 };
const int JUMP_FRAMES = 6;

// ── State ─────────────────────────────────────────────────
uint8_t  frame[8][12];

int  dinoTopRow  = STAND_TOP;
int  jumpPhase   = -1;   // -1 = grounded

int  obstacleCol = 11;
int  obstacleH   = 1;    // 1 or 2 rows tall

int  score       = 0;
bool gameOver    = false;

unsigned long gameSpeed = 200;  // ms per game tick
unsigned long lastTick  = 0;

// ── Helpers ───────────────────────────────────────────────

void clearFrame() {
  memset(frame, 0, sizeof(frame));
}

void drawGround() {
  for (int c = 0; c < 12; c++) frame[GROUND_ROW][c] = 1;
}

void drawDino() {
  for (int r = dinoTopRow; r <= dinoTopRow + 1; r++)
    if (r >= 0 && r < 8) frame[r][DINO_COL] = 1;
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

bool checkCollision() {
  if (obstacleCol != DINO_COL) return false;
  int obsTop = GROUND_ROW - obstacleH;  // row 6 or 5
  return (dinoTopRow + 1) >= obsTop;    // dino bottom at or below obstacle top
}

void spawnObstacle() {
  obstacleCol = 11;
  obstacleH   = random(1, 3);  // 1 or 2
}

// ── Game tick (runs at gameSpeed interval) ─────────────────

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

  // Scroll obstacle left
  obstacleCol--;
  if (obstacleCol < 0) {
    score++;
    if (gameSpeed > 80) gameSpeed -= 10;
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
  score      = 0;
  gameOver   = false;
  gameSpeed  = 200;
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
    // Wait for button press to restart
    while (digitalRead(BUTTON_PIN) == HIGH) {}
    delay(200);
    resetGame();
    return;
  }

  // Jump input — check every loop so the response is immediate
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
