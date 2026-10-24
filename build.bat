@echo off
setlocal
rem Compila a Aurora com Visual Studio 2022 (ou mais novo) + CMake.
rem Uso: build.bat            (CPU)
rem      build.bat cuda       (whisper acelerado em GPU NVIDIA, requer CUDA Toolkit)

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
  echo [ERRO] Visual Studio nao encontrado. Instale o "Visual Studio Build Tools" com "Desenvolvimento para desktop com C++".
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

where cmake >nul 2>nul || (echo [ERRO] CMake nao encontrado. Instale: winget install Kitware.CMake & exit /b 1)

set EXTRA=
if /i "%1"=="cuda" set EXTRA=-DGGML_CUDA=ON

rem Pasta de build curta evita o limite de 260 caracteres do Windows.
set BUILD=%~d0\aurora-build
where ninja >nul 2>nul && (set GEN=-G Ninja) || (set GEN=)
cmake -S "%~dp0." -B "%BUILD%" %GEN% -DCMAKE_BUILD_TYPE=Release %EXTRA% || exit /b 1
cmake --build "%BUILD%" --config Release || exit /b 1

if not exist "%~dp0bin" mkdir "%~dp0bin"
if exist "%BUILD%\bin\Aurora.exe" copy /y "%BUILD%\bin\Aurora.exe" "%~dp0bin\" >nul
if exist "%BUILD%\bin\Release\Aurora.exe" copy /y "%BUILD%\bin\Release\Aurora.exe" "%~dp0bin\" >nul
echo.
echo Pronto! Executavel em: %~dp0bin\Aurora.exe
