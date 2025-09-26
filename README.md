# GBA Flash Speed Test

This testrom tests the speed of Flash save chips present in various GBA cartridges (including Pokemon games).

The testrom is divided into 6 different tests. 3 test the speed of erasing flash sectors, and 3 test the speed of programming flash sectors. For flash sector erase tests, the differences just come from the data present on the sector before erasing (0xFF, 0x00, or random). For flash sector programming tests, the differences just come from data used to program the sector (0xFF, 0x00, or random). "Random data" is just generated from xoshiro128++. Several iterations are done for each tests (10 per each sector per erase test, 10 per byte per sector per program test).