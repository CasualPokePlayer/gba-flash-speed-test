# GBA Flash Speed Test

This testrom tests the speed of Flash save chips present in various GBA cartridges (including Pokemon games).


The testrom is divided into 6 different tests. 3 test the speed of erasing flash sectors, and 3 test the speed of programming flash sectors. For flash sector erase tests, the differences just come from the data present on the sector before erasing (0xFF, 0x00, or random). For flash sector programming tests, the differences just come from data used to program the sector (0xFF, 0x00, or random). "Random data" is just generated from xoshiro128++. Several iterations are done for each tests (10 per each sector per erase test, 10 per byte per sector per program test).

These are example results from my own Emerald cartridge. Note that Flash is not deterministic, so results will not strictly be the same across different runs (in my case, program timings appear to be consistent, while erase timings can wildly vary).

<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/dfeec127-64a3-4e63-95be-c1c6979f829f" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/a0f8eb30-b36c-40e0-90ac-84d001998ff3" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/2f6d5209-c10e-4462-87ed-6f11bfefda1b" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/78e323c6-8394-4ff9-a134-6633fd7403fd" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/366d16fa-c95a-4456-b8f7-8649a4899695" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/7fab77c9-3b1d-4940-bed0-b10224cf74a7" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/61473ff1-8c7b-4ba9-ac17-13c6c967ea9d" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/241976f2-46a4-4ecc-9cd1-b35d0fc6fe8a" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/d5ef5047-a45c-426a-b1c0-8488558b632c" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/ddb77364-e031-4ba5-a02e-4f686be6fca4" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/fd9e8408-ccf5-44a9-9345-8f7b2dd6b8fc" />
<img width="240" height="160" alt="image" src="https://github.com/user-attachments/assets/03b7489a-9add-45a0-801f-eb85c035c9ca" />
