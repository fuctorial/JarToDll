@echo off
setlocal

:: =================================================================
:: ==            Build and Package Script for Fuctorize           ==
:: ==                      (Standard Build)                       ==
:: =================================================================

:: --- Configuration ---
set "JDK_DIR=%ProgramFiles%\Java\jdk1.8.0_231"
set "FUCTORIZE_PROJECT_DIR=C:\Fuctorize"
set "MINGW_BIN_DIR=C:\msys64\mingw64\bin"

:: --- Auto-calculated paths ---
set "JAVA_PACKER_EXE=%JDK_DIR%\bin\java.exe"
set "JDK_INCLUDE_DIR=%JDK_DIR%"
set "BUILD_LIBS_DIR=%FUCTORIZE_PROJECT_DIR%\build\libs"
set "MINGW_GXX_EXE=%MINGW_BIN_DIR%\g++.exe"

:: --- Pre-flight Checks ---
echo Verifying tool paths...
if not exist "%JAVA_PACKER_EXE%" (echo ERROR: java.exe not found. Correct JDK_DIR.& goto :error)
if not exist "%MINGW_GXX_EXE%" (echo ERROR: g++.exe not found. Correct MINGW_BIN_DIR.& goto :error)
echo      All tools found.
echo.

:: 1. Copy the latest JAR from build/libs
echo [1/4] Locating and copying built JAR...
set "FOUND_JAR="
for %%f in ("%BUILD_LIBS_DIR%\*.jar") do set "FOUND_JAR=%%~f"
if "%FOUND_JAR%"=="" (echo ERROR: No JAR found in "%BUILD_LIBS_DIR%".& goto :error)
copy /Y "%FOUND_JAR%" "%CD%\Fuctorize-1.0.jar" >nul
echo      Done.
echo.

:: 2. Run JavaDllPacker to generate STANDARD headers
echo [2/4] Packing JAR with JavaDllPacker (Standard)...
if not exist "JavaDllPacker.jar" (echo ERROR: JavaDllPacker.jar not found.& goto :error)
"%JAVA_PACKER_EXE%" -jar JavaDllPacker.jar Fuctorize-1.0.jar
if %errorlevel% neq 0 (echo ERROR: JavaDllPacker failed.& goto :error)
echo      Done. (classes.h and resources.h generated)
echo.

:: 3. Compile the C++ DLL with ENCRYPTION DISABLED
echo [3/4] Compiling fuctorize.dll (without decryption)...
set "PATH=%MINGW_BIN_DIR%;%PATH%"
:: <<<--- Здесь флаг -DENCRYPT_ENABLED ОТСУТСТВУЕТ ---
"%MINGW_GXX_EXE%" -shared -o fuctorize.dll dllmain.cpp utils.cpp -I"%JDK_INCLUDE_DIR%\include" -I"%JDK_INCLUDE_DIR%\include\win32" -Wl,--subsystem,windows -Wl,--export-all-symbols
if %errorlevel% neq 0 (echo ERROR: g++ compilation failed.& goto :error)
echo      Done.
echo.

:: 4. Final summary
echo [4/4] Finalizing...
echo ===========================================
echo  SUCCESS! Standard build completed.
echo  The final compiled DLL is ready:
echo  - fuctorize.dll
echo ===========================================
goto :eof

:error
echo.
echo ===========================================
echo  BUILD FAILED! Please check the error messages above.
echo ===========================================
pause
endlocal