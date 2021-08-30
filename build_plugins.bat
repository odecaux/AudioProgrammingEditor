@echo off


call  "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" >NUL
set opts=-FC -GR- -EHa- -nologo -Zi /LD /std:c++latest /O2
set code=%cd%\plugins

pushd build

for /F %%i in ('dir /B /D %code%') do (
cl %opts% %code%\%%i  
)

popd