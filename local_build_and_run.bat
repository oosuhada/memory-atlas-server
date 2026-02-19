@echo off
setlocal enabledelayedexpansion

REM --- Visual Studio Build Environment Configuration ---
REM Attempt to find vcvarsall.bat automatically for VS 2022.
set "VCVARSALL_PATH="
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL_PATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" set "VCVARSALL_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"

if not defined VCVARSALL_PATH (
    echo ERROR: Could not automatically find vcvarsall.bat for VS 2022.
    echo Please manually set the VCVARSALL_PATH variable in the script.
    REM Fallback to a default path if automatic detection fails.
    set "VCVARSALL_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    echo Using default path as fallback: %VCVARSALL_PATH%
    REM Consider uncommenting pause/exit if manual setting is required.
    REM pause
    REM exit /b 1
) else (
    echo Found vcvarsall.bat at: %VCVARSALL_PATH%
)

set "VC_TARGET_ARCH=x64" REM Target architecture (e.g., x64, x86)

REM --- Build Configuration ---
set "BUILD_TYPE=Release" REM Build type (e.g., Release, Debug)

echo Using build type: %BUILD_TYPE%

REM Setup Visual Studio Environment
call "%VCVARSALL_PATH%" %VC_TARGET_ARCH%
if %errorlevel% neq 0 (
    echo ERROR: Failed to set up Visual Studio environment using vcvarsall.bat.
    pause
    exit /b %errorlevel%
)
echo Visual Studio environment set up successfully.

REM --- Environment Variables --- 
set "ENV_FILE=%~dp0.env.debug" REM Path to the environment file
if not exist "%ENV_FILE%" (
    echo ERROR: .env.debug file not found! Please create it for local debugging.
    pause
    exit /b 1
)
echo Using environment file: %ENV_FILE%

REM Load GOOGLE_MAPS_API_KEY from .env file
REM This loop specifically looks for GOOGLE_MAPS_API_KEY.
set "API_KEY_FOUND="
for /f "usebackq tokens=1,* delims==" %%a in ("%ENV_FILE%") do (
    set "key=%%a"
    set "key=!key: =!" 
    if /i "!key!"=="GOOGLE_MAPS_API_KEY" (
        set "value=%%b"
        REM Trim leading space from value if present
        if defined value if "!value:~0,1!"==" " set "value=!value:~1!" 
        set "GOOGLE_MAPS_API_KEY=!value!" 
        set "API_KEY_FOUND=true"
        echo GOOGLE_MAPS_API_KEY loaded.
        goto :ApiKeySet
    )
)

:ApiKeySet
if not defined API_KEY_FOUND (
    echo ERROR: GOOGLE_MAPS_API_KEY not found or empty in %ENV_FILE%!
    pause
    exit /b 1
)

REM --- CMake and Build Paths --- 
set "SOURCE_DIR=%~dp0"
REM Remove trailing backslash if present
if "%SOURCE_DIR:~-1%"=="\" set "SOURCE_DIR=%SOURCE_DIR:~0,-1%" 

set "BUILD_DIR=%SOURCE_DIR%\out\build\x64-%BUILD_TYPE%" REM Build output directory
set "VCPKG_TOOLCHAIN_FILE=%SOURCE_DIR%\vcpkg\scripts\buildsystems\vcpkg.cmake" REM vcpkg toolchain file
set "CMAKE_GENERATOR=Ninja" REM CMake generator to use
set "EXECUTABLE_NAME=CherryRecorder-Server-App.exe" REM Name of the final executable for Windows
set "EXECUTABLE_PATH=%BUILD_DIR%\%EXECUTABLE_NAME%" REM Full path to the executable

echo Cleaning up previous build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
)

echo Configuring CMake...
echo   Source Dir: %SOURCE_DIR%
echo   Build Dir:  %BUILD_DIR%
echo   Generator:  %CMAKE_GENERATOR%
echo   Toolchain:  %VCPKG_TOOLCHAIN_FILE%
echo   Build Type: %BUILD_TYPE%

mkdir "%BUILD_DIR%" 2> nul REM Create build directory silently

REM --- Configure Project with CMake ---
cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR%" -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN_FILE%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if %errorlevel% neq 0 (
    echo CMake configuration failed!
    pause
    exit /b %errorlevel%
)
echo CMake configuration successful.

REM --- Build Project with CMake ---
echo Building the server (%BUILD_TYPE%)...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE%
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b %errorlevel%
)
echo Build successful.

REM --- Run Server Application ---
echo Starting CherryRecorder Server...

if not exist "%EXECUTABLE_PATH%" (
    echo ERROR: Executable not found at %EXECUTABLE_PATH%!
    pause
    exit /b 1
)

REM --- HTTPS Configuration ---
set "CERT_PATH=%SOURCE_DIR%\cert.pem"
set "KEY_PATH=%SOURCE_DIR%\key.pem"
set "HTTPS_ARGS="

if exist "%CERT_PATH%" (
    if exist "%KEY_PATH%" (
        echo Found cert.pem and key.pem. Enabling HTTPS.
        set "HTTPS_ARGS=--cert_path=\"%CERT_PATH%\" --key_path=\"%KEY_PATH%\""
    ) else (
        echo Found cert.pem but key.pem is missing. HTTPS will be disabled.
    )
) else (
    echo Certificate files not found. HTTPS will be disabled.
)

REM Execute the built server application with or without HTTPS arguments
echo Running command: "%EXECUTABLE_PATH%" %HTTPS_ARGS%
"%EXECUTABLE_PATH%" %HTTPS_ARGS%

echo Server exited. Press any key to close window.
pause

endlocal 