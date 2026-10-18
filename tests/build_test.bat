@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0..\build"
cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DNOMINMAX "%~dp0render_cli.cpp" "%~dp0..\src\markdown.cpp" /Fe:render_cli.exe
popd
