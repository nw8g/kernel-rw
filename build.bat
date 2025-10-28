@echo off
setlocal

where nasm >nul 2>&1
if %errorlevel% neq 0 (
    echo nasm not found, download from https://www.nasm.us/ and add to path
    pause
    exit /b 1
)

if not defined VSINSTALLDIR (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
    if %errorlevel% neq 0 (
        echo Visual Studio 2022 not found! Install with C++ Desktop Development.
        pause
        exit /b 1
    )
)

if not exist build mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64 || (
    echo CMake configuration failed!
    pause
    exit /b 1
)
cmake --build . --config Release || (
    echo Build failed!
    pause
    exit /b 1
)

echo BUILD SUCCESS! driver at build\output\Release\driver.sys
pause

