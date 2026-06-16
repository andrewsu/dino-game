// Dino game for Arduino UNO R4 WiFi built-in 8x12 LED matrix
// Connect a momentary button between pin 8 and GND (uses INPUT_PULLUP)

#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

uint8_t frame[8][12];

// ── Layout ────────────────────────────────────────────────
// Row 7 = bottom row (dino feet and cactus bases both rest here)
// Dino: 4×4 sprite, cols 0–3, standing rows 4–7
// Obstacle: cactus pillar; ~half also carry a 4-wide platform on top,
// with the pillar dropping from a random column under the platform
// ─────────────────────────────────────────────────────────

const int BUTTON_PIN     = 8;
const int GROUND_ROW     = 7;           // bottom row
const int DINO_COL       = 0;
const int DINO_H         = 4;
const int STAND_TOP      = GROUND_ROW - DINO_H + 1;  // row 4 (feet at row 7)
const int PLATFORM_WIDTH = 4;

// Random number of idle ticks between one obstacle leaving and the next
const int GAP_MIN_TICKS  = 3;
const int GAP_MAX_TICKS  = 13;

// Two run frames — rows 1 & 3 swap each tick for running animation
const uint8_t DINO_PIXELS[2][4][4] = {
  {
    {0, 0, 1, 1},   // head
    {1, 0, 1, 0},   // upper body / arm
    {1, 1, 1, 1},   // torso
    {0, 0, 0, 1},   // legs A
  },
  {
    {0, 0, 1, 1},
    {1, 0, 1, 0},
    {1, 1, 1, 1},
    {0, 1, 0, 0},   // swapped
  }
};

// Jump arc: peak elevation = 4 rows above the launch row
const int JUMP_ARC[]  = { 1, 2, 3, 4, 4, 3, 2, 1 };
const int JUMP_FRAMES = 8;

// ── State ─────────────────────────────────────────────────

typedef enum { DINO_GROUNDED, DINO_JUMPING, DINO_ON_PLATFORM, DINO_FALLING } DinoState;

int       dinoTopRow  = STAND_TOP;
int       jumpPhase   = -1;
int       jumpBaseRow = STAND_TOP;   // row the current jump launched from
int       runFrame    = 0;
DinoState dinoState   = DINO_GROUNDED;

int  obstacleCol         = 11;   // cactus pillar column
int  obstacleH           = 2;    // 1–3 rows tall (bottom rests on GROUND_ROW)
bool obstacleHasPlatform = false;
int  obstacleStemOffset  = 0;    // pillar position within platform [0..PLATFORM_WIDTH-1]
bool obstacleActive      = true;
int  gapTicks            = 0;    // idle ticks remaining before next spawn

int  score    = 0;
bool gameOver = false;

unsigned long gameSpeed = 150;   // ms per game tick
unsigned long lastTick  = 0;

// ── Geometry helpers ──────────────────────────────────────

// Top row of the obstacle (platform row when present, else cactus top)
int obstacleTopRow() { return GROUND_ROW - obstacleH + 1; }

// Platform horizontal extent (only meaningful when obstacleHasPlatform)
int platformLeftCol()  { return obstacleCol - obstacleStemOffset; }
int platformRightCol() { return obstacleCol + (PLATFORM_WIDTH - 1 - obstacleStemOffset); }

// Rightmost column occupied by the whole obstacle
int obstacleRightCol() {
  return obstacleHasPlatform ? platformRightCol() : obstacleCol;
}

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
  if (!obstacleActive) return;
  int obsTop = obstacleTopRow();

  if (obstacleHasPlatform) {
    // Horizontal platform across the top
    for (int c = platformLeftCol(); c <= platformRightCol(); c++)
      if (c >= 0 && c < 12) frame[obsTop][c] = 1;
    // Vertical cactus body below the platform, down to the bottom row
    for (int r = obsTop + 1; r <= GROUND_ROW; r++)
      if (obstacleCol >= 0 && obstacleCol < 12) frame[r][obstacleCol] = 1;
  } else {
    // Plain cactus pillar (no ledge), full height down to the bottom row
    for (int r = obsTop; r <= GROUND_ROW; r++)
      if (obstacleCol >= 0 && obstacleCol < 12) frame[r][obstacleCol] = 1;
  }
}

void renderFrame() {
  clearFrame();
  drawDino();
  drawObstacle();
  matrix.renderBitmap(frame, 8, 12);
}

// ── Bounding-box collision: any obstacle pixel inside the 4×4 dino grid ─

bool checkCollision() {
  if (!obstacleActive || dinoState == DINO_ON_PLATFORM) return false;

  int obsTop  = obstacleTopRow();
  int dinoBot = dinoTopRow + DINO_H - 1;

  // Cactus pillar: rows (platform ? obsTop+1 : obsTop)..GROUND_ROW at obstacleCol
  int pillarTop = obstacleHasPlatform ? obsTop + 1 : obsTop;
  if (obstacleCol >= DINO_COL && obstacleCol <= DINO_COL + 3) {
    if (pillarTop <= dinoBot && GROUND_ROW >= dinoTopRow)
      return true;
  }

  // Platform arms (only when present): row obsTop across [platLeft..platRight]
  if (obstacleHasPlatform && obsTop >= dinoTopRow && obsTop <= dinoBot) {
    if (platformLeftCol() <= DINO_COL + 3 && platformRightCol() >= DINO_COL)
      return true;
  }

  return false;
}

// ── Platform landing ──────────────────────────────────────
// Called after dino position update; snaps dino onto the platform
// if it has descended far enough and the platform is in horizontal range.

void tryLandOnPlatform() {
  if (!obstacleActive || !obstacleHasPlatform) return;

  bool descending = (dinoState == DINO_FALLING) ||
                    (dinoState == DINO_JUMPING && jumpPhase >= JUMP_FRAMES / 2);
  if (!descending) return;

  int obsTop    = obstacleTopRow();
  int targetTop = obsTop - DINO_H;   // dinoTopRow when feet rest on platform

  // Platform must overlap dino cols [0..3]
  bool xOverlap = (platformLeftCol() <= DINO_COL + 3) &&
                  (platformRightCol() >= DINO_COL);
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
  obstacleH           = random(1, 4);          // 1, 2, or 3
  obstacleHasPlatform = (random(2) == 0);      // ~50% carry a platform
  obstacleStemOffset  = random(0, PLATFORM_WIDTH);  // pillar at random spot

  // Start with the obstacle's right edge at the rightmost column (11)
  if (obstacleHasPlatform)
    obstacleCol = 11 - (PLATFORM_WIDTH - 1 - obstacleStemOffset);
  else
    obstacleCol = 11;

  obstacleActive = true;
}

// ── Game tick ────────────────────────────────────────────

void gameTick() {
  // Scroll obstacle (or count down the gap before the next one)
  if (obstacleActive) {
    obstacleCol--;
    if (obstacleRightCol() < 0) {   // fully scrolled off the left edge
      if (dinoState == DINO_ON_PLATFORM) dinoState = DINO_FALLING;
      score++;
      if (gameSpeed > 60) gameSpeed -= 8;
      obstacleActive = false;
      gapTicks = random(GAP_MIN_TICKS, GAP_MAX_TICKS + 1);
    }
  } else {
    if (--gapTicks <= 0) spawnObstacle();
  }

  // Update dino vertical position
  switch (dinoState) {
    case DINO_JUMPING:
      jumpPhase++;
      if (jumpPhase >= JUMP_FRAMES) {
        jumpPhase  = -1;
        dinoState  = DINO_FALLING;   // gravity finishes the descent
      } else {
        dinoTopRow = jumpBaseRow - JUMP_ARC[jumpPhase];
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
  dinoTopRow  = STAND_TOP;
  jumpPhase   = -1;
  jumpBaseRow = STAND_TOP;
  runFrame    = 0;
  dinoState   = DINO_GROUNDED;
  score       = 0;
  gameOver    = false;
  gameSpeed   = 150;
  gapTicks    = 0;
  spawnObstacle();
  lastTick    = millis();
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

  // Jump — from ground or a platform; level detection keeps it responsive
  // (holding the button re-jumps the instant the dino lands).
  if (digitalRead(BUTTON_PIN) == LOW &&
      (dinoState == DINO_GROUNDED || dinoState == DINO_ON_PLATFORM)) {
    jumpBaseRow = dinoTopRow;
    jumpPhase   = 0;
    dinoTopRow  = jumpBaseRow - JUMP_ARC[0];
    dinoState   = DINO_JUMPING;
  }

  unsigned long now = millis();
  if (now - lastTick >= gameSpeed) {
    lastTick = now;
    gameTick();
  }

  renderFrame();
  delay(10);
}
