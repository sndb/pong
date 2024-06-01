#include "raylib.h"
#include "raymath.h"

#include <stdlib.h>

/* Macros */
#define LEN(x) (sizeof(x) / sizeof((x)[0]))

#define COLOR(x)                       \
	(Color) {                      \
		.r = (x >> 16) & 0xff, \
		.g = (x >> 8) & 0xff,  \
		.b = x & 0xff,         \
		.a = 0xff,             \
	}

/* Screen */
const int screenWidth = 720;
const int screenHeight = 720;
const Vector2 screenCenter = {screenWidth / 2.0, screenHeight / 2.0};

/* Graphics */
/* Pallete: https://lospec.com/palette-list/resurrect-64 */
const Color colorRacket = COLOR(0xffffff),
            colorRacketHit = COLOR(0xfbff86),
            colorBall = COLOR(0xfbb954),
            colorTrail = COLOR(0x9babb2),
            colorParticleTrail = COLOR(0x9e4539),
            colorParticleBurst = COLOR(0xfbb954),
            colorUIText = colorRacket,
            colorUIFlash = colorRacketHit,
            colorBackgroundA = COLOR(0x2e222f),
            colorBackgroundB = COLOR(0x3e3546);

const float fontSize = 32.0;
Font font;

const int backgroundTileSize = 2;
Texture2D textureBackground;

/* Sounds */
Sound soundHit;
Sound soundLoss;

/* Racket */
const Vector2 racketSize = {120.0, 10.0};
const Vector2 racketOffset = {0.0, racketSize.y * 5.0};
const float racketVelocity = 400.0;
const float racketBoostFactor = 2.0;

float playerPosition;
float opponentPosition;

bool playerAI = false;
bool opponentAI = false;

float recentMoves[10] = {0};

/* Ball */
typedef enum Hit {
	HIT_NONE,
	HIT_PLAYER,
	HIT_OPPONENT,
} Hit;

const float ballRadius = 10.0;
const float ballAcceleration = 25.0;

Vector2 ballPosition;
float ballVelocity;
float ballRotation;
float ballSpin;
int ballHits;
double lastBallHit;

/* Trail */
typedef struct Trail {
	Vector2 position;
	double createdAt;
} Trail;

const float trailContrast = 0.1;
const double trailFrequency = 500.0;
const double trailDuration = 0.1;

Trail trails[128] = {0};

/* Particles */
typedef struct Particle {
	Vector2 position;
	Vector2 velocity;
	float acceleration;
	float size;
	float rotation;
	float spin;
	Color color;
	int type;
	double createdAt;
	double duration;
} Particle;

Particle particles[512] = {0};

/* State */
typedef enum State {
	GAME_GOING,
	GAME_LOST,
} State;

State gameState;

/* UI */
const char *messageText;

/* Functions */
Rectangle GrowRectangle(Rectangle rec, float delta) {
	rec.x -= delta;
	rec.y -= delta;
	rec.width += 2 * delta;
	rec.height += 2 * delta;
	return rec;
}

Rectangle PlayerRectangle(void) {
	Rectangle rec = {
	    .x = playerPosition,
	    .y = screenHeight - racketSize.y - racketOffset.y,
	    .width = racketSize.x,
	    .height = racketSize.y,
	};
	return rec;
}

Rectangle OpponentRectangle(void) {
	Rectangle rec = {
	    .x = opponentPosition,
	    .y = racketSize.y + racketOffset.y,
	    .width = racketSize.x,
	    .height = racketSize.y,
	};
	return rec;
}

Vector2 BallDirection(void) {
	Vector2 dir = {
	    .x = ballRotation >= 90.0 && ballRotation < 270.0 ? -1.0 : +1.0,
	    .y = ballRotation >= 00.0 && ballRotation < 180.0 ? +1.0 : -1.0,
	};
	return dir;
}

Hit BallHit(void) {
	float dy = BallDirection().y;
	if (CheckCollisionCircleRec(ballPosition, ballRadius, PlayerRectangle()) && dy > 0)
		return HIT_PLAYER;
	if (CheckCollisionCircleRec(ballPosition, ballRadius, OpponentRectangle()) && dy < 0)
		return HIT_OPPONENT;
	return HIT_NONE;
}

bool IsBallHitWall(void) {
	Vector2 dir = BallDirection();
	return (ballPosition.x - ballRadius <= racketOffset.x && dir.x < 0)
	    || (ballPosition.x + ballRadius >= screenWidth - racketOffset.x && dir.x > 0);
}

bool IsBallHitRecently(void) {
	return lastBallHit != 0 && GetTime() - lastBallHit < 0.5;
}

bool ParticleAlive(Particle part) {
	return part.createdAt > 0 && part.createdAt + part.duration > GetTime();
}

void ResetGame(void) {
	playerPosition = screenWidth / 2.0 - racketSize.x / 2.0;
	opponentPosition = playerPosition;
	ballVelocity = 400.0;
	ballPosition = screenCenter;
	ballRotation = 70.0;
	ballSpin = 0.0;
	ballHits = 0;
	lastBallHit = 0.0;
	gameState = GAME_GOING;
}

enum {
	WRITE_CENTER = 1,
	WRITE_CENTER_X = 2,
	WRITE_CENTER_Y = 4,
};

void Write(const char *text, Vector2 position, Color color, unsigned long flags) {
	float spacing = 0.0;
	Vector2 size = MeasureTextEx(font, text, fontSize, spacing);
	Vector2 offset = {0.0, 0.0};
	if (flags & (WRITE_CENTER | WRITE_CENTER_X))
		offset.x -= size.x / 2.0;
	if (flags & (WRITE_CENTER | WRITE_CENTER_Y))
		offset.y -= size.y / 2.0;
	DrawTextEx(font, text, Vector2Add(position, offset), fontSize, spacing, color);
}

void DrawBackground() {
	DrawTexture(textureBackground, 0, 0, WHITE);
}

void DrawUI(void) {
	const char *text = TextFormat("%d", ballHits);
	Color color = IsBallHitRecently() ? colorUIFlash : colorUIText;
	Vector2 position = {4, 0};
	Write(text, position, color, 0);

	if (gameState == GAME_LOST)
		Write(messageText, screenCenter, colorUIText, WRITE_CENTER);
}

void DrawParticles(void) {
	for (size_t i = 0; i < LEN(particles); i++) {
		Particle p = particles[i];
		if (!ParticleAlive(p))
			continue;
		DrawPoly(p.position, p.type, p.size / 2, p.rotation, p.color);
	}
}

void UpdateParticles(void) {
	float delta = GetFrameTime();
	for (size_t i = 0; i < LEN(particles); i++) {
		Particle p = particles[i];
		if (!ParticleAlive(p))
			continue;
		p.position = Vector2Add(p.position, Vector2Scale(p.velocity, delta));
		p.velocity = Vector2Add(p.velocity, Vector2Scale(p.velocity, p.acceleration * delta));
		p.rotation += p.spin * delta;
		particles[i] = p;
	}
}

void EmitParticle(Particle part) {
	static size_t lastParticle = 0;

	part.createdAt = GetTime();
	for (size_t i = 0; i < LEN(particles); i++) {
		size_t j = (lastParticle + i) % LEN(particles);
		if (!ParticleAlive(particles[j])) {
			particles[j] = part;
			lastParticle = j;
			return;
		}
	}
}

void EmitBurst(void) {
	Vector2 dir = BallDirection();
	for (int i = 0; i < 10; i++) {
		Particle part = {
		    .position = ballPosition,
		    .velocity.x = dir.x * GetRandomValue(0, 100),
		    .velocity.y = dir.y * GetRandomValue(0, 100),
		    .acceleration = -GetRandomValue(1, 16) / 4.0,
		    .size = ballRadius * (0.4 + GetRandomValue(1, 4) / 10.0),
		    .duration = GetRandomValue(1, 6) / 2.0,
		    .rotation = GetRandomValue(0, 359),
		    .spin = GetRandomValue(-180, 180),
		    .type = GetRandomValue(3, 5),
		    .color = ColorBrightness(colorParticleBurst, -0.75 + GetRandomValue(0, 3) / 4.0),
		};
		EmitParticle(part);
	}
}

void EmitTrailParticle(void) {
	Particle part = {
	    .position.x = ballPosition.x + (GetRandomValue(0, 100) / 50.0 - 1) * ballRadius,
	    .position.y = ballPosition.y + (GetRandomValue(0, 100) / 50.0 - 1) * ballRadius,
	    .velocity.x = GetRandomValue(-100, 100),
	    .velocity.y = GetRandomValue(-100, 100),
	    .acceleration = -8,
	    .size = 0.5 * ballRadius + GetRandomValue(1, 100) / 100.0 * 0.5 * ballRadius,
	    .duration = 0.5,
	    .rotation = GetRandomValue(0, 359),
	    .spin = GetRandomValue(-180, 180),
	    .type = GetRandomValue(3, 5),
	    .color = colorParticleTrail,
	};
	EmitParticle(part);
}

void DrawBallGlow(void) {
	DrawCircleGradient(ballPosition.x, ballPosition.y, 3 * ballRadius, Fade(WHITE, 0.4), BLANK);
}

void DrawTrail(void) {
	double now = GetTime();
	for (size_t i = 0; i < LEN(trails); i++) {
		Trail t = trails[i];
		if (t.createdAt + trailDuration > now) {
			double left = trailDuration - (now - t.createdAt);
			float alpha = left / trailDuration * trailContrast;
			Color color = Fade(colorTrail, alpha);
			DrawCircleV(t.position, ballRadius, color);
		}
	}
}

void UpdateTrail(void) {
	static size_t i = 0;

	double now = GetTime();
	if (trails[i].createdAt + 1.0 / trailFrequency < now) {
		trails[i].position = ballPosition;
		trails[i].createdAt = now;
		i = (i + 1) % LEN(trails);
	}
}

void UpdateRecentMoves(float delta) {
	static size_t i = 0;
	static double lastTime = 0;

	double recentThreshold = 0.1;
	double now = GetTime();
	int n = LEN(recentMoves);
	if (lastTime + recentThreshold / n < now) {
		i = (i + 1) % n;
		lastTime = now;
		recentMoves[i] = 0;
	}
	recentMoves[i] += delta;
}

float RecentMovesDelta(void) {
	float result = 0;
	for (size_t i = 0; i < LEN(recentMoves); i++)
		result += recentMoves[i];
	return result;
}

void DrawBall(void) {
	DrawCircle(ballPosition.x, ballPosition.y, ballRadius, colorBall);
}

void UpdateBall(void) {
	double now = GetTime();
	float delta = GetFrameTime();

	float v = ballVelocity * delta;
	ballPosition.x += v * cosf(DEG2RAD * ballRotation);
	ballPosition.y += v * sinf(DEG2RAD * ballRotation);
	ballRotation = Wrap(ballRotation + ballSpin * delta, 0.0, 360.0);

	Hit hit = BallHit();
	if (hit) {
		ballRotation = Wrap(360.0 - ballRotation, 0.0, 360.0);
		ballVelocity += ballAcceleration;
		if (hit == HIT_PLAYER)
			ballSpin = Clamp(RecentMovesDelta(), -30.0, +30.0);
		PlaySound(soundHit);
		EmitBurst();

		lastBallHit = now;
		ballHits++;
	}

	if (IsBallHitWall()) {
		ballRotation = Wrap(180.0 - ballRotation, 0.0, 360.0);
		ballSpin *= -1;
		PlaySound(soundHit);
		EmitBurst();
	}

	static double lastTrailEmit = 0;
	if (now - lastTrailEmit > 0.2) {
		EmitTrailParticle();
		lastTrailEmit = now;
	}
}

void DrawRacket(void) {
	Rectangle playerRec = PlayerRectangle();
	Rectangle opponentRec = OpponentRectangle();

	bool playerHit = IsBallHitRecently() && ballHits % 2 == 1;
	bool opponentHit = IsBallHitRecently() && ballHits % 2 == 0;

	Color playerColor = playerHit ? colorRacketHit : colorRacket;
	Color opponentColor = opponentHit ? colorRacketHit : colorRacket;
	Color outlineColor = Fade(colorRacket, 0.2);

	float outline = 2.0;
	float roundness = 0.5;
	int segments = 16;

	DrawRectangleRounded(GrowRectangle(playerRec, outline), roundness, segments, outlineColor);
	DrawRectangleRounded(GrowRectangle(opponentRec, outline), roundness, segments, outlineColor);
	DrawRectangleRounded(playerRec, roundness, segments, playerColor);
	DrawRectangleRounded(opponentRec, roundness, segments, opponentColor);
}

float PlayerVelocity(void) {
	float boost = IsKeyDown(KEY_UP) ? racketBoostFactor : 1;
	float dir = IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT);
	return racketVelocity * dir * boost;
}

float OpponentVelocity(void) {
	float boost = IsKeyDown(KEY_W) ? racketBoostFactor : 1;
	float dir = IsKeyDown(KEY_D) - IsKeyDown(KEY_A);
	return racketVelocity * dir * boost;
}

void UpdateRacket(void) {
	float oldPosition = playerPosition;

	float delta = GetFrameTime();
	playerPosition += GetMouseDelta().x;
	playerPosition += PlayerVelocity() * delta;
	opponentPosition += OpponentVelocity() * delta;

	float aiPosition = ballPosition.x - racketSize.x / 2;
	if (playerAI)
		playerPosition = aiPosition;
	if (opponentAI)
		opponentPosition = aiPosition;

	float minPos = racketOffset.x;
	float maxPos = screenWidth - racketOffset.x - racketSize.x;
	playerPosition = Clamp(playerPosition, minPos, maxPos);
	opponentPosition = Clamp(opponentPosition, minPos, maxPos);

	UpdateRecentMoves(playerPosition - oldPosition);
}

void LoseGame(const char *message) {
	messageText = message;
	gameState = GAME_LOST;
	PlaySound(soundLoss);
}

void UpdateState(void) {
	if (ballPosition.y + ballRadius > screenHeight)
		LoseGame("You lost.");
	if (ballPosition.y - ballRadius < 0)
		LoseGame("You won!");
}

Texture2D GenerateBackground(void) {
	Color *pixels = malloc(sizeof(Color[screenWidth * screenHeight]));
	for (int y = 0; y < screenHeight; y++) {
		for (int x = 0; x < screenWidth; x++) {
			int i = x / backgroundTileSize + y / backgroundTileSize;
			Color color = i % 2 ? colorBackgroundA : colorBackgroundB;
			pixels[y * screenWidth + x] = color;
		}
	}
	Image img = {
	    .data = pixels,
	    .width = screenWidth,
	    .height = screenHeight,
	    .mipmaps = 1,
	    .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
	};
	Texture2D tex = LoadTextureFromImage(img);
	UnloadImage(img);
	return tex;
}

void InitResources(void) {
	font = LoadFont("font.ttf");
	soundHit = LoadSound("hit.wav");
	soundLoss = LoadSound("lost.wav");
	textureBackground = GenerateBackground();
}

void FreeResources(void) {
	UnloadFont(font);
	UnloadSound(soundHit);
	UnloadSound(soundLoss);
	UnloadTexture(textureBackground);
}

void Update(void) {
	if (gameState == GAME_GOING) {
		UpdateRacket();
		UpdateTrail();
		UpdateParticles();
		UpdateBall();
		UpdateState();
	}
	if (IsKeyPressed(KEY_Q)) opponentAI ^= 1;
	if (IsKeyPressed(KEY_E)) playerAI ^= 1;
	if (IsKeyPressed(KEY_R)) ResetGame();
}

void Draw(void) {
	BeginDrawing();
	DrawBackground();
	DrawUI();
	DrawRacket();
	DrawBallGlow();
	DrawTrail();
	DrawParticles();
	DrawBall();
	EndDrawing();
}

int main(void) {
	SetConfigFlags(FLAG_MSAA_4X_HINT);
	InitWindow(screenWidth, screenHeight, "Ping Pong");
	InitAudioDevice();
	SetTextLineSpacing(fontSize);
	DisableCursor();
	InitResources();
	ResetGame();
	while (!WindowShouldClose()) {
		Update();
		Draw();
	}
	FreeResources();
	CloseAudioDevice();
	CloseWindow();
	return 0;
}
