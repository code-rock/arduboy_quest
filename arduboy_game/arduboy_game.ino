#include <Arduboy2.h>
#include <math.h>

Arduboy2 arduboy;

// ====== Карта ======
#define MAP_WIDTH 8
#define MAP_HEIGHT 8
const char worldMap[MAP_HEIGHT][MAP_WIDTH + 1] PROGMEM = {
  "11111111",
  "10000001",
  "10110001",
  "10000001",
  "10111101",
  "10000001",
  "10000001",
  "11111111"
};

// ====== Игрок ======
float playerX = 3.5; // внутри пустой клетки
float playerY = 3.5;
float playerAngle = 0;
const float moveSpeed = 0.1;
const float rotSpeed = 0.03;

// ====== Рисуем одну вертикаль ======
void drawWallSlice(int x)
{
  float fov = 0.6; // примерно 35 градусов
  float rayAngle = playerAngle - fov / 2.0 + ((float)x / 128.0) * fov;

  float rayX = playerX;
  float rayY = playerY;
  float rayDirX = cos(rayAngle);
  float rayDirY = sin(rayAngle);

  float distance = 0.0;
  bool hit = false;

  while (!hit && distance < 16.0)
  {
    distance += 0.05;
    int mapX = (int)(rayX + rayDirX * distance);
    int mapY = (int)(rayY + rayDirY * distance);

    if (mapX < 0 || mapX >= MAP_WIDTH || mapY < 0 || mapY >= MAP_HEIGHT)
      break;

    char tile = (char)pgm_read_byte(&(worldMap[mapY][mapX]));
    if (tile == '1')
      hit = true;
  }

  if (hit)
  {
    int lineHeight = (int)(64.0 / distance); // 64 = высота экрана
    int drawStart = 32 - lineHeight / 2;
    int drawEnd = 32 + lineHeight / 2;

    if (drawStart < 0) drawStart = 0;
    if (drawEnd >= 64) drawEnd = 63;

    for (int y = drawStart; y <= drawEnd; y++)
      arduboy.drawPixel(x, y, WHITE);
  }
}

void setup()
{
  arduboy.begin();
  arduboy.clear();
  arduboy.setFrameRate(60);
}

void loop()
{
  if (!arduboy.nextFrame())
    return;

  arduboy.pollButtons();
  arduboy.clear();

  // ====== Управление ======
  if (arduboy.pressed(UP_BUTTON))
  {
    float newX = playerX + cos(playerAngle) * moveSpeed;
    float newY = playerY + sin(playerAngle) * moveSpeed;
    char tile = (char)pgm_read_byte(&(worldMap[(int)newY][(int)newX]));
    if (tile != '1')
    {
      playerX = newX;
      playerY = newY;
    }
  }
  if (arduboy.pressed(DOWN_BUTTON))
  {
    float newX = playerX - cos(playerAngle) * moveSpeed;
    float newY = playerY - sin(playerAngle) * moveSpeed;
    char tile = (char)pgm_read_byte(&(worldMap[(int)newY][(int)newX]));
    if (tile != '1')
    {
      playerX = newX;
      playerY = newY;
    }
  }
  if (arduboy.pressed(LEFT_BUTTON)) playerAngle -= rotSpeed;
  if (arduboy.pressed(RIGHT_BUTTON)) playerAngle += rotSpeed;

  // ====== Рендеринг ======
  for (int x = 0; x < 128; x++)
    drawWallSlice(x);

  arduboy.display();
}
