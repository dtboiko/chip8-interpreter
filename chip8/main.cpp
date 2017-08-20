#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sdl.h>

#define CLOCK_RATE 60
#define SCREEN_HEIGHT 768
#define SCREEN_WIDTH 384
#define MEM_SIZE 4096
#define FRAME_BUFFER_SIZE 2048
#define STACK_SIZE 16

void run();
int loadROM(char*);
void test();

bool assumeXshifts = false;
bool assumeIUnchanged = false;
bool drawRequired = false;

// SDL stuff
SDL_Window *window;
SDL_Renderer *renderer;
SDL_AudioSpec want, have;
SDL_AudioDeviceID dev;

uint8_t V[16] = {0}; // Sixteen 8-bit registers: V0 - VF
// VF register used for storing carry values from subtraction or addition,
//and also specifies whether a particular pixel is to be drawn on the screen.
uint16_t I; // 16 bit index register
uint16_t S[STACK_SIZE] = {0};
uint8_t SP; // 8-bit stack pointer
uint8_t DT; // 8-bit delay timer
uint8_t ST; // 8-bit sound timer
uint8_t screen[FRAME_BUFFER_SIZE] = {0}; // 64 * 32 bit frame buffer (x, y) addressible on or off
uint8_t actualScreen[SCREEN_HEIGHT*SCREEN_WIDTH] = {0}; // 64 * 32 bit frame buffer (x, y) addressible on or off
uint16_t PC = 0x200; // Stores next instruction to execute. Programs start from address 0x200

// temp - virtual keyboard
uint8_t keyDown[16] = {0};

// programs will start at memory location 0x200
uint8_t memory[MEM_SIZE] = {
	0xF0,0x90,0x90,0x90,0xF0, // 0
	0x20,0x60,0x20,0x20,0x70, // 1
	0xF0,0x10,0xF0,0x80,0xF0, // 2
	0xF0,0x10,0xF0,0x10,0xF0, // 3
	0x90,0x90,0xF0,0x10,0x10, // 4
	0xF0,0x80,0xF0,0x10,0xF0, // 5
	0xF0,0x80,0xF0,0x90,0xF0, // 6
	0xF0,0x10,0x20,0x40,0x40, // 7
	0xF0,0x90,0xF0,0x90,0xF0, // 8
	0xF0,0x90,0xF0,0x10,0xF0, // 9
	0xF0,0x90,0xF0,0x90,0x90, // A
	0xE0,0x90,0xE0,0x90,0xE0, // B
	0xF0,0x80,0x80,0x80,0xF0, // C
	0xE0,0x90,0x90,0x90,0xE0, // D
	0xF0,0x80,0xF0,0x80,0xF0, // E
	0xF0,0x80,0xF0,0x80,0x80, // F
}; // 4096 bytes of addressable memory

/* Helper functions 
*/
uint16_t nnn(uint16_t instr)
{
	return instr & 0x0FFF;
}

uint8_t nn(uint16_t instr)
{
	return instr & 0x00FF;
}

uint8_t getX(uint16_t instr)
{
	return instr & 0x0F00;
}

uint8_t lowNibble(uint8_t h)
{
	return h & 0x0F;
}

uint8_t getY(uint16_t instr)
{
	return instr & 0x00F0;
}

uint8_t highNibble(uint8_t l)
{
	return (l & 0xF0)>>4;
}

void SUB(uint8_t x, uint8_t y)
{
	if (V[x] < V[y])
		V[0xF] = 0;
	else
		V[0xF] = 1;
	V[x] -= V[y];
}

// copypasta
void Foo(void *unused, Uint8 *stream, int len) {
	for (int i=0;i<len;i++) {
		stream[i] = i;
	}
}

int main(int argc, char** argv)
{
	// store whole emulator inside the memory

	SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_TIMER);
	SDL_CreateWindowAndRenderer(SCREEN_HEIGHT, SCREEN_WIDTH, 0, &window, &renderer);

	if (window == 0)
	{
		printf("Could not create window: %s\n", SDL_GetError());
	}

	SDL_SetWindowTitle(window, "CHIP-8 Interpreter");

	// loop
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);

	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_F32;
	want.channels = 2;
	want.samples = 4096;
	want.callback = Foo;

	dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FORMAT_CHANGE);
	if (dev == 0)
		SDL_Log("failed to open");

	if (loadROM("ROMS/PONG") != 0)
	{
		printf("Failed to load ROM\n");
		return 1;
	}

	run();
	return 0;
}

int loadROM(char* fileName)
{
	if (fileName == "ROMS/BLINKY" || fileName == "ROMS/INVADERS")
		assumeXshifts = true;

	if (fileName == "ROMS/BLINKY" || fileName == "ROMS/CONNECT4" || fileName == "ROMS/HIDDEN" || fileName == "ROMS/TICTAC")
		assumeIUnchanged = true;

	FILE* file = fopen(fileName, "rb");
	if (file == 0)
		return 1;
	uint8_t* data = memory+PC;
	int c;
	while ((c = fgetc(file)) != EOF)
	{
		*data++ = c;
	}
	fclose(file);
	return 0;
}

void DrawScreen()
{
	// change this to use SDL_Texture
	SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
	//https://gamedev.stackexchange.com/questions/98625/sdl-pixel-access-very-slow
	SDL_Rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = 12;
	rect.h = 12;
	for (int r = 0; r < 32; r++)
	{
		for (int c = 0; c < 64; c++)
		{
			rect.x = (c*12);
			rect.y = (r*12);
			if (screen[c+(64*r)] == 1)
			{
				SDL_RenderFillRect(renderer, &rect);
			}
			else
			{
				SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0x00);
				SDL_RenderFillRect(renderer, &rect);
				SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
			}
		}
	}
	SDL_RenderPresent(renderer);
}

void cleanup()
{
	SDL_DestroyWindow(window);
	SDL_CloseAudioDevice(dev);
	SDL_Quit();
	exit(0);
}

SDL_Keycode getInput()
{
	SDL_Keycode key = -1;
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch(event.type)
		{
		case SDL_QUIT:
			cleanup();
		case SDL_KEYDOWN:
			{
				key = event.key.keysym.sym;
				if (key >= SDLK_0 && key <= SDLK_9)
				{
					keyDown[key-0x30]=1;
					key = key-0x30;
				}
				else if (key >= SDLK_a && key <= SDLK_f)
				{
					keyDown[key-0x57]=1;
					key = key-0x57;
				}
				else if (key == SDLK_ESCAPE)
					cleanup();
				break;
			}
		case SDL_KEYUP:
			key = event.key.keysym.sym;
			if (key >= SDLK_0 && key <= SDLK_9)
			{
				keyDown[key-0x30]=0;
			}
			else if (key >= SDLK_a && key <= SDLK_f)
			{
				keyDown[key-0x57]=0;
			}
			break;
		}
	}
	return key;
}

void run()
{
	unsigned int startTime = SDL_GetTicks();

	while (PC < MEM_SIZE)
	{
		getInput();

		int cycleTime = SDL_GetTicks() - startTime;
		int delay = 2;
		if (ST == 0)
			SDL_PauseAudioDevice(dev, 1);
		if (cycleTime >= 1000/CLOCK_RATE)
		{
			if (DT > 0)
			{
				//startTime = SDL_GetTicks();
				DT--;
				//SDL_Delay(16.6666666667);
			}
			//printf("%d\n", DT);
			if (ST > 0)
			{
				SDL_PauseAudioDevice(dev, 0);
				ST--;
				//SDL_Delay(16.6666666667);
			}
			startTime = SDL_GetTicks();
		}
		uint8_t h = memory[PC];
		uint8_t l = memory[PC+1];
		uint16_t opcode = (h << 8) | l;
		switch (h>>4)
		{
		case 0x0:
			if (l == 0xE0) // CLS - Clear the display
			{
				memset(screen, 0, FRAME_BUFFER_SIZE);
			}
			else if (l == 0xEE) // RET - Return from a subroutine
			{
				PC = S[--SP];
				//fS[SP] = 0; // test
				continue;
			}
			break;
		case 0x1: // JP addr - Jump to location nnn
			PC = nnn(opcode);
			continue;
		case 0x2: // CALL addr - Call subroutine at nnn
			S[SP++] = PC+2;
			PC = nnn(opcode);
			continue;
		case 0x3: // SE Vx, byte - Skip next instruction if Vx = kk
			if (V[lowNibble(h)] == l)
				PC += 2;
			break;
		case 0x4: // SNE Vx, byte - Skip next instruction if Vx != kk
			if (V[lowNibble(h)] != l)
				PC += 2;
			break;
		case 0x5: // SE Vx, Vy - Skip next instruction if Vx = Vy
			if (V[lowNibble(h)] == highNibble(l))
				PC += 2;
			break;
		case 0x6: // LD Vx, byte - Store value kk in register Vx
			V[lowNibble(h)] = l;
			break;
		case 0x7: // ADD Vx, byte - Add the value of kk to register Vx, will wraparound if result > 255
			V[lowNibble(h)] += l;
			break;
		case 0x8:
			{
				uint8_t lastNibble = l & 0x0F;
				uint8_t x = lowNibble(h);
				uint8_t y = highNibble(l);
				if (lastNibble == 0) // LD (Vx, Vy) - Stores the value of Vy in register Vx
					V[x] = V[y];
				else if (lastNibble == 1) // OR (Vx, Vy) - Performs a bitwise OR on the values of Vx and Vy and stores the result in Vx 
					V[x] |= V[y];
				else if (lastNibble == 2) // AND (Vx, Vy) - Performs a bitwise AND on the values of Vx and Vy and stores the result in Vx 
					V[x] &= V[y];
				else if (lastNibble == 3) // XOR (Vx, Vy) - Performs a bitwise XOR on the values of Vx and Vy and stores the result in Vx 
					V[x] ^= V[y];
				else if (lastNibble == 4) // ADD (Vx, Vy) - Stores addition of Vx and Vy in register Vx, sets carry if result overflows
				{ 
					uint16_t result = V[x] + V[y];
					if (result > 0xFF)
						V[0xF] = 1;
					else
						V[0xF] = 0;
					V[x] += V[y];
				}
				else if (lastNibble == 5) // SUB (Vx, Vy) - Stores subtraction of Vx and Vy in register Vx, sets carry if result underflows
				{ 
					SUB(x, y);
				}
				else if (lastNibble == 6) // SHR Vx {, Vy} - Store the value of Vy shifted to the right by one in register Vx, VF = LSB before shift
				{ 
					if (!assumeXshifts)
					{
						V[0xF] = V[y] & 1;
						V[x] = V[y] >> 1;
					}

					else
					{
						V[0xF] = V[x] & 1;
						V[x] = V[x] >> 1;
					}
				}
				else if (lastNibble == 7) { // SUBN (Vx, Vy)
					SUB(y, x);
				}
				else if (lastNibble == 0xE) { // SHL Vx {, Vy}- Store the value of Vy shifted to the left by one in register Vx, VF = MSB before shift
					if (!assumeXshifts)
					{
						V[0xF] = V[y] & 128;
						V[x] = V[y] << 1;
					}
					else
					{
						V[0xF] = V[x] & 128;
						V[x] = V[x] << 1;
					}
				}
				break;
			}
		case 0x9: // SNE Vx, Vy - Skip next instruction is Vx != Vy
			if (V[lowNibble(h)] != V[highNibble(l)])
				PC += 2;
			break;
		case 0xA: // LD (I, addr)
			I = nnn(opcode);
			break;
		case 0xB:  // JP (V0, addr)
			PC = V[0] + nnn(opcode);
			continue;
		case 0xC: // RND (Vx, byte)
			V[lowNibble(h)] = l & (rand()%256);
			break;
		case 0xD:  // DRW (Vx, Vy, nibble)
			{ 
				drawRequired = true;
				uint8_t x = lowNibble(h);
				uint8_t y = highNibble(l);
				uint8_t height = l & 0x0F;
				V[0xF] = 0;
				if ((V[x] > 0x3F) || (V[y] > 0x1F)) // sprite is off the screen
					break;
				for (int i = 0; i < height; i++)
				{
					uint8_t spriteData = memory[I+i];
					for (int j = 0; j < 8; j++)
					{
						if (spriteData & 128)
						{
							uint16_t index = (V[x]+j) + ((i+V[y])*64);
							if (screen[index] == 1)
								V[0xF] = 1;
							screen[index] ^= 1;
							//screen[(V[x]+j)+(V[y]*64)] ^= spriteData;
						}
						spriteData <<= 1;
					}
				}
			}
			break;
		case 0xE:
			if (l == 0x9E) // SKP (Vx) - Skip the following instruction if key stored in register Vx is currently pressed.
			{
				uint8_t key = V[lowNibble(h)];
				if (keyDown[key] == 1)
				{
					keyDown[key] = 0;
					PC += 2;
				}
			}
			else if (l == 0xA1) // Skip the following instruction if key stored in register Vx is not currently pressed.
			{
				uint8_t key = V[lowNibble(h)];
				if (keyDown[key] == 0)
				{
					PC += 2;
				}
			}
			break;
		case 0xF:
			if (l == 0x07) // LD (Vx, DT) - Store the current value of the delay timer in register Vx 
				V[lowNibble(h)] = DT;
			else if (l == 0x15) // LD (DT, Vx) - Set the delay timer to the value of register Vx
				DT = V[lowNibble(h)];
			else if (l == 0x18) // LD (ST, Vx) - Sets the sound timer to the value of register Vx
				ST = V[lowNibble(h)];
			else if (l == 0x0A) { // LD (Vx, K) - Wait for a keypress and store the result in register Vx
				SDL_Keycode key;
				while (key = getInput() == -1)
					SDL_Delay(16);
				V[lowNibble(h)] = key;
			}
			else if (l == 0x1E) // ADD (I, Vx) - Add the value stored in register Vx to register I
				I += V[lowNibble(h)];
			else if (l == 0x29) // LD (F, Vx) - Set the value of I to the memory address of the sprite data for thingy stored in Register Vx 
				I = V[lowNibble(h)]*5;
			else if (l == 0x33) // LD (F, Vx) - Stores the binary coded decimal (BCD) equivalent of the value in register Vx at addresses I..I+2
			{
				int val = V[lowNibble(h)];
				for (int i = 2; i >= 0; val /= 10, i--)
				{
					memory[I+i] = val % 10;
				}
			}
			else if (l == 0x55) // LD ([I], Vx) - Store the values of registers V0 to VX in memory starting at address I
			{ 
				int X = lowNibble(h);
				for (int i = 0; i <= X; i++)
				{
					memory[I+i] = V[i];
				}
				if (!assumeIUnchanged) 
					I += X+1;
			}
			else if (l == 0x65) // LD (Vx, [I])
			{
				int X = lowNibble(h);
				for (int i = 0; i <= X; i++)
				{
					V[i] = memory[I+i];
				}
				if (!assumeIUnchanged)
					I += X+1;
			}
			break;
		default:
			printf("problem");
			getchar();
		}
		if (drawRequired)
		{
			DrawScreen();
			drawRequired = false;
		}
		SDL_Delay(2);
		PC += 2;
	}
}