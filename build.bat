@echo off
setlocal
set "ROOT=%~dp0"
set "WV=%ROOT%third_party\webview2\build\native"
if not exist "%WV%\include\WebView2.h" (
  echo WebView2 SDK missing in third_party\webview2. See README.md.
  exit /b 1
)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%ROOT%build" mkdir "%ROOT%build"
pushd "%ROOT%build"
rc /nologo /fo app.res "%ROOT%src\app.rc" || (popd & exit /b 1)
cl /nologo /std:c++17 /EHsc /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX ^
  /I"%WV%\include" /I"%ROOT%src" ^
  "%ROOT%src\main.cpp" "%ROOT%src\markdown.cpp" app.res ^
  /Fe:MarkdownViewer.exe ^
  /link /SUBSYSTEM:WINDOWS "%WV%\x64\WebView2LoaderStatic.lib" ^
  user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib shlwapi.lib advapi32.lib version.lib runtimeobject.lib winhttp.lib
set ERR=%ERRORLEVEL%
popd
if %ERR%==0 echo Built build\MarkdownViewer.exe
exit /b %ERR%
