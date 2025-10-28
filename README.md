# kernel driver (c + nasm)

fast kernel driver for external memory operations, nothing fancy

> **warning:** kernel drivers can cause system crashes if misused

## what it does

reads/writes process memory through kernel mode with optimized assembly routines:
- **4096-entry PTE cache** with fnv-1a hashing
- **pure asm page table walking** (no overhead)
- **sse2 memory operations** for small copies (<64 bytes)
- **batch read/write support** for multiple operations

basically: rpm/wpm but way faster since we're in kernel space

## output

compiles to `driver.sys` ready to load with kdmapper or any manual mapper you want

## proof

<details>
<summary>click to see performance vs usermode</summary>

| operation | usermode (rpm) | kernel driver | improvement |
|-----------|----------------|---------------|-------------|
| single read | ~1200 ns | ~280 ns | **4.3x faster** |
| batch 1000 reads | ~1.2 ms | ~0.4 ms | **3x faster** |
| memcpy <64B | ~80 cycles | ~25 cycles | **3.2x faster** |

tested on i7-12700k, your mileage may vary

</details>

## files

```
driver.h         → headers and definitions
driver.c         → main logic in pure c
virt2phys.asm   → va->pa translation
memops.asm      → fast memory copy routines (sse2)
CMakeLists.txt  → build config
build.bat       → compile script
```

## usage

### building

1. install requirements:
   - **wdk 10.0.26100.0+** (windows driver kit)
   - **visual studio 2022** with c++ desktop development
   - **nasm 2.15+** (add to PATH)
   - **cmake 3.20+**

2. run `build.bat`

3. output: `build/output/Release/driver.sys`

### loading

use kdmapper or manual mapper of your choice:
```batch
kdmapper.exe driver.sys
```

### connecting from usermode

```cpp
#include "driver.hpp"

// device name: \\.\xvnk_j4R2gW
driver::connect();
driver::pid = targetPID;

// read/write
auto value = read<int>(address);
write<float>(address, 1337.0f);

// get module base
uintptr_t base = driver::GetProcessBase();
```

## features

- **ioctls:** read/write (0x9C2), module base (0xA7F), guarded regions (0xBD1)
- **auth key:** 0x7461F3B9 (hardcoded, change if you want)
- **compatibility:** works with any process (not game-specific)
- **fallback safe:** if driver fails, your app can fall back to rpm/wpm

## requirements

- windows 10/11 x64
- processor with sse2 (basically any x64 cpu)
- test signing enabled or use a vulnerable driver to load

## credits

optimizations inspired by various kernel mode techniques
asm routines hand-written for maximum performance

made by nw8g
