#include <Arduboy2.h>
#include <ArduboyTones.h>
#include <math.h>

// ------------------------------------------------------
// Базовые объекты
// ------------------------------------------------------
Arduboy2 arduboy;

bool audioEnabled() {
  return arduboy.audio.enabled();
}

ArduboyTones sound(audioEnabled);

// ------------------------------------------------------
// Экран и рендер
// ------------------------------------------------------
constexpr float SCREEN_W_FL = 128.0f;
constexpr float SCREEN_H_FL = 64.0f;
constexpr int SCREEN_W_INT  = 128;
constexpr int SCREEN_H_INT  = 64;

constexpr int   FPS_TARGET     = 60;
constexpr float FOV            = 0.6f;
constexpr float RAY_STEP       = 0.1f;
constexpr float MAX_DISTANCE   = 32.0f;
constexpr float MOVE_SPEED     = 0.15f;
constexpr float ROT_SPEED      = 0.12f;

constexpr int   SCREEN_CENTER_Y_INT = 32;
constexpr float INVERSE_SCREEN_W_FL = 1.0f / 128.0f;
constexpr float FOV_HALF            = 0.3f;

// ------------------------------------------------------
// Enum для объектов
// ------------------------------------------------------
enum TileType {
  TILE_EMPTY = 0,  // пусто
  TILE_WALL = 1,   // стена
  TILE_BOX = 2,    // кубический ящик
  TILE_GOAL = 3    // квадрат на полу
};

// ------------------------------------------------------
// Сложность игры
// ------------------------------------------------------
enum Difficulty {
  DIFF_EASY,    // легкая - карта с возможностью перемещения
  DIFF_MEDIUM,  // средняя - карта только для просмотра
  DIFF_HARD     // сложная - карта отключена
};

Difficulty gameDifficulty = DIFF_EASY;

// ------------------------------------------------------
// Карты уровней
// ------------------------------------------------------
#define MAP_W 8
#define MAP_H 8
#define NUM_LEVELS 4

// '0' - пусто, '1' - стена, '2' - ящик, '3' - цель
const char levelMaps[NUM_LEVELS][MAP_H][MAP_W + 1] PROGMEM = {
  // Уровень 0: 1 ящик, 1 цель, стена по середине
  {
    "11111111",
    "10000001",
    "10000001",
    "10011001",  // стена
    "10203001",  // ящик(2) и цель(3)
    "10000001",
    "10000001",
    "11111111"
  },
  // Уровень 1: 2 ящика, 2 цели
  {
    "11111111",
    "10000001",
    "10020001",
    "10003001",
    "10000001",
    "10020001",
    "10003001",
    "11111111"
  },
  // Уровень 2: 3 ящика, 3 цели
  {
    "11111111",
    "10020001",
    "10003001",
    "10000001",
    "10020001",
    "10003001",
    "10020001",
    "11111111"
  },
  // Уровень 3: 4 ящика, 4 цели
  {
    "11111111",
    "10020001",
    "10003001",
    "10020001",
    "10003001",
    "10020001",
    "10003001",
    "11111111"
  }
};

char currentMap[MAP_H][MAP_W + 1];

// ------------------------------------------------------
// Игрок - ИСПРАВЛЕНА НАЧАЛЬНАЯ ПОЗИЦИЯ
// ------------------------------------------------------
// Начальная позиция должна быть в свободной клетке
// Проверяем карту: строка 1, колонка 1 = '0' (пусто)
constexpr float PLAYER_START_X = 1.5f;  // Изменено с 3.5 на 1.5
constexpr float PLAYER_START_Y = 1.5f;  // Изменено с 3.5 на 1.5

float playerOnX   = PLAYER_START_X;
float playerOnY   = PLAYER_START_Y;
float playerAngle = 0.0f;

// ------------------------------------------------------
// Игровые состояния
// ------------------------------------------------------
enum GameState {
  STATE_MENU,
  STATE_PLAYING,
  STATE_MAP_VIEW,
  STATE_BOX_CONFIRM,
  STATE_LEVEL_COMPLETE,
  STATE_GAME_COMPLETE,
  STATE_EXIT
};

GameState gameState = STATE_MENU;
int       currentLevel = 0;
bool      menuMusicPlaying = false;

// Координаты последнего перемещенного ящика (для отмены)
int lastBoxX = -1;
int lastBoxY = -1;
int lastBoxPrevX = -1;
int lastBoxPrevY = -1;

// Переменные времени для управления движением
unsigned long lastMoveTime = 0;
unsigned long lastMapMoveTime = 0;

// ------------------------------------------------------
// Музыка
// ------------------------------------------------------
const uint16_t menuMusic[] PROGMEM = {
  440, 150,
  659, 150,
  523, 200,
  TONES_END
};

// ------------------------------------------------------
// Звуки
// ------------------------------------------------------
void playStepSound() {
  sound.tone(200, 30);
}

void playBoxPushSound() {
  sound.tone(400, 50);
}

void playLevelCompleteSound() {
  sound.tone(600, 100);
  delay(50);
  sound.tone(700, 100);
  delay(50);
  sound.tone(800, 150);
}

// ------------------------------------------------------
// Работа с картой
// ------------------------------------------------------
bool isInsideMap(int x, int y) {
  return (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H);
}

char getTile(int x, int y) {
  if (!isInsideMap(x, y)) return '1';
  return currentMap[y][x];
}

void setTile(int x, int y, char t) {
  if (!isInsideMap(x, y)) return;
  currentMap[y][x] = t;
}

bool isBlockingTile(char t) {
  return (t == '1' || t == '2');
}

bool checkLevelComplete() {
  for (int y = 0; y < MAP_H; ++y) {
    for (int x = 0; x < MAP_W; ++x) {
      char t = getTile(x, y);
      if (t == '3') return false; // есть пустая цель
    }
  }
  return true; // все цели заняты ящиками
}

void loadLevel(int lvl) {
  for (int y = 0; y < MAP_H; ++y) {
    for (int x = 0; x < MAP_W; ++x) {
      char c = (char)pgm_read_byte(&(levelMaps[lvl][y][x]));
      currentMap[y][x] = c;
    }
    currentMap[y][MAP_W] = '\0';
  }

  // Устанавливаем начальную позицию в зависимости от уровня
  // Для каждого уровня находим первую свободную клетку
  bool foundStart = false;
  for (int y = 1; y < MAP_H - 1 && !foundStart; ++y) {
    for (int x = 1; x < MAP_W - 1 && !foundStart; ++x) {
      if (getTile(x, y) == '0') {
        playerOnX = (float)x + 0.5f;
        playerOnY = (float)y + 0.5f;
        foundStart = true;
      }
    }
  }
  
  // Если не нашли свободную клетку, используем дефолтную
  if (!foundStart) {
    playerOnX = 1.5f;
    playerOnY = 1.5f;
  }
  
  playerAngle = 0.0f;
  lastBoxX = -1;
  lastBoxY = -1;
  
  // Инициализируем время движения при загрузке уровня
  lastMoveTime = millis();
  lastMapMoveTime = millis();
}

void goToNextLevel() {
  if (currentLevel < NUM_LEVELS - 1) {
    currentLevel++;
    loadLevel(currentLevel);
    gameState = STATE_PLAYING;
  } else {
    gameState = STATE_GAME_COMPLETE;
    playLevelCompleteSound();
  }
}

// ------------------------------------------------------
// Рендер кубического ящика
// ------------------------------------------------------
void drawBoxSlice(int x, float distance) {
  if (distance < 0.1f) distance = 0.1f;
  int lineHeight = (int)(64.0f / distance);
  int boxHeight = (lineHeight * 2) / 3;
  if (boxHeight < 8) boxHeight = 8;
  if (boxHeight > 50) boxHeight = 50;
  
  int drawStart = 32 - boxHeight / 2;
  int drawEnd = 32 + boxHeight / 2;

  if (drawStart < 0) drawStart = 0;
  if (drawEnd >= 64) drawEnd = 63;

  for (int y = drawStart; y <= drawEnd; ++y) {
    arduboy.drawPixel(x, y, WHITE);
  }
  
  for (int y = drawStart + 2; y < drawEnd - 1; y += 3) {
    if (x % 4 == 0 && y < drawEnd - 2) {
      arduboy.drawPixel(x, y, BLACK);
      arduboy.drawPixel(x, y + 1, BLACK);
    }
  }
}

// ------------------------------------------------------
// Рендер квадрата на полу
// ------------------------------------------------------
void drawGoalSlice(int x, float distance) {
  if (distance < 0.1f) distance = 0.1f;
  int thickness = 4;
  int drawStart = 58;
  int drawEnd = 62;
  
  if (drawStart < 0) drawStart = 0;
  if (drawEnd >= 64) drawEnd = 63;

  for (int y = drawStart; y <= drawEnd; ++y) {
    if ((x + y) % 2 == 0) {
      arduboy.drawPixel(x, y, WHITE);
    }
  }
}

// ------------------------------------------------------
// Рендер стены
// ------------------------------------------------------
void drawWallSlice(int x, float distance) {
  if (distance < 0.1f) distance = 0.1f;
  int lineHeight = (int)(64.0f / distance);
  int drawStart = 32 - lineHeight / 2;
  int drawEnd   = 32 + lineHeight / 2;

  if (drawStart < 0) drawStart = 0;
  if (drawEnd >= 64) drawEnd = 63;

  for (int y = drawStart; y <= drawEnd; ++y) {
    bool on = (((y / 2) + (x / 4)) % 2 == 0);
    if (on) arduboy.drawPixel(x, y, WHITE);
  }
}

// ------------------------------------------------------
// Каст луча
// ------------------------------------------------------
void castRayAndDraw(int screenX) {
  float rayAngle = playerAngle - FOV_HALF + ((float)screenX * INVERSE_SCREEN_W_FL) * FOV;
  float rayDirX = cos(rayAngle);
  float rayDirY = sin(rayAngle);

  float distance = 0.0f;
  bool hitWall = false;
  bool hitBox = false;
  bool foundGoal = false;
  char hitTile = '1';
  float boxDistance = MAX_DISTANCE;
  float goalDistance = MAX_DISTANCE;

  while ((!hitWall && !hitBox) && distance < MAX_DISTANCE) {
    distance += RAY_STEP;
    int mapX = (int)(playerOnX + rayDirX * distance);
    int mapY = (int)(playerOnY + rayDirY * distance);

    if (!isInsideMap(mapX, mapY)) {
      hitWall = true;
      break;
    }

    char t = getTile(mapX, mapY);
    
    if (t == '3' && goalDistance > MAX_DISTANCE) {
      goalDistance = distance;
      foundGoal = true;
    }
    
    if (t == '2' && !hitBox) {
      hitBox = true;
      boxDistance = distance;
      hitTile = '2';
    }
    
    if (t == '1' && !hitWall) {
      hitWall = true;
      hitTile = '1';
    }
  }

  // Рендерим пол
  for (int y = 33; y < 64; ++y) {
    int factor = (y - 32) / 4 + 1;
    if (factor < 1) factor = 1;
    if ((screenX + y) % factor == 0) {
      arduboy.drawPixel(screenX, y, WHITE);
    }
  }

  // Рендерим цель
  if (foundGoal && goalDistance < MAX_DISTANCE && (!hitWall || goalDistance < distance)) {
    drawGoalSlice(screenX, goalDistance);
  }

  // Рендерим ящик или стену
  if (hitBox || hitWall) {
    float renderDist = hitBox ? boxDistance : distance;
    if (hitBox) {
      drawBoxSlice(screenX, renderDist);
    } else {
      drawWallSlice(screenX, renderDist);
    }
  }
}

// ------------------------------------------------------
// Определение направления движения
// ------------------------------------------------------
void getDirection(int& dx, int& dy, float angle) {
  // Нормализуем угол в диапазон [0, 2*PI)
  while (angle < 0) angle += 2.0f * 3.14159265f;
  while (angle >= 2.0f * 3.14159265f) angle -= 2.0f * 3.14159265f;
  
  // Используем cos и sin для определения направления
  float cosVal = cos(angle);
  float sinVal = sin(angle);
  
  // Определяем основное направление по наибольшей компоненте
  if (fabs(cosVal) > fabs(sinVal)) {
    // Горизонтальное движение (восток или запад)
    dx = (cosVal > 0) ? 1 : -1;
    dy = 0;
  } else {
    // Вертикальное движение (север или юг)
    dx = 0;
    dy = (sinVal > 0) ? 1 : -1; // sin > 0 = юг (вниз), sin < 0 = север (вверх)
  }
}

// ------------------------------------------------------
// Проверка столкновений - ИСПРАВЛЕНА
// ------------------------------------------------------
bool canMoveTo(float newX, float newY) {
  // Проверяем текущую позицию игрока
  int currentX = (int)playerOnX;
  int currentY = (int)playerOnY;
  
  // Проверяем, что текущая позиция свободна (на случай если игрок застрял)
  if (isInsideMap(currentX, currentY)) {
    char currentTile = getTile(currentX, currentY);
    if (isBlockingTile(currentTile)) {
      // Игрок застрял в стене/ящике - не можем двигаться
      return false;
    }
  }
  
  // Проверяем целевую позицию
  int targetX = (int)newX;
  int targetY = (int)newY;
  
  if (!isInsideMap(targetX, targetY)) {
    return false;
  }
  
  char tile = getTile(targetX, targetY);
  if (isBlockingTile(tile)) {
    return false;
  }
  
  // Если игрок переходит в другую клетку, проверяем промежуточные
  if (targetX != currentX || targetY != currentY) {
    float dx = newX - playerOnX;
    float dy = newY - playerOnY;
    float steps = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy);
    if (steps > 0.1f) {
      float stepX = dx / steps;
      float stepY = dy / steps;
      for (int i = 1; i <= (int)steps; ++i) {
        int checkX = (int)(playerOnX + stepX * i);
        int checkY = (int)(playerOnY + stepY * i);
        if (isInsideMap(checkX, checkY)) {
          char checkTile = getTile(checkX, checkY);
          if (isBlockingTile(checkTile)) {
            return false;
          }
        }
      }
    }
  }
  
  return true;
}

// Проверка возможности сдвинуть ящик
bool canPushBox(int boxX, int boxY, float angle) {
  int dx, dy;
  getDirection(dx, dy, angle);
  
  int nextX = boxX + dx;
  int nextY = boxY + dy;
  
  if (!isInsideMap(nextX, nextY)) {
    return false;
  }
  
  char nextTile = getTile(nextX, nextY);
  return (nextTile == '0' || nextTile == '3');
}

// Сдвиг ящика
bool pushBox(int boxX, int boxY, float angle) {
  if (!canPushBox(boxX, boxY, angle)) {
    return false;
  }
  
  int dx, dy;
  getDirection(dx, dy, angle);
  
  int nextX = boxX + dx;
  int nextY = boxY + dy;
  
  char nextTile = getTile(nextX, nextY);
  
  // Сохраняем для возможной отмены
  lastBoxPrevX = boxX;
  lastBoxPrevY = boxY;
  lastBoxX = nextX;
  lastBoxY = nextY;
  
  // Сдвигаем ящик
  setTile(boxX, boxY, '0');
  setTile(nextX, nextY, '2');
  
  playBoxPushSound();
  
  // Проверяем, попал ли ящик на цель
  if (nextTile == '3') {
    gameState = STATE_BOX_CONFIRM;
    return true;
  }
  
  // Проверяем завершение уровня
  if (checkLevelComplete()) {
    gameState = STATE_LEVEL_COMPLETE;
    playLevelCompleteSound();
    return true;
  }
  
  return true;
}

// ------------------------------------------------------
// Управление - ИСПРАВЛЕНО
// ------------------------------------------------------
void handleMovement() {
  unsigned long currentTime = millis();
  
  // Инициализация при первом вызове
  if (lastMoveTime == 0) {
    lastMoveTime = currentTime;
  }
  
  if (currentTime - lastMoveTime < 150) return;
  
  float moveX = cos(playerAngle) * MOVE_SPEED;
  float moveY = sin(playerAngle) * MOVE_SPEED;
  
  // Вперёд (обычное движение)
  if (arduboy.pressed(UP_BUTTON) && !arduboy.pressed(A_BUTTON)) {
    float newX = playerOnX + moveX;
    float newY = playerOnY + moveY;
    
    if (canMoveTo(newX, newY)) {
      playerOnX = newX;
      playerOnY = newY;
      playStepSound();
      lastMoveTime = currentTime;
    }
  }

  // Сдвиг ящика (UP + A)
  if (arduboy.pressed(UP_BUTTON) && arduboy.pressed(A_BUTTON)) {
    float newX = playerOnX + moveX;
    float newY = playerOnY + moveY;
    
    int targetX = (int)(newX + 0.5f);
    int targetY = (int)(newY + 0.5f);
    
    if (isInsideMap(targetX, targetY)) {
      char tile = getTile(targetX, targetY);
      
      // Если перед нами ящик
      if (tile == '2') {
        // Пытаемся сдвинуть ящик
        if (pushBox(targetX, targetY, playerAngle)) {
          // Двигаем игрока только если можем
          if (canMoveTo(newX, newY)) {
            playerOnX = newX;
            playerOnY = newY;
          }
          lastMoveTime = currentTime;
          return;
        }
      } 
      // Если перед нами не ящик, проверяем обычное движение
      else if (canMoveTo(newX, newY)) {
        playerOnX = newX;
        playerOnY = newY;
        playStepSound();
        lastMoveTime = currentTime;
      }
    }
  }

  // Назад
  if (arduboy.pressed(DOWN_BUTTON)) {
    float newX = playerOnX - moveX;
    float newY = playerOnY - moveY;
    
    if (canMoveTo(newX, newY)) {
      playerOnX = newX;
      playerOnY = newY;
      playStepSound();
      lastMoveTime = currentTime;
    }
  }

  // Повороты
  if (arduboy.pressed(LEFT_BUTTON)) {
    playerAngle -= ROT_SPEED;
    lastMoveTime = currentTime;
  }
  if (arduboy.pressed(RIGHT_BUTTON)) {
    playerAngle += ROT_SPEED;
    lastMoveTime = currentTime;
  }
}

// ------------------------------------------------------
// Управление на карте 2D
// ------------------------------------------------------
void handleMapMovement() {
  unsigned long currentTime = millis();
  
  // Инициализация при первом вызове
  if (lastMapMoveTime == 0) {
    lastMapMoveTime = currentTime;
  }
  
  if (currentTime - lastMapMoveTime < 100) return;
  
  float moveSpeed = 0.3f;
  float moveX = cos(playerAngle) * moveSpeed;
  float moveY = sin(playerAngle) * moveSpeed;
  
  // Вперёд (обычное движение)
  if (arduboy.pressed(UP_BUTTON) && !arduboy.pressed(A_BUTTON)) {
    float newX = playerOnX + moveX;
    float newY = playerOnY + moveY;
    
    // Определяем направление для точной проверки
    int dx, dy;
    getDirection(dx, dy, playerAngle);
    
    // Вычисляем целевую клетку в направлении движения
    int currentCellX = (int)playerOnX;
    int currentCellY = (int)playerOnY;
    int targetCellX = currentCellX + dx;
    int targetCellY = currentCellY + dy;
    
    // Проверяем, можем ли мы войти в целевую клетку
    if (isInsideMap(targetCellX, targetCellY)) {
      char tile = getTile(targetCellX, targetCellY);
      
      // Если целевая клетка блокирующая - не двигаемся
      if (isBlockingTile(tile)) {
        // Не двигаемся
      } else {
        // Можем двигаться
        playerOnX = newX;
        playerOnY = newY;
        lastMapMoveTime = currentTime;
      }
    } else {
      // Выход за границы карты - не двигаемся
    }
  }

  // Сдвиг ящика (UP + A) на карте
  if (arduboy.pressed(UP_BUTTON) && arduboy.pressed(A_BUTTON)) {
    float newX = playerOnX + moveX;
    float newY = playerOnY + moveY;
    
    // Определяем направление для точной проверки
    int dx, dy;
    getDirection(dx, dy, playerAngle);
    
    // Вычисляем целевую клетку в направлении движения
    int currentCellX = (int)playerOnX;
    int currentCellY = (int)playerOnY;
    int targetCellX = currentCellX + dx;
    int targetCellY = currentCellY + dy;
    
    if (isInsideMap(targetCellX, targetCellY)) {
      char tile = getTile(targetCellX, targetCellY);
      
      // Если перед нами ящик
      if (tile == '2') {
        // Пытаемся сдвинуть ящик
        if (pushBox(targetCellX, targetCellY, playerAngle)) {
          // Двигаем игрока в целевую клетку
          int nextPlayerCellX = targetCellX;
          int nextPlayerCellY = targetCellY;
          
          // Проверяем, что новая позиция игрока свободна
          if (isInsideMap(nextPlayerCellX, nextPlayerCellY) && !isBlockingTile(getTile(nextPlayerCellX, nextPlayerCellY))) {
            playerOnX = (float)nextPlayerCellX + 0.5f;
            playerOnY = (float)nextPlayerCellY + 0.5f;
          } else {
            // Если не можем войти, двигаемся ближе к ящику
            playerOnX = newX;
            playerOnY = newY;
          }
          lastMapMoveTime = currentTime;
          return;
        }
      } 
      // Если перед нами не ящик, проверяем обычное движение
      else if (!isBlockingTile(tile)) {
        playerOnX = newX;
        playerOnY = newY;
        lastMapMoveTime = currentTime;
      }
    }
  }

  // Повороты
  if (arduboy.pressed(LEFT_BUTTON)) {
    playerAngle -= ROT_SPEED * 2.0f;
    lastMapMoveTime = currentTime;
  }
  if (arduboy.pressed(RIGHT_BUTTON)) {
    playerAngle += ROT_SPEED * 2.0f;
    lastMapMoveTime = currentTime;
  }
}

// ------------------------------------------------------
// Полноэкранная карта
// ------------------------------------------------------
void drawFullScreenMap() {
  arduboy.clear();
  
  arduboy.setCursor(50, 0);
  arduboy.print(F("MAP"));
  
  const int scale = 6;
  int mapW = MAP_W * scale;
  int mapH = MAP_H * scale;
  int offsetX = (128 - mapW) / 2;
  int offsetY = 8;
  
  arduboy.drawRect(offsetX - 1, offsetY - 1, mapW + 2, mapH + 2, WHITE);
  
  for (int y = 0; y < MAP_H; ++y) {
    for (int x = 0; x < MAP_W; ++x) {
      char t = getTile(x, y);
      int sx = offsetX + x * scale;
      int sy = offsetY + y * scale;
      
      if (t == '1') {
        arduboy.fillRect(sx, sy, scale, scale, WHITE);
      } else if (t == '2') {
        arduboy.fillRect(sx, sy, scale, scale, WHITE);
        arduboy.drawRect(sx + 1, sy + 1, scale - 2, scale - 2, BLACK);
      } else if (t == '3') {
        arduboy.drawLine(sx, sy, sx + scale - 1, sy + scale - 1, WHITE);
        arduboy.drawLine(sx + scale - 1, sy, sx, sy + scale - 1, WHITE);
      }
    }
  }
  
  // Игрок
  int px = offsetX + (int)(playerOnX * scale);
  int py = offsetY + (int)(playerOnY * scale);
  
  arduboy.drawPixel(px, py, WHITE);
  arduboy.drawPixel(px + 1, py, WHITE);
  arduboy.drawPixel(px - 1, py, WHITE);
  arduboy.drawPixel(px, py + 1, WHITE);
  arduboy.drawPixel(px, py - 1, WHITE);
  
  int dx = (int)(cos(playerAngle) * 3.0f);
  int dy = (int)(sin(playerAngle) * 3.0f);
  arduboy.drawLine(px, py, px + dx, py + dy, WHITE);
  
  if (gameDifficulty == DIFF_EASY) {
    arduboy.setCursor(20, 56);
    arduboy.print(F("A=CLOSE MAP"));
  } else {
    arduboy.setCursor(15, 56);
    arduboy.print(F("A=CLOSE VIEW"));
  }
}

// ------------------------------------------------------
// Экраны
// ------------------------------------------------------
void drawMenuScreen() {
  arduboy.setCursor(20, 2);
  arduboy.print(F("BOX PUZZLE"));

  arduboy.setCursor(0, 12);
  arduboy.print(F("UP/DOWN: move"));
  arduboy.setCursor(0, 20);
  arduboy.print(F("LEFT/RIGHT: turn"));
  arduboy.setCursor(0, 28);
  arduboy.print(F("UP+A: push box"));
  
  arduboy.setCursor(0, 38);
  if (gameDifficulty == DIFF_EASY) {
    arduboy.print(F("> EASY"));
  } else {
    arduboy.print(F("  EASY"));
  }
  
  arduboy.setCursor(0, 46);
  if (gameDifficulty == DIFF_MEDIUM) {
    arduboy.print(F("> MEDIUM"));
  } else {
    arduboy.print(F("  MEDIUM"));
  }
  
  arduboy.setCursor(0, 54);
  if (gameDifficulty == DIFF_HARD) {
    arduboy.print(F("> HARD"));
  } else {
    arduboy.print(F("  HARD"));
  }

  arduboy.setCursor(0, 0);
  arduboy.print(F("A=START B=EXIT"));
}

void updateMenu() {
  if (!menuMusicPlaying) {
    sound.noTone();
    sound.tones(menuMusic);
    menuMusicPlaying = true;
  }

  if (arduboy.justPressed(UP_BUTTON)) {
    if (gameDifficulty > DIFF_EASY) {
      gameDifficulty = (Difficulty)((int)gameDifficulty - 1);
    }
  }
  if (arduboy.justPressed(DOWN_BUTTON)) {
    if (gameDifficulty < DIFF_HARD) {
      gameDifficulty = (Difficulty)((int)gameDifficulty + 1);
    }
  }

  drawMenuScreen();

  if (arduboy.justPressed(A_BUTTON)) {
    sound.noTone();
    menuMusicPlaying = false;
    currentLevel = 0;
    loadLevel(currentLevel);
    gameState = STATE_PLAYING;
    // Инициализируем время движения при старте игры
    lastMoveTime = millis();
    lastMapMoveTime = millis();
  } else if (arduboy.justPressed(B_BUTTON)) {
    sound.noTone();
    menuMusicPlaying = false;
    gameState = STATE_EXIT;
  }
}

void updateGame() {
  if (arduboy.justPressed(B_BUTTON)) {
    sound.noTone();
    gameState = STATE_EXIT;
    return;
  }
  
  if (arduboy.justPressed(A_BUTTON) && !arduboy.pressed(UP_BUTTON)) {
    if (gameDifficulty == DIFF_EASY || gameDifficulty == DIFF_MEDIUM) {
      if (gameState == STATE_PLAYING) {
        gameState = STATE_MAP_VIEW;
        return;
      } else if (gameState == STATE_MAP_VIEW) {
        gameState = STATE_PLAYING;
        return;
      }
    }
  }
  
  if (gameState == STATE_MAP_VIEW) {
    drawFullScreenMap();
    
    if (gameDifficulty == DIFF_EASY) {
      handleMapMovement();
    }
  } 
  else if (gameState == STATE_PLAYING) {
    handleMovement();
    
    for (int x = 0; x < 128; ++x) {
      castRayAndDraw(x);
    }
  }
}

void updateBoxConfirm() {
  arduboy.clear();
  arduboy.setCursor(10, 10);
  arduboy.print(F("Box on goal!"));
  arduboy.setCursor(5, 20);
  arduboy.print(F("Keep it?"));
  arduboy.setCursor(0, 30);
  arduboy.print(F("B=YES DOWN=NO"));
  
  if (arduboy.justPressed(B_BUTTON)) {
    gameState = STATE_PLAYING;
    if (checkLevelComplete()) {
      gameState = STATE_LEVEL_COMPLETE;
      playLevelCompleteSound();
    }
  } else if (arduboy.justPressed(DOWN_BUTTON)) {
    // Возвращаем ящик назад
    if (lastBoxX >= 0 && lastBoxY >= 0 && lastBoxPrevX >= 0 && lastBoxPrevY >= 0) {
      // Вычисляем направление, в котором двигался ящик
      int boxDx = lastBoxX - lastBoxPrevX; // Направление движения ящика
      int boxDy = lastBoxY - lastBoxPrevY;
      
      // Возвращаем ящик на предыдущую позицию
      setTile(lastBoxX, lastBoxY, '3'); // Возвращаем цель (ящик был на цели)
      setTile(lastBoxPrevX, lastBoxPrevY, '2'); // Возвращаем ящик
      
      // Перемещаем игрока на шаг назад от ящика
      // Игрок должен быть на клетке, с которой он толкал ящик
      // Это клетка, противоположная направлению движения ящика
      int playerNewX = lastBoxPrevX - boxDx;
      int playerNewY = lastBoxPrevY - boxDy;
      
      // Проверяем, что новая позиция игрока валидна
      if (isInsideMap(playerNewX, playerNewY) && !isBlockingTile(getTile(playerNewX, playerNewY))) {
        playerOnX = (float)playerNewX + 0.5f;
        playerOnY = (float)playerNewY + 0.5f;
      } else {
        // Если не можем поставить игрока туда, ставим его рядом с ящиком
        // Пробуем альтернативные позиции
        bool placed = false;
        for (int tryX = -1; tryX <= 1 && !placed; ++tryX) {
          for (int tryY = -1; tryY <= 1 && !placed; ++tryY) {
            if (tryX == 0 && tryY == 0) continue; // Пропускаем позицию ящика
            int testX = lastBoxPrevX + tryX;
            int testY = lastBoxPrevY + tryY;
            if (isInsideMap(testX, testY) && !isBlockingTile(getTile(testX, testY))) {
              playerOnX = (float)testX + 0.5f;
              playerOnY = (float)testY + 0.5f;
              placed = true;
            }
          }
        }
      }
      
      lastBoxX = -1;
      lastBoxY = -1;
    }
    gameState = STATE_PLAYING;
  }
}

void updateLevelComplete() {
  arduboy.clear();
  if (currentLevel < NUM_LEVELS - 1) {
    arduboy.setCursor(15, 20);
    arduboy.print(F("LEVEL"));
    arduboy.setCursor(50, 20);
    arduboy.print(currentLevel + 1);
    arduboy.setCursor(20, 30);
    arduboy.print(F("COMPLETE!"));
    arduboy.setCursor(25, 45);
    arduboy.print(F("A=NEXT"));
  } else {
    arduboy.setCursor(10, 15);
    arduboy.print(F("CONGRATS!"));
    arduboy.setCursor(5, 25);
    arduboy.print(F("GAME COMPLETE"));
    arduboy.setCursor(20, 40);
    arduboy.print(F("A=MENU"));
  }
  
  if (arduboy.justPressed(A_BUTTON)) {
    if (currentLevel < NUM_LEVELS - 1) {
      goToNextLevel();
    } else {
      gameState = STATE_MENU;
      menuMusicPlaying = false;
    }
  }
}

void updateGameComplete() {
  arduboy.clear();
  arduboy.setCursor(5, 10);
  arduboy.print(F("CONGRATULATIONS!"));
  arduboy.setCursor(10, 20);
  arduboy.print(F("YOU COMPLETED"));
  arduboy.setCursor(15, 30);
  arduboy.print(F("THIS AMAZING"));
  arduboy.setCursor(20, 40);
  arduboy.print(F("GAME!"));
  arduboy.setCursor(25, 50);
  arduboy.print(F("A=MENU"));
  
  if (arduboy.justPressed(A_BUTTON)) {
    gameState = STATE_MENU;
    menuMusicPlaying = false;
  }
}

void updateExitScreen() {
  arduboy.setCursor(30, 24);
  arduboy.print(F("GOOD BYE"));
  arduboy.setCursor(6, 36);
  arduboy.print(F("RESET TO PLAY AGAIN"));
}

// ------------------------------------------------------
// setup / loop
// ------------------------------------------------------
void setup() {
  arduboy.begin();
  arduboy.clear();
  arduboy.setFrameRate(60);

  arduboy.audio.begin();
  arduboy.audio.on();

  gameState = STATE_MENU;
  menuMusicPlaying = false;
  gameDifficulty = DIFF_EASY;
  
  // Инициализируем время движения при старте
  lastMoveTime = 0;
  lastMapMoveTime = 0;
}

void loop() {
  if (!arduboy.nextFrame()) return;

  arduboy.pollButtons();
  arduboy.clear();

  switch (gameState) {
    case STATE_MENU:
      updateMenu();
      break;
    case STATE_PLAYING:
    case STATE_MAP_VIEW:
      updateGame();
      break;
    case STATE_BOX_CONFIRM:
      updateBoxConfirm();
      break;
    case STATE_LEVEL_COMPLETE:
      updateLevelComplete();
      break;
    case STATE_GAME_COMPLETE:
      updateGameComplete();
      break;
    case STATE_EXIT:
      updateExitScreen();
      break;
  }

  arduboy.display();
}
