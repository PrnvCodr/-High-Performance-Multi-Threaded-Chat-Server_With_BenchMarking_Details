@echo off
echo ========================================
echo   Building Chat Server (MinGW-w64)
echo ========================================
echo.

:: Check for g++
where g++ >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: g++ not found.
    echo Please install MinGW-w64 and add it to PATH.
    pause
    exit /b 1
)

echo G++ found. Building...
echo.

:: Create build directory
if not exist build mkdir build

:: Compile server
echo [1/5] Building server.exe...
g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
    -o build/server.exe ^
    server.cpp sockutil.cpp thread_pool.cpp iocp_server.cpp ^
    connection_manager.cpp chat_room.cpp message_store.cpp ^
    -lws2_32 -lmswsock

if %ERRORLEVEL% neq 0 (
    echo ERROR: Server build failed!
    pause
    exit /b 1
)

:: Compile client
echo [2/5] Building client.exe...
g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
    -o build/client.exe ^
    client.cpp sockutil.cpp ^
    -lws2_32

if %ERRORLEVEL% neq 0 (
    echo ERROR: Client build failed!
    pause
    exit /b 1
)

:: Compile unit tests
echo [3/5] Building tests.exe...
g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
    -o build/tests.exe ^
    tests.cpp thread_pool.cpp ^
    -lws2_32

if %ERRORLEVEL% neq 0 (
    echo ERROR: Tests build failed!
    pause
    exit /b 1
)

:: Compile benchmarks
echo [4/5] Building benchmark.exe...
g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
    -o build/benchmark.exe ^
    benchmark.cpp thread_pool.cpp ^
    -lws2_32

if %ERRORLEVEL% neq 0 (
    echo ERROR: Benchmark build failed!
    pause
    exit /b 1
)

:: Compile stress test
echo [5/5] Building stress_test.exe...
g++ -std=c++17 -O2 -Wall -D_WIN32_WINNT=0x0601 ^
    -o build/stress_test.exe ^
    stress_test.cpp sockutil.cpp ^
    -lws2_32

if %ERRORLEVEL% neq 0 (
    echo ERROR: Stress test build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo   Build successful!
echo ========================================
echo.
echo Executables created in build\ directory:
echo   - server.exe       (Chat server)
echo   - client.exe       (Chat client)
echo   - tests.exe        (Unit tests)
echo   - benchmark.exe    (Performance benchmarks)
echo   - stress_test.exe  (Load testing tool)
echo.
echo Quick Start:
echo   1. Run tests:      build\tests.exe
echo   2. Run benchmarks: build\benchmark.exe
echo   3. Start server:   build\server.exe 8080
echo   4. Connect client: build\client.exe 127.0.0.1 8080
echo   5. Load test:      build\stress_test.exe 127.0.0.1 8080 50 100
echo.
pause

