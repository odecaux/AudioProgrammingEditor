@echo off
call  "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >NUL
set opts=-FC -GR- -EHa- -nologo -Zi /INCREMENTAL
set code=%cd%
pushd build
cl %opts% "%code%\win32_platform.cpp" dxguid.lib user32.lib Winmm.lib Ole32.lib uuid.lib  D2d1.lib Dinput8.lib Dwrite.lib Kernel32.lib /std:c++latest /DEBUG -Feout

popd
