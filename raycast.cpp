#include <stdio.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <stdint.h>
#include <malloc.h>
#include <memory.h>
#include <stdlib.h>

#include "DOSLib.h"
#include "ScanCode.h"

#include "DOSLib.cpp"

#define LOGICAL_DISPLAY_BUFFER_WIDTH 128
#define LOGICAL_DISPLAY_BUFFER_HEIGHT 64
#define CGA_BASE_VRAM_ADDRESS MK_FP(0xB800, 0)
#define VIEWPORT_VRAM_ADDRESS MK_FP(0xB800, 648)

enum
{
	BLACK,
	BLUE,
	GREEN,
	CYAN,
	RED,
	MAGENTA,
	BROWN,
	LIGHT_GREY,
	DARK_GREY,
	LIGHT_BLUE,
	LIGHT_GREEN,
	LIGHT_CYAN,
	LIGHT_RED,
	LIGHT_MAGENTA,
	YELLOW,
	WHITE
};

uint8_t far* VRAMViewportBase = (uint8_t far*) MK_FP(0xB800, 648);

unsigned char* palette;

unsigned char tandyPalette[] =
{
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
};

unsigned char rgbPalette[] =
{
	0x00, 0x05, 0x05, 0x05, 0x0a, 0x0a, 0x08, 0x0f, 0x0c, 0x07, 0x07, 0x07, 0x0e, 0x0e, 0x0f, 0x0f,
};

unsigned char compositePalette[] =
{
	0x00, 0x02, 0x01, 0x07, 0x04, 0x06, 0x08, 0x0a, 0x05, 0x03, 0x09, 0x0b, 0x0c, 0x0e, 0x0d, 0x0f
};

unsigned char monoPalette[] =
{
	0x00, 0x05, 0x05, 0x05, 0x0a, 0x0a, 0x08, 0x0f, 0x0c, 0x07, 0x07, 0x07, 0x0e, 0x0e, 0x0f, 0x0f,
};

unsigned char lcdPalette[] =
{
	0x0f, 0x0a, 0x0a, 0x0a, 0x05, 0x05, 0x07, 0x00, 0x03, 0x08, 0x08, 0x08, 0x01, 0x01, 0x00, 0x00,
};

uint8_t* displayBuffer;//[LOGICAL_DISPLAY_BUFFER_WIDTH * LOGICAL_DISPLAY_BUFFER_HEIGHT * 2];

static void BlitBufferToScreen(void* displayBuffer, void far* VRAM);

#pragma aux BlitBufferToScreen = \
	"mov dx, 64"		/* 64 lines */ \
	"_blitLine:" \
	"push si" \
	"mov cx, 32" 		/* Copy 32 words (64 bytes) */ \
	"rep movsw" 		/* ES:DI <- DS:SI */ \
	"pop si" \
	"add di, 0x1fc0"	/* Next page */ \
	"mov cx, 32" 		/* Copy 32 words (64 bytes) */ \
	"rep movsw" 		/* ES:DI <- DS:SI */ \
	"sub di, 0x1ff0"	/* Prev page */ \
	"dec dx" \
	"jnz _blitLine" \	
	modify [dx cx] \	
	parm[si][es di];

static void BlitBufferToScreenTextMode(void* displayBuffer, void far* VRAM);

#pragma aux BlitBufferToScreenTextMode = \
	"mov dx, 64"		/* 64 lines */ \
	"_blitLine:" \
	"mov cx, 64" 		/* Copy 64 words (128 bytes) */ \
	"rep movsw" 		/* ES:DI <- DS:SI */ \
	"add di, 32"	/* Next line */ \
	"dec dx" \
	"jnz _blitLine" \	
	modify [dx cx] \	
	parm[si][es di];

#define TEXTURE_WIDTH 32
#define TEXTURE_HEIGHT 16
#define TEXTURE_EVEN_OFFSET (TEXTURE_WIDTH * TEXTURE_HEIGHT * 2)

#define MAX_SCALER_HEIGHT 64

uint8_t texture[TEXTURE_WIDTH * TEXTURE_HEIGHT * 4];

uint8_t scalerRoutineScratchSpace[2048];

typedef void far* ScalerRoutine;

ScalerRoutine scalerRoutines[MAX_SCALER_HEIGHT][2];
int scalerRoutineDisplayPitch = 64;

ScalerRoutine GenerateScalerRoutineMirrored(int scale, bool isEven)
{
	// DS:DI = framebuffer
	// DS:SI = texture
	// BL = ceiling colour
	// BH = floor colour
	//Test();
	
	//printf("Generating scaler for %d:%d\n", scale, isEven ? 1 : 0);
	
	uint8_t* fn = scalerRoutineScratchSpace;
	
	int prevU = -1;
	
	for(int y = 0; y < 32; y++)
	{
		if(y < 32 - scale)
		{		
			// Background colour
			
			if(isEven)
			{
				// OR [DI+offset], bl
				*fn++ = 0x08;	*fn++ = 0x9D;	
				uint16_t offset = (uint16_t) (y * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);

				// OR [DI+offset], bh
				*fn++ = 0x08;	*fn++ = 0xBD;	
				offset = (uint16_t) ((63 - y) * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
			}
			else
			{
				// MOV [DI+offset], bl
				*fn++ = 0x88;	*fn++ = 0x9D;	
				uint16_t offset = (uint16_t) (y * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);

				// MOV [DI+offset], bh
				*fn++ = 0x88;	*fn++ = 0xBD;	
				offset = (uint16_t) ((63 - y) * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
			}
		}
		else
		{
			int u = y - (32 - scale);
			u = (u * TEXTURE_HEIGHT / 2) / scale;

			if(u != prevU)
			{
				// MOV al, [si+u]
				*fn++ = 0x8A;	*fn++ = 0x44;
				*fn++ = (uint8_t)(u);
				
				prevU = u;
			}
			
			if(isEven)
			{
				// OR [DI+offset], al
				*fn++ = 0x08;	*fn++ = 0x85;
				uint16_t offset = (uint16_t) (y * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
				
				// OR [DI+offset], al
				*fn++ = 0x08;	*fn++ = 0x85;
				offset = (uint16_t) ((63 - y) * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
			}
			else
			{
				// MOV [DI+offset], al
				*fn++ = 0x88;	*fn++ = 0x85;
				uint16_t offset = (uint16_t) (y * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
				
				// MOV [DI+offset], al
				*fn++ = 0x88;	*fn++ = 0x85;
				offset = (uint16_t) ((63 - y) * scalerRoutineDisplayPitch);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
			}
		}
	}
	
	*fn++ = 0xCB;		// RET far
	
	size_t requiredSize = fn - scalerRoutineScratchSpace;
	
	if(requiredSize > 2048)
	{
		fprintf(stderr, "Scaler too big! %d bytes\n", requiredSize);
		exit(1);
	}
	
	ScalerRoutine result = malloc(requiredSize);
	
	if(!result)
	{
		fprintf(stderr, "Could not allocate scaler\n");
		exit(1);
	}
	
	_fmemcpy(result, scalerRoutineScratchSpace, requiredSize);
	
	//printf("Done: %d bytes\n", requiredSize);
	return result;
}


ScalerRoutine GenerateScalerRoutine(int scale, bool isEven)
{
	// DS:DI = framebuffer
	// DS:SI = texture
	// BL = ceiling colour
	// BH = floor colour
	//Test();
	
	//printf("Generating scaler for %d:%d\n", scale, isEven ? 1 : 0);
	
	uint8_t* fn = scalerRoutineScratchSpace;
	
	int prevU = -1;
	
	for(int y = 0; y < 64; y++)
	{
		if(y < 32 - scale || y >= 32 + scale)
		{		
			// Background colour
			
			if(isEven)
			{
				// OR [DI+offset], bl
				*fn++ = 0x08;	*fn++ = 0x9D;	
				uint16_t offset = (uint16_t) (y * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);

				// OR [DI+offset], bh
				*fn++ = 0x08;	*fn++ = 0xBD;	
				offset = (uint16_t) ((63 - y) * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
			}
			else
			{
				// MOV [DI+offset], bl
				*fn++ = 0x88;	*fn++ = 0x9D;	
				uint16_t offset = (uint16_t) (y * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);

				// MOV [DI+offset], bh
				*fn++ = 0x88;	*fn++ = 0xBD;	
				offset = (uint16_t) ((63 - y) * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
			}
		}
		else
		{
			int u = y - (32 - scale);
			u = (u * TEXTURE_HEIGHT) / (2 * scale);

			if(u != prevU)
			{
				// MOV al, [si+u]
				*fn++ = 0x8A;	*fn++ = 0x44;
				*fn++ = (uint8_t)(u);
				
				prevU = u;
			}
			
			if(isEven)
			{
				// OR [DI+offset], al
				*fn++ = 0x08;	*fn++ = 0x85;
				uint16_t offset = (uint16_t) (y * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
				
				// OR [DI+offset], al
				/**fn++ = 0x08;	*fn++ = 0x85;
				offset = (uint16_t) ((63 - y) * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);*/
			}
			else
			{
				// MOV [DI+offset], al
				*fn++ = 0x88;	*fn++ = 0x85;
				uint16_t offset = (uint16_t) (y * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);
				
				// MOV [DI+offset], al
				/**fn++ = 0x88;	*fn++ = 0x85;
				offset = (uint16_t) ((63 - y) * 64);
				*fn++ = (uint8_t)(offset & 0xff);
				*fn++ = (uint8_t)((offset >> 8) & 0xff);*/
			}
		}
	}
	
	*fn++ = 0xCB;		// RET far
	
	size_t requiredSize = fn - scalerRoutineScratchSpace;
	
	if(requiredSize > 2048)
	{
		fprintf(stderr, "Scaler too big! %d bytes\n", requiredSize);
		exit(1);
	}
	
	ScalerRoutine result = malloc(requiredSize);
	
	if(!result)
	{
		fprintf(stderr, "Could not allocate scaler\n");
		exit(1);
	}
	
	_fmemcpy(result, scalerRoutineScratchSpace, requiredSize);
	
	//printf("Done: %d bytes\n", requiredSize);
	return result;
}

void CallScaler(void far* scaler, void* frameBuffer, void* texture, uint8_t ceilingColour, uint8_t floorColour);
#pragma aux CallScaler = \
	"push bp" \
	"push es" \
	"push ax" \
	"mov bp, sp" \
	"call far ptr [bp]" \
	"add sp, 4" \
	"pop bp" \
	modify [ax] \
	parm [es ax][di][si][bl][bh];

#include <math.h>
#define M_PI 3.141592654

#define MAX_ANGLE 1024
#define FOV (MAX_ANGLE / 4)
#define WRAP_ANGLE(x) ((x) & (MAX_ANGLE - 1))

#define MAP_SIZE 8
#define BLOCK_SIZE 128
uint8_t map[MAP_SIZE * MAP_SIZE] =
{
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 1, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 1, 0, 0, 1, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 1, 0, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
};

int16_t cameraX = 384, cameraY = 384;
uint16_t cameraAngle = 0;// MAX_ANGLE / 8;

uint8_t wBuffer[LOGICAL_DISPLAY_BUFFER_WIDTH];
uint8_t uvBuffer[LOGICAL_DISPLAY_BUFFER_WIDTH];

#define MAX_DISTANCE 2000

int16_t fixedSin[MAX_ANGLE];
int16_t fixedCos[MAX_ANGLE];
int16_t oneOverSin[MAX_ANGLE];
int16_t oneOverCos[MAX_ANGLE];
int16_t oneOverSin2[MAX_ANGLE];
int16_t oneOverCos2[MAX_ANGLE];
int16_t relativeCameraAngle[LOGICAL_DISPLAY_BUFFER_WIDTH];
uint8_t distanceToHeight[MAX_DISTANCE];

void GenerateLUTs()
{
	const int maxValue = 1023;
	for(int n = 0; n < MAX_ANGLE; n++)
	{
		float angle = (n * M_PI * 2) / MAX_ANGLE;
		//printf("%f\n", angle);
		//fflush(stdout);
		float sa = sin(angle);
		float ca = cos(angle);

		fixedSin[n] = (int16_t)(sa * 64.5f);
		fixedCos[n] = (int16_t)(ca * 64.5f);
		
		float x = sa == 0 ? maxValue : 64.5f / sa;
		if(x < -maxValue)
			x = -maxValue;
		if(x > maxValue)
			x = maxValue;
		oneOverSin[n] = (int16_t)(x);
		
		float y = ca == 0 ? maxValue : 64.5f / ca;
		if(y < -maxValue)
			y = -maxValue;
		if(y > maxValue)
			y = maxValue;
		oneOverCos[n] = (int16_t)(y);		
		
		oneOverCos2[n] = oneOverCos[n] >> 1;
		oneOverSin2[n] = oneOverSin[n] >> 1;
	}
	
	for(int n = 0; n < LOGICAL_DISPLAY_BUFFER_WIDTH; n++)
	{
		relativeCameraAngle[n] = (int16_t)(((float)n * FOV) / LOGICAL_DISPLAY_BUFFER_WIDTH - (FOV / 2));
	}
	
	for(int n = 0; n < MAX_DISTANCE; n++)
	{
		if(n == 0)
		{
			distanceToHeight[n] = MAX_SCALER_HEIGHT - 1;
		}
		else
		{
			int16_t height = 2000 / n;
			if(height >= MAX_SCALER_HEIGHT)
			{
				height = MAX_SCALER_HEIGHT - 1;
			}
			distanceToHeight[n] = (uint8_t)(height);
		}
	}
}

void CastRays()
{
	int16_t cameraTileX = cameraX >> 8;
	int16_t cameraTileY = cameraY >> 8;
	int16_t cameraMapTile = cameraTileY * MAP_SIZE + cameraTileX;
	
	int16_t cameraTileInnerX = (cameraX & 0xff) >> 2;
	int16_t cameraTileInnerY = (cameraY & 0xff) >> 2;
	//cameraTileInnerX >>= 1;
	//cameraTileInnerY >>= 1;
	
	for(int screenX = 0; screenX < LOGICAL_DISPLAY_BUFFER_WIDTH; screenX++)
	{
		int16_t relativeAngle = relativeCameraAngle[screenX];
		//relativeAngle = 0;
		int16_t angle = WRAP_ANGLE(cameraAngle + relativeAngle);
		int16_t dirX = fixedCos[angle];
		int16_t dirY = fixedSin[angle];
		int16_t currentTile = cameraMapTile;
		
		int timeX = 2000;
		int timeY = 2000;
		
		int deltaX = 0, deltaY = 0;
		
		if(dirX > 0)
		{
			//timeX = (128 - (cameraX & 0x7f)) / (dirX / 128.0f);
			deltaX = oneOverCos[angle];
			
			timeX = (64 - (cameraTileInnerX));
			//timeX = (timeX * (int32_t)deltaX) >> 6;
			timeX = (timeX * oneOverCos2[angle]) >> 5;
		}
		else if(dirX < 0)
		{
			//timeX = (cameraX & 0x7f) / (-dirX / 128.0f);
			deltaX = -oneOverCos[angle];
			
			timeX = cameraTileInnerX;
			//timeX = (timeX * (int32_t)deltaX) >> 6;
			timeX = (timeX * -oneOverCos2[angle]) >> 5;
		}
		if(dirY > 0)
		{
			//timeY = (128 - (cameraY & 0x7f)) / (dirY / 128.0f);
			deltaY = oneOverSin[angle];
			
			timeY = (64 - (cameraTileInnerY));
			//timeY = (timeY * (int32_t)deltaY) >> 6;
			timeY = (timeY * oneOverSin2[angle]) >> 5;
		}
		else if(dirY < 0)
		{
			//timeY = (cameraY & 0x7f) / (-dirY / 128.0f);
			deltaY = -oneOverSin[angle];
			
			timeY = cameraTileInnerY;
			//timeY = (timeY * (int32_t)deltaY) >> 6;
			timeY = (timeY * -oneOverSin2[angle]) >> 5;
		}
		
		
		/*printf("Relative angle: %d\n"
				"Angle: %d\n"
				"Dir X: %d\n"
				"Dir Y: %d\n"
				"Time X: %d\n"
				"Time Y: %d\n"
				"Delta X: %d\n"
				"Delta Y: %d\n",
				relativeAngle, angle, dirX, dirY, timeX, timeY, deltaX, deltaY);
			*/
		//exit(0);
		
		
		int16_t distance = 0;
		int coord;
		//int texDeltaX = (deltaX * dirY) >> 7;
		//int texDeltaY = (deltaY * dirX) >> 7;
		
		while(1)
		{
			if(timeX < timeY)
			{
				if(dirX < 0)
				{
					currentTile--;
				}
				else
				{
					currentTile++;
				}
				
				if(map[currentTile])
				{
					distance = timeX;
					//coord = cameraY + distance * dirY / 128.0f;
					coord = cameraTileInnerY + (int16_t)(((int16_t) distance * dirY) >> 6);
					coord = (coord >> 2) & 15;
					break;
				}
				
				timeX += deltaX;
			}
			else
			{
				if(dirY < 0)
				{
					currentTile -= MAP_SIZE;
				}
				else
				{
					currentTile += MAP_SIZE;
				}		

				if(map[currentTile])
				{
					distance = timeY;
					//coord = cameraX + distance * dirX / 128.0f;
					coord = cameraTileInnerX + (int16_t)(((int16_t) distance * dirX) >> 6);
					coord = (coord >> 2) & 15;
					coord += TEXTURE_WIDTH;
					break;
				}

				timeY += deltaY;
			}
		}

		//printf("%f\t", (float)distance);

		//float relativeAngleF = screenX * 90 / LOGICAL_DISPLAY_BUFFER_WIDTH - 45;
		//float correctDistance = distance * cos(relativeAngleF * M_PI / 180.0f);
		//correctDistance = distance;
		//float correctDistance = (fixedCos[WRAP_ANGLE(relativeAngle)] / 128.0f) * distance;
		//float correctDistance = distance * cos(0.5 * (screenX - LOGICAL_DISPLAY_BUFFER_WIDTH / 2) * M_PI / 180.0f);

		//float correctDistance = (float)fixedCos[WRAP_ANGLE(relativeAngle)] / 128.0f * distance;

		//printf("Distance: %d Corrected: %f\n", distance, correctDistance);
		//exit(0);

		//correctDistance = distance;
		//float heightF = (3000.0f / correctDistance);
		
		//int16_t correctDistance = (fixedCos[WRAP_ANGLE(relativeAngle)] * (distance >> 2)) >> 5;
		//float heightF = (5000.0f / correctDistance);
		//int height = (int) heightF;
		
		int height;
		
		int16_t correctDistance = (fixedCos[WRAP_ANGLE(relativeAngle)] * distance) >> 6;
		if(correctDistance >= MAX_DISTANCE)
		{
			height = 0;
		}
		else
		{
			height = distanceToHeight[correctDistance];
		}
		
		uvBuffer[screenX] = coord; 
		
		//printf("%d %d %d %d\n", rayX, rayY, cameraTileXWorld, cameraTileYWorld);
		//printf("%d %d %d %d %d\n", dirX, dirY, timeX, timeY, height);
		wBuffer[screenX] = height;
		//printf("distance:%f height:%f\n", distance, height);
	}
}

void RenderWalls()
{
	uint8_t floorColour = palette[DARK_GREY];
	uint8_t floorColourEven = floorColour << 4;
	uint8_t ceilingColour = palette[LIGHT_GREY];
	uint8_t ceilingColourEven = ceilingColour << 4;
	
	uint8_t* displayBufferPtr = displayBuffer;
	
	if(currentVideoMode == VIDEO_MODE_TEXT_HACK)
	{
		for(int x = 0; x < LOGICAL_DISPLAY_BUFFER_WIDTH; x++)
		{
			displayBufferPtr ++;
			int textureOffset = uvBuffer[x] * TEXTURE_HEIGHT;
			CallScaler(scalerRoutines[wBuffer[x]][0], displayBufferPtr, texture + textureOffset, ceilingColourEven, floorColourEven);
			x++;
			
			textureOffset = uvBuffer[x] * TEXTURE_HEIGHT;
			CallScaler(scalerRoutines[wBuffer[x]][1], displayBufferPtr, texture + textureOffset + (TEXTURE_EVEN_OFFSET), ceilingColour, floorColour);
			displayBufferPtr ++;
		}
	}
	else
	{
		for(int x = 0; x < LOGICAL_DISPLAY_BUFFER_WIDTH; x++)
		{
			int textureOffset = uvBuffer[x] * TEXTURE_HEIGHT;
			CallScaler(scalerRoutines[wBuffer[x]][0], displayBufferPtr, texture + textureOffset, ceilingColourEven, floorColourEven);
			x++;
			
			textureOffset = uvBuffer[x] * TEXTURE_HEIGHT;
			CallScaler(scalerRoutines[wBuffer[x]][1], displayBufferPtr, texture + textureOffset + (TEXTURE_EVEN_OFFSET), ceilingColour, floorColour);
			displayBufferPtr ++;
		}
	}
}

void GenerateTexture()
{
	for(int y = 0; y < TEXTURE_HEIGHT; y++)
	{
		for(int x = 0; x < TEXTURE_WIDTH; x++)
		{
			int colourIndex1 = 0;
			int colourIndex2 = 0;
			switch(y & 31)
			{
				case 0:
				case 5:
				case 10:
				case 15:
				colourIndex1 = BLACK;
				colourIndex2 = BLACK;
				break;
				case 1:
				case 6:
				case 11:
				colourIndex1 = LIGHT_GREY;
				colourIndex2 = LIGHT_RED;
				break;
				default:
				colourIndex1 = LIGHT_RED;
				colourIndex2 = RED;
				break;
			}
			
			if(x == 5 && (y < 5 || y > 10))
			{
				colourIndex1 = BLACK;
				colourIndex2 = BLACK;
			}
			if(x == 12 && y > 5 && y < 10)
			{
				colourIndex1 = BLACK;
				colourIndex2 = BLACK;
			}
			
			texture[x * TEXTURE_HEIGHT + y + TEXTURE_EVEN_OFFSET] = palette[colourIndex1];
			texture[x * TEXTURE_HEIGHT + y + TEXTURE_EVEN_OFFSET + TEXTURE_WIDTH * TEXTURE_HEIGHT] = palette[colourIndex2];

			texture[x * TEXTURE_HEIGHT + y] = palette[colourIndex1] << 4;
			texture[x * TEXTURE_HEIGHT + y + TEXTURE_WIDTH * TEXTURE_HEIGHT] = palette[colourIndex2] << 4;
		}
	}
}

void GenerateScalers()
{
	for(int n = 0; n < MAX_SCALER_HEIGHT; n++)
	{
		scalerRoutines[n][0] = GenerateScalerRoutineMirrored(n, false);
		scalerRoutines[n][1] = GenerateScalerRoutineMirrored(n, true);
	}
}

void SetCursor()
{
	union REGS inreg, outreg;
	inreg.h.ah = 0x2;
	inreg.h.ah = 0x2;
	inreg.h.bh = 0x0;
	inreg.h.dl = 0;
	inreg.h.dh = 0;

	int86(0x10, &inreg, &outreg);
}

VideoMode ChooseScreenMode()
{
	printf("(T) Tandy\n");
	printf("(R) CGA RGB\n");
	printf("(C) CGA Composite\n");
	printf("(M) CGA Monochrome\n");
	printf("(L) CGA LCD\n");
	printf("(H) Hercules\n");
	printf("(A) Text mode 160x100\n");
	
	char choice = getch();
	
	switch(choice)
	{
		case 't':
			palette = tandyPalette;
		return VIDEO_MODE_TANDY;
		case 'r':
			palette = rgbPalette;
		return VIDEO_MODE_CGA_RGB;
		case 'c':
			palette = compositePalette;
		return VIDEO_MODE_CGA_COMPOSITE;
		case 'm':
			palette = monoPalette;
		return VIDEO_MODE_CGA_MONOCHROME;
		case 'l':
			palette = lcdPalette;
		return VIDEO_MODE_CGA_LCD;
		case 'h':
			palette = monoPalette;
			VRAMViewportBase = (uint8_t far*) MK_FP(0xB000, 648);
		return VIDEO_MODE_HERCULES;
		case 'a':
			palette = tandyPalette;
			VRAMViewportBase = (uint8_t far*) MK_FP(0xB800, 1296);
			scalerRoutineDisplayPitch = 128;
		return VIDEO_MODE_TEXT_HACK;
		default:
			exit(0);
		return VIDEO_MODE_NULL;
	}
}

int main()
{
	printf("Starting!\n");
	VideoMode chosenMode = ChooseScreenMode();

	printf("Generating texture..\n");
	GenerateTexture();
	printf("Generating LUTs..\n");
	GenerateLUTs();
	printf("Generating Scalers..\n");
	GenerateScalers();
	
	InitVideo(chosenMode);
	InitTimer();
	InitKeyboard();
	
	if(chosenMode == VIDEO_MODE_TEXT_HACK)
	{
		// Text mode hack requires double the space for the characters
		displayBuffer = new uint8_t[LOGICAL_DISPLAY_BUFFER_WIDTH * LOGICAL_DISPLAY_BUFFER_HEIGHT * 2];

		for(int n = 0; n < LOGICAL_DISPLAY_BUFFER_HEIGHT * LOGICAL_DISPLAY_BUFFER_WIDTH * 2; n++)
		{
			displayBuffer[n] = 0xde;
		}
	}
	else
	{
		displayBuffer = new uint8_t[LOGICAL_DISPLAY_BUFFER_WIDTH * LOGICAL_DISPLAY_BUFFER_HEIGHT];
	}

	milliseconds = 0;
	
	int fpsCounter = 0;
	unsigned long frameStartTime = 0;
	unsigned long logicTimer = 0;
	unsigned long fpsTimer = 1000;
	
	uint16_t far* VRAM = (uint16_t far*) VRAMBase;
	uint16_t backgroundPattern = palette[LIGHT_BLUE] | (palette[LIGHT_BLUE] << 4)  | (palette[LIGHT_BLUE] << 8)  | (palette[LIGHT_BLUE] << 12);
	
	if(currentVideoMode == VIDEO_MODE_TEXT_HACK)
	{
		for(int n = 0; n < 8000; n++)
		{
			VRAM[n] = backgroundPattern;
		}
	}
	else
	{
		for(int n = 0; n < 0x2000; n++)
		{
			VRAM[n] = backgroundPattern;
		}
	}

	while(1)
	{
		frameStartTime = milliseconds;
		
		CastRays();
		RenderWalls();

		if(currentVideoMode == VIDEO_MODE_TEXT_HACK)
		{
			BlitBufferToScreenTextMode(displayBuffer, VRAMViewportBase);
		}
		else
		{
			BlitBufferToScreen(displayBuffer, VRAMViewportBase);
		}
		
		unsigned long frameTime = (milliseconds - frameStartTime);
		int ticksPassed = 0;
		
		while(logicTimer < milliseconds && ticksPassed < 5)
		{
			logicTimer += 33;
			ticksPassed++;
		}

		fpsCounter++;
		if(milliseconds > fpsTimer)
		{
			SetCursor();
			printf("%d  \n", fpsCounter);
			fpsCounter = 0;
			fpsTimer = milliseconds + 1000;
		}
		
		if (normalKeys[sc_arrowLeft] || extendedKeys[sc_arrowLeft])	// LEFT
		{
			cameraAngle = WRAP_ANGLE(cameraAngle - 5 * ticksPassed);
		}
		if (normalKeys[sc_arrowRight] || extendedKeys[sc_arrowRight]) // RIGHT
		{
			cameraAngle = WRAP_ANGLE(cameraAngle + 5 * ticksPassed);
		}
		if (normalKeys[sc_arrowUp] || extendedKeys[sc_arrowUp]) // UP
		{
			cameraX += (ticksPassed * fixedCos[cameraAngle]) >> 3;
			cameraY += (ticksPassed * fixedSin[cameraAngle]) >> 3;
		}

		if (normalKeys[sc_escape])	// Escape
		{
			break;
		}
	}

	delete[] displayBuffer;

	ShutdownTimer();
	ShutdownKeyboard();
	ShutdownVideo();
	
	return 0;
}
