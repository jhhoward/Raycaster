#include <stdio.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <stdint.h>
#include <malloc.h>
#include <memory.h>

#include "DOSLib.h"

VideoMode currentVideoMode;
uint8_t far* VRAMBase = (uint8_t far*) MK_FP(0xB800, 0);
volatile long int milliseconds = 0; 

unsigned char normalKeys[0x60];
unsigned char extendedKeys[0x60];

static INTFUNCPTR oldTimerInterrupt; 
static INTFUNCPTR oldKeyboardInterrupt; 

static int startingBiosMode = -1;

static void SetBIOSVideoMode(int mode)
{
	union REGS inreg, outreg;
	inreg.h.ah = 0;
	inreg.h.al = (unsigned char)mode;
	int86(0x10, &inreg, &outreg);
}

static void EnableCGAColourBurst()
{	
	outp(0x3D8, 0x1A);
}

static void EnableAlternateCGAPalette(bool isBright = false)
{	
	union REGS inreg, outreg;

	if(isBright)
	{
		// Set Cyan / red palette (CGA)
		outp(0x3d9, 0x30);
		outp(0x3D8, 0xE);
		
		// Set Cyan / red palette (EGA / VGA)
		inreg.w.ax = 0x1000;
		inreg.h.bl = 0x2;
		inreg.h.bh = 0x14;
		int86(0x10, &inreg, &outreg);
	}
	else
	{
		// Set Cyan / red palette (CGA)
		outp(0x3d9, 0x20);
		outp(0x3D8, 0xE);
		
		// Set Cyan / red palette (EGA / VGA)
		inreg.w.ax = 0x1000;
		inreg.h.bl = 0x2;
		inreg.h.bh = 4;
		int86(0x10, &inreg, &outreg);
	}
}

static void SetHerculesMode()
{
    //byte Graph_640x400[12] = {0x03, 0x34, 0x28, 0x2A, 0x47, 0x69, 0x00, 0x64, 0x65, 0x02, 0x03, 0x0A};
    uint8_t Graph_640x200[12] = {0x03, 0x6E, 0x28, 0x2E, 0x07, 0x67, 0x0A, 0x64, 0x65, 0x02, 0x01, 0x0A};
    int i;

    outp(0x03BF, Graph_640x200[0]);
    for (i = 0; i < 10; i++)
    {
        outp(0x03B4, i);
        outp(0x03B5, Graph_640x200[i + 1]);
    }
    outp(0x03B8, Graph_640x200[11]);

	VRAMBase = (uint8_t far*) MK_FP(0xB000, 0);
}

static void SetTextHackMode()
{
	union REGS inreg, outreg;
	
	uint8_t charLineCount = 1;
	uint8_t switches;

	/* get EGA switch settings */
	inreg.h.ah = 0x12;
	inreg.h.al = 0x0;
	inreg.h.bl = 0x10;
	int86(0x10, &inreg, &outreg);
	switches = outreg.h.cl;
	
	inreg.h.ah = 0x1a;
	inreg.h.al = 0x0;
	int86(0x10, &inreg, &outreg);
	uint8_t active = outreg.h.bl;
	uint8_t status = outreg.h.al;

	if (status == 0x1a && (active == 0x07 || active == 0x08))   /* VGA color or mono*/
	{
		charLineCount = 3;
	}
	else if ( /* EGA */
				switches == 0x6 ||	/* CGA w/CGA 40x25 */
				switches == 0x7 ||	/* CGA w/CGA 80x25 */
				switches == 0x8 ||	/* EGA w/CGA 80x25 */
				switches == 0x9 ||	/* EGA w/ECD 80x25 */
				switches == 0xB 	/* EGA w/MONO */
				) {

		/* turn off blink via EGA BIOS */
		inreg.h.ah = 0x10;
		inreg.h.al = 0x03;
		inreg.h.bl = 0x0;
		int86(0x10, &inreg, &outreg);

		/* EGA hires mode is 640x350 with a 9x14 character cell.  The pixel aspect
			ratio is 1:1.37, so if we make the blocks 3 scans tall you get a square
			pixel at 160x100, but some of the scan lines are not used (50) */
		
		if (
			switches == 0x09 ||		/* EGA Hires monitor attached, 9x14 */
			switches == 0xB		/* EGA with Monochrome monitor, 9x14 */
			) {
				charLineCount = 2;
			}

		/* set char scan line count */
		outp(0x3D4, 0x09);
		outp(0x3D5, charLineCount);
		return;
	}	
	
	// Set 80x25 color mode
    inreg.h.ah = 0x00;
    inreg.h.al = 0x03;
    int86(0x10, &inreg, &outreg);

    // Disable cursor
    inreg.h.ah = 0x01;
    inreg.h.ch = 0x3F;
    int86(0x10, &inreg, &outreg);

    // Disable blinking
    inreg.h.ah = 0x10;
    inreg.h.al = 0x03;
    inreg.h.bl = 0x00;
    inreg.h.bh = 0x00;
    int86(0x10, &inreg, &outreg);

    /* set mode control register for 80x25 text mode and disable video output */
    outp(0x3D8, 1);

    /*
        These settings put the 6845 into "graphics" mode without actually
        switching the CGA controller into graphics mode.  The register
        values are directly copied from CGA graphics mode register
        settings.  The 6845 does not directly display graphics, the
        6845 only generates addresses and sync signals, the CGA
        attribute controller either displays character ROM data or color
        pixel data, this is external to the 6845 and keeps the CGA card
        in text mode.
        ref: HELPPC
    */
	

    /* set vert total lines to 127 */
    outp(0x3D4, 0x04);
    outp(0x3D5, 0x7F);
    /* set vert displayed char rows to 100 */
    outp(0x3D4, 0x06);
    outp(0x3D5, 0x64);
    /* set vert sync position to 112 */
    outp(0x3D4, 0x07);
    outp(0x3D5, 0x70);
    /* set char scan line count */
    outp(0x3D4, 0x09);
    outp(0x3D5, charLineCount);

    /* re-enable the video output in 80x25 text mode */
    outp(0x3D8, 9);		
}

void InitVideo(VideoMode mode)
{
	if(startingBiosMode == -1)
	{
		union REGS inreg, outreg;
		inreg.h.al = 0;
		inreg.h.ah = 0xf;

		int86(0x10, &inreg, &outreg);

		startingBiosMode = outreg.h.al;
	}
	
	switch(mode)
	{
		case VIDEO_MODE_TANDY:
			SetBIOSVideoMode(8);
		break;
		case VIDEO_MODE_CGA_RGB:
			SetBIOSVideoMode(5);
			EnableAlternateCGAPalette();
		break;
		case VIDEO_MODE_CGA_COMPOSITE:
			SetBIOSVideoMode(6);
			EnableCGAColourBurst();
		break;
		case VIDEO_MODE_CGA_MONOCHROME:
			SetBIOSVideoMode(6);
		break;
		case VIDEO_MODE_CGA_LCD:
			SetBIOSVideoMode(6);
		break;
		case VIDEO_MODE_HERCULES:
			SetHerculesMode();
		break;
		case VIDEO_MODE_TEXT_HACK:
			SetTextHackMode();
		break;
		default:
		return;
	}
	
	currentVideoMode = mode;
}

void ShutdownVideo()
{
	if(startingBiosMode != -1)
	{
		SetBIOSVideoMode(startingBiosMode);		
	}
}

static void __interrupt __far TimerHandler(void)
{
	static unsigned long count = 0; // To keep track of original timer ticks
	++milliseconds;
	count += 1103;

	if (count >= 65536) // It is now time to call the original handler
	{
		count -= 65536;
		_chain_intr(oldTimerInterrupt);
	}
	else
		outp(0x20, 0x20); // Acknowledge interrupt
}

void InitTimer()
{
	union REGS r;
	struct SREGS s;
	_disable();
	segread(&s);
	/* Save old interrupt vector: */
	r.h.al = 0x08;
	r.h.ah = 0x35;
	int86x(0x21, &r, &r, &s);
	oldTimerInterrupt = (INTFUNCPTR)MK_FP(s.es, r.x.bx);
	/* Install new interrupt handler: */
	milliseconds = 0;
	r.h.al = 0x08;
	r.h.ah = 0x25;
	s.ds = FP_SEG(TimerHandler);
	r.x.dx = FP_OFF(TimerHandler);
	int86x(0x21, &r, &r, &s);
	/* Set resolution of timer chip to 1ms: */
	outp(0x43, 0x36);
	outp(0x40, (unsigned char)(1103 & 0xff));
	outp(0x40, (unsigned char)((1103 >> 8) & 0xff));
	_enable();
}

void ShutdownTimer()
{
	union REGS r;
	struct SREGS s;
	_disable();
	segread(&s);
	/* Re-install original interrupt handler: */
	r.h.al = 0x08;
	r.h.ah = 0x25;
	s.ds = FP_SEG(oldTimerInterrupt);
	r.x.dx = FP_OFF(oldTimerInterrupt);
	int86x(0x21, &r, &r, &s);
	/* Reset timer chip resolution to 18.2...ms: */
	outp(0x43, 0x36);
	outp(0x40, 0x00);
	outp(0x40, 0x00);
	_enable();
}

static void __interrupt __far KeyboardHandler(void)
{
	static unsigned char buffer;
	unsigned char rawcode;
	unsigned char make_break;
	int scancode;
	unsigned char temp;

	rawcode = inp(0x60); /* read scancode from keyboard controller */

	// Tell the XT keyboard controller to clear the key
	outp(0x61, (temp = inp(0x61)) | 0x80);
	outp(0x61, temp);

	make_break = !(rawcode & 0x80); /* bit 7: 0 = make, 1 = break */
	scancode = rawcode & 0x7F;

	if (buffer == 0xE0) { /* second byte of an extended key */
		if (scancode < 0x60) {
			extendedKeys[scancode] = make_break;
		}
		buffer = 0;
	}
	else if (buffer >= 0xE1 && buffer <= 0xE2) {
		buffer = 0; /* ingore these extended keys */
	}
	else if (rawcode >= 0xE0 && rawcode <= 0xE2) {
		buffer = rawcode; /* first byte of an extended key */
	}
	else if (scancode < 0x60) {
		normalKeys[scancode] = make_break;
	}

	outp(0x20, 0x20); /* must send EOI to finish interrupt */
}

void InitKeyboard()
{
	oldKeyboardInterrupt = _dos_getvect(0x09);
	_dos_setvect(0x09, KeyboardHandler);
}

void ShutdownKeyboard()
{
	if (oldKeyboardInterrupt != NULL)
	{
		_dos_setvect(0x09, oldKeyboardInterrupt);
		oldKeyboardInterrupt = NULL;
	}
}
