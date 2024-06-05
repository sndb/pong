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
const Vector2 center = {screenWidth / 2.0, screenHeight / 2.0};

/* Graphics */
/* Pallete: https://lospec.com/palette-list/resurrect-64 */
typedef struct Colorsheme {
	Color racket;
	Color racketHit;
	Color ball;
	Color trail;
	Color particleTrail;
	Color particleBurst;
	Color uiText;
	Color uiFlash;
	Color backgroundA;
	Color backgroundB;
} Colorscheme;

const Colorscheme colors = {
	.racket = COLOR(0xffffff),
	.racketHit = COLOR(0x8fd3ff),
	.ball = COLOR(0xfbb954),
	.trail = COLOR(0x9babb2),
	.particleTrail = COLOR(0x9e4539),
	.particleBurst = COLOR(0xfbb954),
	.uiText = COLOR(0xffffff),
	.uiFlash = COLOR(0x8fd3ff),
	.backgroundA = COLOR(0x2e222f),
	.backgroundB = COLOR(0x3e3546),
};

const float fontSize = 32.0;
Font font;

const int backgroundTileSize = 2;
const int separatorHalfWidth = 1;
const int separatorPitch = 48;
Texture2D textureBackground;

/* Sounds */
typedef struct Sounds {
	Sound hit;
	Sound loss;
} Sounds;

Sounds sounds;

/* Racket */
typedef struct Player {
	float moves[10];
	float position;
	bool ai;
} Player;

typedef struct Opponent {
	float position;
	bool ai;
} Opponent;

const Vector2 racketSize = {120.0, 10.0};
const Vector2 racketOffset = {0.0, racketSize.y * 5.0};
const float racketVelocity = 400.0;
const float racketBoostFactor = 2.0;

/* Ball */
typedef enum Hit {
	HIT_NONE,
	HIT_PLAYER,
	HIT_OPPONENT,
} Hit;

typedef struct Ball {
	Vector2 position;
	float velocity;
	float rotation;
	float spin;
	int hitCount;
	double lastHitTime;
	Hit lastHit;
} Ball;

const float ballRadius = 10.0;
const float ballAcceleration = 25.0;

/* Trail */
typedef struct Trail {
	Vector2 position;
	double createdAt;
} Trail;

const float trailContrast = 0.1;
const double trailFrequency = 500.0;
const double trailDuration = 0.1;

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

/* State */
typedef enum Status {
	STATUS_GOING,
	STATUS_LOST,
} Status;

struct {
	Ball ball;
	Player player;
	Opponent opponent;
	Status status;
	Particle particles[512];
	Trail trails[128];
	const char *message;
} state;

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
		.x = state.player.position,
		.y = screenHeight - racketSize.y - racketOffset.y,
		.width = racketSize.x,
		.height = racketSize.y,
	};
	return rec;
}

Rectangle OpponentRectangle(void) {
	Rectangle rec = {
		.x = state.opponent.position,
		.y = racketSize.y + racketOffset.y,
		.width = racketSize.x,
		.height = racketSize.y,
	};
	return rec;
}

Vector2 BallDirection(void) {
	float r = state.ball.rotation;
	Vector2 dir = {
		.x = r >= 90.0 && r < 270.0 ? -1.0 : +1.0,
		.y = r >= 00.0 && r < 180.0 ? +1.0 : -1.0,
	};
	return dir;
}

Hit BallHit(void) {
	Vector2 pos = state.ball.position;
	float dy = BallDirection().y;
	if (CheckCollisionCircleRec(pos, ballRadius, PlayerRectangle()) && dy > 0)
		return HIT_PLAYER;
	if (CheckCollisionCircleRec(pos, ballRadius, OpponentRectangle()) && dy < 0)
		return HIT_OPPONENT;
	return HIT_NONE;
}

bool IsBallHitWall(void) {
	float x = state.ball.position.x;
	Vector2 dir = BallDirection();
	return (x - ballRadius <= racketOffset.x && dir.x < 0)
	    || (x + ballRadius >= screenWidth - racketOffset.x && dir.x > 0);
}

bool IsBallHitRecently(void) {
	double t = state.ball.lastHitTime;
	return t != 0 && GetTime() - t < 0.5;
}

bool ParticleAlive(Particle part) {
	return part.createdAt > 0 && part.createdAt + part.duration > GetTime();
}

void ResetGame(void) {
	state.player.position = screenWidth / 2.0 - racketSize.x / 2.0;
	state.opponent.position = state.player.position;
	state.ball.position = center;
	state.ball.velocity = 400.0;
	state.ball.rotation = 70.0;
	state.ball.spin = 0.0;
	state.ball.hitCount = 0;
	state.ball.lastHitTime = 0.0;
	state.status = STATUS_GOING;
}

enum {
	WRITE_CENTER_X = 1,
	WRITE_CENTER_Y = 2,
	WRITE_CENTER = WRITE_CENTER_X | WRITE_CENTER_Y,
};

void Write(const char *text, Vector2 position, Color color, unsigned long flags) {
	float spacing = 0.0;
	Vector2 size = MeasureTextEx(font, text, fontSize, spacing);
	Vector2 offset = {0.0, 0.0};
	if (flags & WRITE_CENTER_X)
		offset.x -= size.x / 2.0;
	if (flags & WRITE_CENTER_Y)
		offset.y -= size.y / 2.0;
	DrawTextEx(font, text, Vector2Add(position, offset), fontSize, spacing, color);
}

void DrawBackground() {
	DrawTexture(textureBackground, 0, 0, WHITE);
}

void DrawUI(void) {
	const char *text = TextFormat("%d", state.ball.hitCount);
	Color color = IsBallHitRecently() ? colors.uiFlash : colors.uiText;
	Vector2 position = {4, 0};
	Write(text, position, color, 0);

	if (state.status == STATUS_LOST)
		Write(state.message, center, colors.uiText, WRITE_CENTER);
}

void DrawParticles(void) {
	for (size_t i = 0; i < LEN(state.particles); i++) {
		Particle p = state.particles[i];
		if (!ParticleAlive(p))
			continue;
		DrawPoly(p.position, p.type, p.size / 2, p.rotation, p.color);
	}
}

void UpdateParticles(void) {
	float delta = GetFrameTime();
	for (size_t i = 0; i < LEN(state.particles); i++) {
		Particle p = state.particles[i];
		if (!ParticleAlive(p))
			continue;
		p.position = Vector2Add(p.position, Vector2Scale(p.velocity, delta));
		p.velocity = Vector2Add(p.velocity, Vector2Scale(p.velocity, p.acceleration * delta));
		p.rotation += p.spin * delta;
		state.particles[i] = p;
	}
}

void EmitParticle(Particle part) {
	static size_t lastParticle = 0;

	size_t n = LEN(state.particles);
	for (size_t i = 0; i < n; i++) {
		size_t j = (lastParticle + i) % n;
		if (!ParticleAlive(state.particles[j])) {
			part.createdAt = GetTime();
			state.particles[j] = part;
			lastParticle = j;
			return;
		}
	}
}

void EmitHitParticles(void) {
	Vector2 dir = BallDirection();
	for (int i = 0; i < 10; i++) {
		Particle part = {
			.position = state.ball.position,
			.velocity.x = dir.x * GetRandomValue(0, 100),
			.velocity.y = dir.y * GetRandomValue(0, 100),
			.acceleration = -GetRandomValue(1, 16) / 4.0,
			.size = ballRadius * (0.4 + GetRandomValue(1, 4) / 10.0),
			.duration = GetRandomValue(1, 6) / 2.0,
			.rotation = GetRandomValue(0, 359),
			.spin = GetRandomValue(-180, 180),
			.type = GetRandomValue(3, 5),
			.color = ColorBrightness(colors.particleBurst, -0.75 + GetRandomValue(0, 3) / 4.0),
		};
		EmitParticle(part);
	}
}

void EmitTrailParticle(void) {
	Particle part = {
		.position.x = state.ball.position.x + (GetRandomValue(0, 100) / 50.0 - 1) * ballRadius,
		.position.y = state.ball.position.y + (GetRandomValue(0, 100) / 50.0 - 1) * ballRadius,
		.velocity.x = GetRandomValue(-100, 100),
		.velocity.y = GetRandomValue(-100, 100),
		.acceleration = -8,
		.size = 0.5 * ballRadius + GetRandomValue(1, 100) / 100.0 * 0.5 * ballRadius,
		.duration = 0.5,
		.rotation = GetRandomValue(0, 359),
		.spin = GetRandomValue(-180, 180),
		.type = GetRandomValue(3, 5),
		.color = colors.particleTrail,
	};
	EmitParticle(part);
}

void DrawBallGlow(void) {
	Vector2 pos = state.ball.position;
	DrawCircleGradient(pos.x, pos.y, 3 * ballRadius, Fade(WHITE, 0.4), BLANK);
}

void DrawTrail(void) {
	double now = GetTime();
	for (size_t i = 0; i < LEN(state.trails); i++) {
		Trail t = state.trails[i];
		if (t.createdAt + trailDuration > now) {
			double left = trailDuration - (now - t.createdAt);
			float alpha = left / trailDuration * trailContrast;
			Color color = Fade(colors.trail, alpha);
			DrawCircleV(t.position, ballRadius, color);
		}
	}
}

void UpdateTrail(void) {
	static size_t i = 0;

	double now = GetTime();
	Trail *t = &state.trails[i];
	if (t->createdAt + 1.0 / trailFrequency < now) {
		t->position = state.ball.position;
		t->createdAt = now;
		i = (i + 1) % LEN(state.trails);
	}
}

void UpdateRecentMoves(float delta) {
	static size_t i = 0;
	static double lastTime = 0;

	double recentThreshold = 0.1;
	double now = GetTime();
	int n = LEN(state.player.moves);
	if (lastTime + recentThreshold / n < now) {
		i = (i + 1) % n;
		lastTime = now;
		state.player.moves[i] = 0;
	}
	state.player.moves[i] += delta;
}

float RecentMovesDelta(void) {
	float result = 0;
	for (size_t i = 0; i < LEN(state.player.moves); i++)
		result += state.player.moves[i];
	return result;
}

void DrawBall(void) {
	Vector2 pos = state.ball.position;
	DrawCircle(pos.x, pos.y, ballRadius + 2.0, Fade(colors.ball, 0.2));
	DrawCircle(pos.x, pos.y, ballRadius, colors.ball);
}

void UpdateBall(void) {
	double now = GetTime();
	float delta = GetFrameTime();

	float v = state.ball.velocity;
	float r = state.ball.rotation;
	state.ball.position.x += v * cosf(DEG2RAD * r) * delta;
	state.ball.position.y += v * sinf(DEG2RAD * r) * delta;
	state.ball.rotation = Wrap(r + state.ball.spin * delta, 0.0, 360.0);

	Hit hit = BallHit();
	if (hit) {
		state.ball.lastHit = hit;
		state.ball.rotation = Wrap(360.0 - state.ball.rotation, 0.0, 360.0);
		state.ball.velocity += ballAcceleration;
		if (hit == HIT_PLAYER)
			state.ball.spin = Clamp(RecentMovesDelta(), -30.0, +30.0);
		PlaySound(sounds.hit);
		EmitHitParticles();

		state.ball.lastHitTime = now;
		state.ball.hitCount++;
	}

	if (IsBallHitWall()) {
		state.ball.rotation = Wrap(180.0 - state.ball.rotation, 0.0, 360.0);
		state.ball.spin *= -1;
		PlaySound(sounds.hit);
		EmitHitParticles();
	}

	static double lastTrailEmit = 0;
	if (now - lastTrailEmit > 0.2) {
		EmitTrailParticle();
		lastTrailEmit = now;
	}
}

void DrawRacket(Rectangle rec, bool hit) {
	float roundness = 0.5;
	int segments = 16;
	DrawRectangleRounded(GrowRectangle(rec, 2.0), roundness, segments, Fade(colors.racket, 0.2));
	DrawRectangleRounded(rec, roundness, segments, hit ? colors.racketHit : colors.racket);
}

void DrawRackets(void) {
	bool hit = IsBallHitRecently();
	Hit side = state.ball.lastHit;
	DrawRacket(PlayerRectangle(), hit && side == HIT_PLAYER);
	DrawRacket(OpponentRectangle(), hit && side == HIT_OPPONENT);
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
	float oldPosition = state.player.position;

	float delta = GetFrameTime();
	state.player.position += GetMouseDelta().x;
	state.player.position += PlayerVelocity() * delta;
	state.opponent.position += OpponentVelocity() * delta;

	float aiPosition = state.ball.position.x - racketSize.x / 2;
	if (state.player.ai)
		state.player.position = aiPosition;
	if (state.opponent.ai)
		state.opponent.position = aiPosition;

	float minPos = racketOffset.x;
	float maxPos = screenWidth - racketOffset.x - racketSize.x;
	state.player.position = Clamp(state.player.position, minPos, maxPos);
	state.opponent.position = Clamp(state.opponent.position, minPos, maxPos);

	UpdateRecentMoves(state.player.position - oldPosition);
}

void LoseGame(const char *message) {
	state.message = message;
	state.status = STATUS_LOST;
	PlaySound(sounds.loss);
}

void UpdateState(void) {
	float y = state.ball.position.y;
	if (y + ballRadius > screenHeight)
		LoseGame("You lost.");
	if (y - ballRadius < 0)
		LoseGame("You won!");
}

Texture2D GenerateBackground(void) {
	Color *pixels = malloc(sizeof(Color[screenWidth * screenHeight]));
	for (int y = 0; y < screenHeight; y++) {
		for (int x = 0; x < screenWidth; x++) {
			int i = x / backgroundTileSize + y / backgroundTileSize;
			Color color = i % 2 ? colors.backgroundA : colors.backgroundB;
			pixels[y * screenWidth + x] = color;
		}
	}
	for (int w = -separatorHalfWidth; w <= separatorHalfWidth; w++) {
		for (int x = 0; x < screenWidth; x++) {
			if (x / separatorPitch % 2) {
				int i = (w + center.y) * screenWidth + x;
				pixels[i] = ColorBrightness(pixels[i], 0.2);
			}
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
	sounds.hit = LoadSound("hit.wav");
	sounds.loss = LoadSound("lost.wav");
	textureBackground = GenerateBackground();
}

void FreeResources(void) {
	UnloadFont(font);
	UnloadSound(sounds.hit);
	UnloadSound(sounds.loss);
	UnloadTexture(textureBackground);
}

void Update(void) {
	if (state.status == STATUS_GOING) {
		UpdateRacket();
		UpdateTrail();
		UpdateParticles();
		UpdateBall();
		UpdateState();
	}
	if (IsKeyPressed(KEY_Q)) state.opponent.ai ^= 1;
	if (IsKeyPressed(KEY_E)) state.player.ai ^= 1;
	if (IsKeyPressed(KEY_R)) ResetGame();
	if (IsKeyPressed(KEY_PRINT_SCREEN)) TakeScreenshot("pong.png");
}

void Draw(void) {
	BeginDrawing();
	DrawBackground();
	DrawUI();
	DrawRackets();
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
