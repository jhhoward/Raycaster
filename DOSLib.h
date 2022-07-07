#pragma once

#include <stdint.h>

enum VideoMode
{
	VIDEO_MODE_NULL = 0,
	VIDEO_MODE_TANDY,
	VIDEO_MODE_CGA_RGB,
	VIDEO_MODE_CGA_COMPOSITE,
	VIDEO_MODE_CGA_MONOCHROME,
	VIDEO_MODE_CGA_LCD,
	VIDEO_MODE_HERCULES,
	VIDEO_MODE_TEXT_HACK
};

typedef void (__interrupt __far* INTFUNCPTR)(void);

void InitVideo(VideoMode mode);
void ShutdownVideo();

void InitTimer();
void ShutdownTimer();

void InitKeyboard();
void ShutdownKeyboard();

extern VideoMode currentVideoMode;
extern uint8_t far* VRAMBase;

extern volatile long int milliseconds; 
extern unsigned char normalKeys[0x60];
extern unsigned char extendedKeys[0x60];
