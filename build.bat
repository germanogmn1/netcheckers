@echo off
cl > nul 2>&1
if %ERRORLEVEL% neq 0 call "%VCVARSALL%" x86

rem cl options: https://msdn.microsoft.com/en-us/library/19z1t1wy.aspx

set CompilerOptions=-nologo -Od -Oi -MT -I..\win32_deps\include
set WarningOptions=-W4 -wd4100
set LinkerOptions=-LIBPATH:..\win32_deps\lib -subsystem:console  User32.lib ole32.lib Winmm.lib Imm32.lib OleAut32.lib Version.lib Shell32.lib Gdi32.lib Ws2_32.lib SDL2.lib SDL2main.lib SDL2_image.lib

if not exist build mkdir build
pushd build

cl %CompilerOptions% %WarningOptions% ..\src\netcheckers.c ..\src\network.c -link %LinkerOptions%

copy ..\win32_deps\dlls\*.dll .

popd
