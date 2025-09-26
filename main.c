#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <gba.h>

// why is this not already typedef'd???
typedef uint64_t u64;

#define NO_UNROLL _Pragma("GCC unroll 0")
#define FORCE_INLINE inline __attribute__((always_inline))
#define NO_INLINE __attribute__((noinline))

// RNG

// This state can be seeded with any value.
IWRAM_DATA static uint64_t splitmix64_state;

IWRAM_CODE uint64_t splitmix64_next()
{
	uint64_t z = (splitmix64_state += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

IWRAM_CODE static FORCE_INLINE uint32_t rotl(const uint32_t x, int k)
{
	return (x << k) | (x >> (32 - k));
}

IWRAM_DATA static bool xoshiro128pp_init;
IWRAM_DATA static uint32_t xoshiro128pp_state[4];

IWRAM_CODE static NO_INLINE void init_xoshiro128pp()
{
	do
	{
		xoshiro128pp_state[0] = splitmix64_next();
		xoshiro128pp_state[1] = splitmix64_next();
		xoshiro128pp_state[2] = splitmix64_next();
		xoshiro128pp_state[3] = splitmix64_next();
	}
	while ((xoshiro128pp_state[0] | xoshiro128pp_state[1] | xoshiro128pp_state[2] | xoshiro128pp_state[3]) == 0);
}

IWRAM_CODE uint32_t xoshiro128pp_next(void)
{
	if (!xoshiro128pp_init)
	{
		init_xoshiro128pp();
		xoshiro128pp_init = true;
	}

	const uint32_t result = rotl(xoshiro128pp_state[0] + xoshiro128pp_state[3], 7) + xoshiro128pp_state[0];

	const uint32_t t = xoshiro128pp_state[1] << 9;

	xoshiro128pp_state[2] ^= xoshiro128pp_state[0];
	xoshiro128pp_state[3] ^= xoshiro128pp_state[1];
	xoshiro128pp_state[1] ^= xoshiro128pp_state[2];
	xoshiro128pp_state[0] ^= xoshiro128pp_state[3];

	xoshiro128pp_state[2] ^= t;

	xoshiro128pp_state[3] = rotl(xoshiro128pp_state[3], 11);

	return result;
}

#define FLASH_BASE ((vu8 *)0xE000000)

#define FLASH_READ(addr) (*(vu8 *)(FLASH_BASE + (addr)))
#define FLASH_WRITE(addr, data) ((*(vu8 *)(FLASH_BASE + (addr))) = (data))

#define FLASH_DELAY() do { for (vu16 i = 20000; i != 0; i--) {} } while (0)

#define FLASH_SECTOR_COUNT 32
#define FLASH_SECTORS_PER_BANK 16
#define FLASH_BYTES_PER_SECTOR 4096
#define FLASH_SECTOR_SHIFT 12

IWRAM_CODE static u16 ReadFlashID()
{
	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0x90);
	FLASH_DELAY();

	u16 flashId;
	flashId = FLASH_READ(1) << 8;
	flashId |= FLASH_READ(0);

	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0xF0);
	FLASH_WRITE(0x5555, 0xF0);
	FLASH_DELAY();

	return flashId;
}

struct FlashSectorTiming
{
	u32 MinCycles;
	u32 MaxCycles;
	u32 MeanCycles;
	u32 NumFailures;
};

#define NUM_FLASH_TESTS 6

struct FlashTiming
{
	struct FlashSectorTiming EraseAllFFTimings[FLASH_SECTOR_COUNT];
	struct FlashSectorTiming EraseAll00Timings[FLASH_SECTOR_COUNT];
	struct FlashSectorTiming EraseAllRandomTimings[FLASH_SECTOR_COUNT];
	struct FlashSectorTiming ProgramFFTimings[FLASH_SECTOR_COUNT];
	struct FlashSectorTiming Program00Timings[FLASH_SECTOR_COUNT];
	struct FlashSectorTiming ProgramRandomTimings[FLASH_SECTOR_COUNT];
};

// erase tests have 10 iterations done
// program tests have 10 iterations done per byte
// the larger of the two must be buffered for timing results
#define NUM_ERASE_TEST_ITERATIONS_PER_SECTOR 10
#define NUM_PROGRAM_TEST_ITERATIONS_PER_BYTE 10
#define NUM_PROGRAM_TEST_ITERATIONS_PER_SECTOR (NUM_PROGRAM_TEST_ITERATIONS_PER_BYTE * FLASH_BYTES_PER_SECTOR)
#define MAX_TEST_ITERATIONS_PER_SECTOR (NUM_ERASE_TEST_ITERATIONS_PER_SECTOR > NUM_PROGRAM_TEST_ITERATIONS_PER_SECTOR ? NUM_ERASE_TEST_ITERATIONS_PER_SECTOR : NUM_PROGRAM_TEST_ITERATIONS_PER_SECTOR)

// careful here, the linker won't be able to warn if the bss is too large
// EWRAM_DATA can't be used here, as the linker will shove EWRAM_DATA into IWRAM regardless
EWRAM_BSS static u32 SectorTestBuffer[MAX_TEST_ITERATIONS_PER_SECTOR];

IWRAM_DATA static struct FlashTiming FlashTiming;

IWRAM_CODE static void SetupTimer(void)
{
	REG_TM0CNT_H = 0;
	REG_TM0CNT_L = 0;

	REG_TM1CNT_H = 0;
	REG_TM1CNT_L = 0;

	REG_TM1CNT_H = TIMER_COUNT | TIMER_START;
	REG_IME = 0;
}

IWRAM_CODE static FORCE_INLINE void StartTimer(void)
{
	REG_TM0CNT_H = TIMER_START;
}

IWRAM_CODE static FORCE_INLINE void EndTimer(void)
{
	REG_TM0CNT_H = 0;
	REG_IME = 1;
}

IWRAM_CODE static FORCE_INLINE u32 GetTimerResult(void)
{
	return ((u32)REG_TM1CNT_L << 16) | REG_TM0CNT_L;
}

IWRAM_CODE static void SwitchFlashBank(u8 bankNum)
{
	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0xB0);
	FLASH_WRITE(0x0000, bankNum);
}

IWRAM_CODE static bool EraseSector(u16 sectorNum)
{
	vu8* addr = FLASH_BASE + (sectorNum << FLASH_SECTOR_SHIFT);

	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0x80);
	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);

	*addr = 0x30;

	while (true)
	{
		u8 status = *addr;
		if (status == 0xFF)
		{
			return true;
		}

		if (status & 0x20)
		{
			FLASH_WRITE(0x5555, 0xF0);
			return false;
		}
	}
}

IWRAM_CODE static NO_INLINE bool EraseSectorTimed(u16 sectorNum)
{
	SetupTimer();
	vu8* addr = FLASH_BASE + (sectorNum << FLASH_SECTOR_SHIFT);

	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0x80);
	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);

	StartTimer();
	*addr = 0x30;

	while (true)
	{
		u8 status = *addr;
		if (status == 0xFF)
		{
			EndTimer();
			return true;
		}

		if (status & 0x20)
		{
			EndTimer();
			FLASH_WRITE(0x5555, 0xF0);
			return false;
		}
	}
}

IWRAM_CODE static bool ProgramByte(u16 sectorNum, u16 byteOffset, u8 val)
{
	vu8* addr = FLASH_BASE + (sectorNum << FLASH_SECTOR_SHIFT) + byteOffset;

	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0xA0);

	*addr = val;

	while (true)
	{
		u8 status = *addr;
		if (status == val)
		{
			return true;
		}

		if (status & 0x20)
		{
			FLASH_WRITE(0x5555, 0xF0);
			return false;
		}
	}
}

IWRAM_CODE static NO_INLINE bool ProgramByteTimed(u16 sectorNum, u16 byteOffset, u8 val)
{
	SetupTimer();
	vu8* addr = FLASH_BASE + (sectorNum << FLASH_SECTOR_SHIFT) + byteOffset;

	FLASH_WRITE(0x5555, 0xAA);
	FLASH_WRITE(0x2AAA, 0x55);
	FLASH_WRITE(0x5555, 0xA0);

	StartTimer();
	*addr = val;

	while (true)
	{
		u8 status = *addr;
		if (status == val)
		{
			EndTimer();
			return true;
		}

		if (status & 0x20)
		{
			EndTimer();
			FLASH_WRITE(0x5555, 0xF0);
			return false;
		}
	}
}

IWRAM_CODE static void ZeroSector(u16 sectorNum)
{
	while (true)
	{
		for (u16 i = 0; i < FLASH_BYTES_PER_SECTOR; i++)
		{
			while (!ProgramByte(sectorNum, i, 0))
			{
				// wait until byte is successfully programmed
			}
		}

		// make sure sector is actually 0'd out

		u8 check = 0;
		for (u16 i = 0; i < FLASH_BYTES_PER_SECTOR; i++)
		{
			check |= FLASH_READ((sectorNum << FLASH_SECTOR_SHIFT) + i);
		}

		if (check == 0)
		{
			break;
		}
	}
}

IWRAM_DATA static u8 RandomDataBuffer[FLASH_BYTES_PER_SECTOR];

IWRAM_CODE static void RandomizeSector(u16 sectorNum)
{
	for (u16 i = 0; i < FLASH_BYTES_PER_SECTOR; i++)
	{
		RandomDataBuffer[i] = xoshiro128pp_next();
	}

	while (true)
	{
		for (u16 i = 0; i < FLASH_BYTES_PER_SECTOR; i++)
		{
			while (!ProgramByte(sectorNum, i, RandomDataBuffer[i]))
			{
				// wait until byte is successfully programmed
			}
		}

		// make sure sector was actually randomized

		u16 check = 0;
		for (u16 i = 0; i < FLASH_BYTES_PER_SECTOR; i++)
		{
			if (FLASH_READ((sectorNum << FLASH_SECTOR_SHIFT) + i) == RandomDataBuffer[i])
			{
				check++;
			}
		}

		if (check == FLASH_BYTES_PER_SECTOR)
		{
			break;
		}
	}
}

IWRAM_CODE static void SaveResults(struct FlashSectorTiming* flashSectorTiming, u16 numSuccessfulTests, u16 maxSuccessfulTests)
{
	if (numSuccessfulTests == 0)
	{
		flashSectorTiming->MinCycles = 0;
		flashSectorTiming->MaxCycles = 0;
		flashSectorTiming->MeanCycles = 0;
		flashSectorTiming->NumFailures = maxSuccessfulTests;
		return;
	}

	u64 totalTime = 0;
	flashSectorTiming->MinCycles = UINT32_MAX;
	flashSectorTiming->MaxCycles = 0;

	for (u16 j = 0; j < numSuccessfulTests; j++)
	{
		u32 sectorTestResult = SectorTestBuffer[j];

		if (flashSectorTiming->MinCycles > sectorTestResult)
		{
			flashSectorTiming->MinCycles = sectorTestResult;
		}

		if (flashSectorTiming->MaxCycles < sectorTestResult)
		{
			flashSectorTiming->MaxCycles = sectorTestResult;
		}

		totalTime += sectorTestResult;
	}

	// round mean to the nearest integer
	u32 meanCyclesFixedPoint = totalTime * 10 / numSuccessfulTests;
	u32 meanCyclesInteger = meanCyclesFixedPoint / 10;
	u32 meanCyclesTenths = meanCyclesFixedPoint - (meanCyclesInteger * 10);
	if (meanCyclesTenths >= 5)
	{
		meanCyclesInteger++;
	}

	flashSectorTiming->MeanCycles = meanCyclesInteger;

	flashSectorTiming->NumFailures = maxSuccessfulTests - numSuccessfulTests;
}

// avoid unrolling any loops here

IWRAM_CODE static void PerformFlashTests(void)
{
	u16 sectorTestBufferIndex;
	memset(&FlashTiming, 0, sizeof(FlashTiming));

	// test 1, check erase timings after already erasing sector
	NO_UNROLL for (u8 i = 0; i < FLASH_SECTOR_COUNT; i++)
	{
		sectorTestBufferIndex = 0;

		u8 sectorNum = i;
		if (sectorNum >= FLASH_SECTORS_PER_BANK)
		{
			sectorNum -= FLASH_SECTORS_PER_BANK;
		}

		if (sectorNum == 0)
		{
			SwitchFlashBank(i >= FLASH_SECTORS_PER_BANK ? 1 : 0);
		}

		while (!EraseSector(sectorNum))
		{
			// wait until the sector is successfully erased
		}

		NO_UNROLL for (u16 j = 0; j < NUM_ERASE_TEST_ITERATIONS_PER_SECTOR; j++)
		{
			bool success = EraseSectorTimed(sectorNum);
			if (success)
			{
				SectorTestBuffer[sectorTestBufferIndex++] = GetTimerResult();
			}
		}

		SaveResults(&FlashTiming.EraseAllFFTimings[i], sectorTestBufferIndex, NUM_ERASE_TEST_ITERATIONS_PER_SECTOR);
	}

	printf("Completed Test 1\n");

	// test 2, check erase timings after erasing the sector with 0s
	NO_UNROLL for (u8 i = 0; i < FLASH_SECTOR_COUNT; i++)
	{
		sectorTestBufferIndex = 0;

		u8 sectorNum = i;
		if (sectorNum >= FLASH_SECTORS_PER_BANK)
		{
			sectorNum -= FLASH_SECTORS_PER_BANK;
		}

		if (sectorNum == 0)
		{
			SwitchFlashBank(i >= FLASH_SECTORS_PER_BANK ? 1 : 0);
		}

		NO_UNROLL for (u16 j = 0; j < NUM_ERASE_TEST_ITERATIONS_PER_SECTOR; j++)
		{
			ZeroSector(sectorNum);
			bool success = EraseSectorTimed(sectorNum);
			if (success)
			{
				SectorTestBuffer[sectorTestBufferIndex++] = GetTimerResult();
			}
		}

		SaveResults(&FlashTiming.EraseAll00Timings[i], sectorTestBufferIndex, NUM_ERASE_TEST_ITERATIONS_PER_SECTOR);
	}

	printf("Completed Test 2\n");

	// test 3, check erase timings after erasing the sector with random data
	NO_UNROLL for (u8 i = 0; i < FLASH_SECTOR_COUNT; i++)
	{
		sectorTestBufferIndex = 0;

		u8 sectorNum = i;
		if (sectorNum >= FLASH_SECTORS_PER_BANK)
		{
			sectorNum -= FLASH_SECTORS_PER_BANK;
		}

		if (sectorNum == 0)
		{
			SwitchFlashBank(i >= FLASH_SECTORS_PER_BANK ? 1 : 0);
		}

		NO_UNROLL for (u16 j = 0; j < NUM_ERASE_TEST_ITERATIONS_PER_SECTOR; j++)
		{
			RandomizeSector(sectorNum);
			bool success = EraseSectorTimed(sectorNum);
			if (success)
			{
				SectorTestBuffer[sectorTestBufferIndex++] = GetTimerResult();
			}
		}

		SaveResults(&FlashTiming.EraseAllRandomTimings[i], sectorTestBufferIndex, NUM_ERASE_TEST_ITERATIONS_PER_SECTOR);
	}

	printf("Completed Test 3\n");

	// test 4, check program timings, writing all FFs
	NO_UNROLL for (u8 i = 0; i < FLASH_SECTOR_COUNT; i++)
	{
		sectorTestBufferIndex = 0;

		u8 sectorNum = i;
		if (sectorNum >= FLASH_SECTORS_PER_BANK)
		{
			sectorNum -= FLASH_SECTORS_PER_BANK;
		}

		if (sectorNum == 0)
		{
			SwitchFlashBank(i >= FLASH_SECTORS_PER_BANK ? 1 : 0);
		}

		NO_UNROLL for (u16 j = 0; j < NUM_PROGRAM_TEST_ITERATIONS_PER_BYTE; j++)
		{
			while (!EraseSector(sectorNum))
			{
				// wait until the sector is successfully erased
			}

			NO_UNROLL for (u16 k = 0; k < FLASH_BYTES_PER_SECTOR; k++)
			{
				bool success = ProgramByteTimed(sectorNum, k, 0xFF);
				if (success)
				{
					SectorTestBuffer[sectorTestBufferIndex++] = GetTimerResult();
				}
			}
		}

		SaveResults(&FlashTiming.ProgramFFTimings[i], sectorTestBufferIndex, NUM_PROGRAM_TEST_ITERATIONS_PER_SECTOR);
	}

	printf("Completed Test 4\n");

	// test 5, check program timings going from FF -> 00
	NO_UNROLL for (u8 i = 0; i < FLASH_SECTOR_COUNT; i++)
	{
		sectorTestBufferIndex = 0;

		u8 sectorNum = i;
		if (sectorNum >= FLASH_SECTORS_PER_BANK)
		{
			sectorNum -= FLASH_SECTORS_PER_BANK;
		}

		if (sectorNum == 0)
		{
			SwitchFlashBank(i >= FLASH_SECTORS_PER_BANK ? 1 : 0);
		}

		NO_UNROLL for (u16 j = 0; j < NUM_PROGRAM_TEST_ITERATIONS_PER_BYTE; j++)
		{
			while (!EraseSector(sectorNum))
			{
				// wait until the sector is successfully erased
			}

			NO_UNROLL for (u16 k = 0; k < FLASH_BYTES_PER_SECTOR; k++)
			{
				bool success = ProgramByteTimed(sectorNum, k, 0x00);
				if (success)
				{
					SectorTestBuffer[sectorTestBufferIndex++] = GetTimerResult();
				}
			}
		}

		SaveResults(&FlashTiming.Program00Timings[i], sectorTestBufferIndex, NUM_PROGRAM_TEST_ITERATIONS_PER_SECTOR);
	}

	printf("Completed Test 5\n");

	// test 6, check program timings going from random -> random
	NO_UNROLL for (u8 i = 0; i < FLASH_SECTOR_COUNT; i++)
	{
		sectorTestBufferIndex = 0;

		u8 sectorNum = i;
		if (sectorNum >= FLASH_SECTORS_PER_BANK)
		{
			sectorNum -= FLASH_SECTORS_PER_BANK;
		}

		if (sectorNum == 0)
		{
			SwitchFlashBank(i >= FLASH_SECTORS_PER_BANK ? 1 : 0);
		}

		NO_UNROLL for (u16 j = 0; j < NUM_PROGRAM_TEST_ITERATIONS_PER_BYTE; j++)
		{
			while (!EraseSector(sectorNum))
			{
				// wait until the sector is successfully erased
			}

			NO_UNROLL for (u16 k = 0; k < FLASH_BYTES_PER_SECTOR; k++)
			{
				RandomDataBuffer[k] = xoshiro128pp_next();
			}

			NO_UNROLL for (u16 k = 0; k < FLASH_BYTES_PER_SECTOR; k++)
			{
				u8 randomData = RandomDataBuffer[k];
				bool success = ProgramByteTimed(sectorNum, k, randomData);
				if (success)
				{
					SectorTestBuffer[sectorTestBufferIndex++] = GetTimerResult();
				}
			}
		}

		SaveResults(&FlashTiming.ProgramRandomTimings[i], sectorTestBufferIndex, NUM_PROGRAM_TEST_ITERATIONS_PER_SECTOR);
	}

	printf("Completed Test 6\n");
}

#define REG_WAITCNT (*(vu16 *)(REG_BASE + 0x204))
#define WAITCNT_SRAM_8 (3 << 0)
#define WAITCNT_SRAM_MASK (3 << 0)

IWRAM_DATA static u16 FlashId;

IWRAM_CODE static void ReportFlashType(void)
{
	if (FlashId == 0)
	{
		FlashId = ReadFlashID();
	}

	if (FlashId == 0x09C2)
	{
		printf("Flash Chip: Macronix MX29L010\n");
	}
	else if (FlashId == 0x1362)
	{
		printf("Flash Chip: Sanyo LE26FV10N1TS");
	}
	else
	{
		printf("Unknown Flash Chip: %04X\n", FlashId);
	}
}

IWRAM_CODE static void ReportFlashSectorTiming(struct FlashSectorTiming* flashSectorTiming)
{
	printf("%7d|%7d|%7d|%5d|", flashSectorTiming->MinCycles, flashSectorTiming->MaxCycles, flashSectorTiming->MeanCycles, flashSectorTiming->NumFailures);
}

#define NUM_FLASH_TEST_PAGES (NUM_FLASH_TESTS << 1)

#ifndef MULTIBOOT
// this breaks in multiboot for whatever reason
IWRAM_DATA
#endif
static const char* FlashTestPageInfo[NUM_FLASH_TEST_PAGES] =
{
	"Erase Sectors 00-15 FF->FF",
	"Erase Sectors 16-31 FF->FF",
	"Erase Sectors 00-15 00->FF",
	"Erase Sectors 16-31 00->FF",
	"Erase Sectors 00-15 RD->FF",
	"Erase Sectors 16-31 RD->FF",
	"Program Sectors 00-15 FF",
	"Program Sectors 16-31 FF",
	"Program Sectors 00-15 00",
	"Program Sectors 16-31 00",
	"Program Sectors 00-15 RD",
	"Program Sectors 16-31 RD",
};

#ifndef MULTIBOOT
// for emu autodetection purposes (if gamecode wasn't enough)
#pragma GCC push_options
#pragma GCC optimize("O0")
static __attribute__((aligned(4))) const char* FLASH_LIB_STR = "FLASH1M_V103";
#pragma GCC pop_options
#endif

IWRAM_CODE int main()
{
	irqInit();
	irqEnable(IRQ_VBLANK);

	consoleDemoInit();

	REG_WAITCNT = (REG_WAITCNT & ~WAITCNT_SRAM_MASK) | WAITCNT_SRAM_8;

	while (true)
	{
		ReportFlashType();

		printf("Performing Flash Tests\n(this will take a while)\n");

		PerformFlashTests();

		VBlankIntrWait();
		scanKeys();
		keysDown();
		u8 testPage = 0;
		u8 testPageChanged = true;

		while (true)
		{
			if (testPageChanged)
			{
				printf(CON_CLS());

				ReportFlashType();

				printf("%s\n", FlashTestPageInfo[testPage]);

				printf("Min Cyc|Max Cyc|Avg Cyc|Fails|");

				u8 testType = testPage >> 1;
				struct FlashSectorTiming* flashSectorTimings;
				switch (testType)
				{
					case 0:
						flashSectorTimings = FlashTiming.EraseAllFFTimings;
						break;
					case 1:
						flashSectorTimings = FlashTiming.EraseAll00Timings;
						break;
					case 2:
						flashSectorTimings = FlashTiming.EraseAllRandomTimings;
						break;
					case 3:
						flashSectorTimings = FlashTiming.ProgramFFTimings;
						break;
					case 4:
						flashSectorTimings = FlashTiming.Program00Timings;
						break;
					case 5:
						flashSectorTimings = FlashTiming.ProgramRandomTimings;
						break;
				}

				if (testPage & 1)
				{
					flashSectorTimings = &flashSectorTimings[FLASH_SECTORS_PER_BANK];
				}

				for (u8 i = 0; i < FLASH_SECTORS_PER_BANK; i++)
				{
					ReportFlashSectorTiming(&flashSectorTimings[i]);
				}

				printf("<- Prev|-> Next|ABSS Redo All|");

				testPageChanged = false;
			}

			VBlankIntrWait();

			scanKeys();
			u16 keysPressed = keysDown();

			if (keysPressed & (KEY_RIGHT | KEY_R))
			{
				testPage++;
				if (testPage == NUM_FLASH_TEST_PAGES)
				{
					testPage = 0;
				}

				testPageChanged = true;
			}
			else if (keysPressed & (KEY_LEFT | KEY_L))
			{
				testPage--;
				if (testPage == UINT8_MAX)
				{
					testPage = NUM_FLASH_TEST_PAGES - 1;
				}

				testPageChanged = true;
			}

			if ((~REG_KEYINPUT & (KEY_START | KEY_SELECT | KEY_B | KEY_A)) == (KEY_START | KEY_SELECT | KEY_B | KEY_A))
			{
				break;
			}
		}

		printf(CON_CLS());
	}

}
