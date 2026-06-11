// Dino game for Arduino UNO R4 WiFi built-in 8x12 LED matrix
// Connect a momentary button between pin 8 and GND (uses INPUT_PULLUP)

#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

uint8_t frame[8][12];

// ── Layout ────────────────────────────────────────────────
// Row 7 = invisible floor (ground line removed)
// Dino: 4×4 sprite, cols 0–3, standing rows 4–7
// Obstacle: cactus pillar + 4-wide platform on top, scrolls right→left
// ─────────────────────────────────────────────────────────

const int BUTTON_PIN     = 8;
const int GROUND_ROW     = 7;           // invisible floor row
const int DINO_COL       = 0;
const int DINO_H         = 4;
const int STAND_TOP      = GROUND_ROW - DINO_H + 1;  // row 4 (feet at row 7)
const int PLATFORM_WIDTH = 4;

// Two run frames — rows 1 & 3 swap each tick for running animation
const uint8_t DINO_PIXELS[2][4][4] = {
  {
    {0, 0, 1, 1},   // head
    {1, 0, 1, 0},   // upper body / arm
    {1, 1, 1, 1},   // torso
    {0, 1, 0, 1},   // legs A
  },
  {
    {0, 0, 1, 1},
    {0, 1, 0, 1},   // swapped
    {1, 1, 1, 1},
    {1, 0, 1, 0},   // swapped
  }
};

// Jump arc: peak elevation = 4 rows (dinoTopRow goes 4→3→2→1→0→0→1→2→3→4)
const int JUMP_ARC[]  = { 1, 2, 3, 4, 4, 3, 2, 1 };
const int JUMP_FRAMES = 8;

// ── State ─────────────────────────────────────────────────

typedef enum { DINO_GROUNDED, DINO_JUMPING, DINO_ON_PLATFORM, DINO_FALLING } DinoState;

int       dinoTopRow = STAND_TOP;
int       jumpPhase  = -1;
int       runFrame   = 0;
DinoState dinoState  = DINO_GROUNDED;

int  obstacleCol = 11;
int  obstacleH   = 2;    // 1–3 rows tall

int  score    = 0;
bool gameOver = false;

unsigned long gameSpeed = 150;   // ms per game tick
unsigned long lastTick  = 0;

// ── Draw helpers ──────────────────────────────────────────

void clearFrame() { memset(frame, 0, sizeof(frame)); }

void drawDino() {
  for (int r = 0; r < DINO_H; r++) {
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
  if (obstacleCol >= 12) return;
  int obsTop = GROUND_ROW - obstacleH;
  // Horizontal platform across the top
  for (int c = obstacleCol - PLATFORM_WIDTH + 1; c <= obstacleCol; c++)
    if (c >= 0 && c < 12) frame[obsTop][c] = 1;
  // Vertical cactus body below the platform
  for (int r = obsTop + 1; r < GROUND_ROW; r++)
    if (obstacleCol >= 0) frame[r][obstacleCol] = 1;
}

void renderFrame() {
  clearFrame();
  drawDino();
  drawObstacle();
  matrix.renderBitmap(frame, 8, 12);
}

// ── Pixel-accurate collision (cactus column, full height) ─

bool checkCollision() {
  if (dinoState == DINO_ON_PLATFORM) return false;
  int relCol = obstacleCol - DINO_COL;
  if (relCol < 0 || relCol >= 4) return false;
  int obsTop = GROUND_ROW - obstacleH;
  for (int r = obsTop; r < GROUND_ROW; r++) {
    int relRow = r - dinoTopRow;
    if (relRow >= 0 && relRow < DINO_H && DINO_PIXELS[runFrame][relRow][relCol])
      return true;
  }
  return false;
}

// ── Platform landing ──────────────────────────────────────
// Called after dino position update; snaps dino onto the platform
// if it has descended far enough and the obstacle is in horizontal range.

void tryLandOnPlatform() {
  bool descending = (dinoState == DINO_FALLING) ||
                    (dinoState == DINO_JUMPING && jumpPhase >= JUMP_FRAMES / 2);
  if (!descending) return;

  int obsTop    = GROUND_ROW - obstacleH;
  int targetTop = obsTop - DINO_H;   // dinoTopRow when feet rest on platform

  // Platform x-range [obstacleCol-3 .. obstacleCol] must overlap dino cols [0..3]
  bool xOverlap = (obstacleCol >= DINO_COL) &&
                  (obstacleCol - PLATFORM_WIDTH + 1 <= DINO_COL + 3);
  if (!xOverlap) return;

  // Dino bottom has reached the platform surface
  if (dinoTopRow + DINO_H - 1 >= obsTop && dinoTopRow < STAND_TOP) {
    dinoTopRow = targetTop;
    dinoState  = DINO_ON_PLATFORM;
    jumpPhase  = -1;
  }
}

// ── Obstacle ─────────────────────────────────────────────

void spawnObstacle() {
  obstacleCol = 11;
  obstacleH   = random(1, 4);  // 1, 2, or 3
}

// ── Game tick ────────────────────────────────────────────

void gameTick() {
  // Scroll obstacle (do this first so landing/collision use the new position)
  obstacleCol--;
  if (obstacleCol < 0) {
    if (dinoState == DINO_ON_PLATFORM) dinoState = DINO_FALLING;
    score++;
    if (gameSpeed > 60) gameSpeed -= 8;
    spawnObstacle();
  }

  // Update dino vertical position
  switch (dinoState) {
    case DINO_JUMPING:
      jumpPhase++;
      if (jumpPhase >= JUMP_FRAMES) {
        jumpPhase  = -1;
        dinoTopRow = STAND_TOP;
        dinoState  = DINO_GROUNDED;
      } else {
        dinoTopRow = STAND_TOP - JUMP_ARC[jumpPhase];
      }
      break;

    case DINO_FALLING:
      if (dinoTopRow < STAND_TOP) {
        dinoTopRow++;
      } else {
        dinoTopRow = STAND_TOP;
        dinoState  = DINO_GROUNDED;
      }
      break;

    default:
      break;
  }

  tryLandOnPlatform();

  runFrame ^= 1;

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
  dinoState  = DINO_GROUNDED;
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

  // Jump — only from ground (not from platform or mid-air)
  if (digitalRead(BUTTON_PIN) == LOW && dinoState == DINO_GROUNDED) {
    jumpPhase  = 0;
    dinoTopRow = STAND_TOP - JUMP_ARC[0];
    dinoState  = DINO_JUMPING;
    delay(50);   // debounce
  }

  unsigned long now = millis();
  if (now - lastTick >= gameSpeed) {
    lastTick = now;
    gameTick();
  }

  renderFrame();
  delay(10);
}
