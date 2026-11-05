@echo off
rem Manual Debug build of QScreenCut, used only to verify the project compiles and links.
rem The real build is done by Visual Studio.
rem Why manual: MSBuild.exe is blocked by the local security policy.
setlocal

set "MSVC_ROOT=D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDK_VER=10.0.26100.0"
set "QTDIR=D:\Qt\6.11.0\msvc2022_64"
set "PROJ=D:\VSProject\QTest"
set "OUT=%PROJ%\build-verify"

if not exist "%OUT%" mkdir "%OUT%"

set "PATH=%MSVC_ROOT%\bin\Hostx64\x64;%SDK_ROOT%\bin\%SDK_VER%\x64;%QTDIR%\bin;%PATH%"
set "INCLUDE=%MSVC_ROOT%\include;%SDK_ROOT%\Include\%SDK_VER%\ucrt;%SDK_ROOT%\Include\%SDK_VER%\um;%SDK_ROOT%\Include\%SDK_VER%\shared;%SDK_ROOT%\Include\%SDK_VER%\winrt;%SDK_ROOT%\Include\%SDK_VER%\cppwinrt;%QTDIR%\include;%QTDIR%\include\QtCore;%QTDIR%\include\QtGui;%QTDIR%\include\QtWidgets;%QTDIR%\include\QtSql;%QTDIR%\include\QtNetwork;%QTDIR%\mkspecs\win32-msvc"
set "LIB=%MSVC_ROOT%\lib\x64;%SDK_ROOT%\Lib\%SDK_VER%\ucrt\x64;%SDK_ROOT%\Lib\%SDK_VER%\um\x64;%QTDIR%\lib"

rem 1. generate moc sources and qrc source
moc "%PROJ%\QScreenCut.h" -o "%OUT%\moc_QScreenCut.cpp"


moc "%PROJ%\ScreenshotOverlay.h" -o "%OUT%\moc_ScreenshotOverlay.cpp"
if errorlevel 1 goto failed_codegen

moc "%PROJ%\PinnedImageWindow.h" -o "%OUT%\moc_PinnedImageWindow.cpp"
if errorlevel 1 goto failed_codegen

moc "%PROJ%\SettingsWindow.h" -o "%OUT%\moc_SettingsWindow.cpp"
if errorlevel 1 goto failed_codegen

moc "%PROJ%\AiClient.h" -o "%OUT%\moc_AiClient.cpp"
if errorlevel 1 goto failed_codegen

moc "%PROJ%\BaiduTranslateClient.h" -o "%OUT%\moc_BaiduTranslateClient.cpp"
if errorlevel 1 goto failed_codegen

moc "%PROJ%\TextResultWindow.h" -o "%OUT%\moc_TextResultWindow.cpp"
if errorlevel 1 goto failed_codegen

moc "%PROJ%\EntryDetailDialog.h" -o "%OUT%\moc_EntryDetailDialog.cpp"
if errorlevel 1 goto failed_codegen

rcc "%PROJ%\QScreenCut.qrc" -o "%OUT%\qrc_QScreenCut.cpp"
if errorlevel 1 goto failed_codegen

rem 2. compile
pushd "%OUT%"
cl /nologo /c /EHsc /MDd /utf-8 /std:c++20 /Zc:__cplusplus /Zi /W3 ^
   /DQT_WIDGETS_LIB /DQT_GUI_LIB /DQT_CORE_LIB /DQT_SQL_LIB /DQT_NETWORK_LIB /DQWK_CORE_STATIC /DQWK_WIDGETS_STATIC ^
   /I"%OUT%" /I"%PROJ%" /I"%PROJ%\third_party\qwindowkit\Debug\include\QWindowKit" ^
   "%PROJ%\main.cpp" "%PROJ%\QScreenCut.cpp" "%PROJ%\ClipboardStore.cpp" ^
   "%PROJ%\ScreenshotOverlay.cpp" "%PROJ%\PinnedImageWindow.cpp" ^
   "%PROJ%\AppSettings.cpp" "%PROJ%\SettingsWindow.cpp" "%PROJ%\IconUtils.cpp" ^
   "%PROJ%\AiClient.cpp" "%PROJ%\BaiduTranslateClient.cpp" "%PROJ%\TextResultWindow.cpp" ^
   "%PROJ%\ThumbnailUtils.cpp" "%PROJ%\EntryDetailDialog.cpp" ^
   "%PROJ%\AppLogger.cpp" "%PROJ%\HistoryListWidget.cpp" ^
   "%OUT%\moc_QScreenCut.cpp" "%OUT%\moc_ScreenshotOverlay.cpp" ^
   "%OUT%\moc_PinnedImageWindow.cpp" "%OUT%\moc_SettingsWindow.cpp" ^
   "%OUT%\moc_AiClient.cpp" "%OUT%\moc_BaiduTranslateClient.cpp" "%OUT%\moc_TextResultWindow.cpp" "%OUT%\moc_EntryDetailDialog.cpp" "%OUT%\qrc_QScreenCut.cpp"
if errorlevel 1 goto failed_link

rem 3. link
link /nologo /SUBSYSTEM:WINDOWS /OUT:"%OUT%\QScreenCut.exe" ^
   /LIBPATH:"%PROJ%\third_party\qwindowkit\Debug\lib" ^
   main.obj QScreenCut.obj ClipboardStore.obj ScreenshotOverlay.obj PinnedImageWindow.obj ^
   AppSettings.obj SettingsWindow.obj IconUtils.obj AiClient.obj BaiduTranslateClient.obj TextResultWindow.obj ^
   ThumbnailUtils.obj EntryDetailDialog.obj AppLogger.obj HistoryListWidget.obj ^
   moc_QScreenCut.obj moc_ScreenshotOverlay.obj moc_PinnedImageWindow.obj moc_SettingsWindow.obj ^
   moc_AiClient.obj moc_BaiduTranslateClient.obj moc_TextResultWindow.obj moc_EntryDetailDialog.obj qrc_QScreenCut.obj ^
   Qt6EntryPointd.lib Qt6Widgetsd.lib Qt6Guid.lib Qt6Cored.lib Qt6Sqld.lib Qt6Svgd.lib Qt6Networkd.lib ^
   QWKCored.lib QWKWidgetsd.lib ^
   user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib uxtheme.lib dwmapi.lib d3d11.lib dxgi.lib ^
   windowsapp.lib ws2_32.lib crypt32.lib secur32.lib
if errorlevel 1 goto failed_link
popd

echo [OK] QScreenCut built to %OUT%\QScreenCut.exe
exit /b 0

:failed_codegen
echo [FAILED] code generation step failed
exit /b 1

:failed_link
popd
echo [FAILED] compile or link step failed
exit /b 1
