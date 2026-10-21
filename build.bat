@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo vcvars64 failed & exit /b 1)

cd /d "%~dp0"
if not exist build mkdir build
if not exist out mkdir out

set MH=..\minhook-master
set INC=/I "%MH%\include" /I "%MH%\src" /I "%MH%\src\hde"

echo === payload (managed, .NET Framework 4.x) ===
if not exist build\0Harmony.dll (
    echo   [!] build\0Harmony.dll missing - copy Lib.Harmony net472 0Harmony.dll there
    exit /b 1
)
"%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /target:library ^
    /out:build\payload.dll /r:build\0Harmony.dll src\payload.cs
if errorlevel 1 exit /b 1

echo === compiling proxy.c ===
cl /nologo /c /O2 /GS- /DNDEBUG /MT /W3 %INC% /Fo:build\proxy.obj src\proxy.c
if errorlevel 1 exit /b 1

echo === compiling clrhost.cpp ===
cl /nologo /c /O2 /DNDEBUG /MT /EHsc /W3 /Fo:build\clrhost.obj src\clrhost.cpp
if errorlevel 1 exit /b 1

echo === compiling minhook ===
cl /nologo /c /O2 /DNDEBUG /MT /W3 %INC% /Fo:build\ /Tp "%MH%\src\hde\hde64.c"
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /DNDEBUG /MT /W3 %INC% /Fo:build\ /Tp "%MH%\src\buffer.c"
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /DNDEBUG /MT /W3 %INC% /Fo:build\ /Tp "%MH%\src\hook.c"
if errorlevel 1 exit /b 1
cl /nologo /c /O2 /DNDEBUG /MT /W3 %INC% /Fo:build\ /Tp "%MH%\src\trampoline.c"
if errorlevel 1 exit /b 1

echo === assembling stubs ===
ml64 /nologo /c /Fo build\stubs.obj gen\stubs.asm
if errorlevel 1 exit /b 1

echo === embedding payload resources ===
pushd src
rc /nologo /fo ..\build\embed.res embed.rc
popd
if errorlevel 1 exit /b 1

echo === linking ===
link /nologo /DLL /MACHINE:X64 /DEF:gen\proxy.def /OUT:out\dwmapi.dll ^
  build\proxy.obj build\clrhost.obj build\hde64.obj build\buffer.obj build\hook.obj build\trampoline.obj build\stubs.obj ^
  build\embed.res kernel32.lib user32.lib advapi32.lib uuid.lib ole32.lib
if errorlevel 1 exit /b 1

echo.
echo === built ===
dir out\dwmapi.dll
endlocal
