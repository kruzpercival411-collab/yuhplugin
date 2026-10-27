@echo off
cd /d "%~dp0"
where cmake >nul 2>nul || (echo Install CMake and Visual Studio 2022 C++ workload first.&pause&exit /b 1)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 goto fail
cmake --build build --config Release --target SampleFlip_VST3
if errorlevel 1 goto fail
echo.
echo Built VST3: build\SampleFlip_artefacts\Release\VST3\Sample Flip.vst3
pause
exit /b 0
:fail
echo Build failed. See errors above.
pause
exit /b 1
