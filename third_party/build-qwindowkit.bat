@echo off
rem Build QWindowKit as a static library with MSVC + Ninja.
rem Why not vcvars64.bat: it queries the registry (reg.exe) for the Windows SDK
rem paths, and reg.exe is blocked by the local security policy, so LIB/INCLUDE
rem end up incomplete. We assemble the environment manually instead.
rem Usages: build-qwindowkit.bat Release | Debug
setlocal

if "%~1"=="" goto usage

set "BUILD_TYPE=%~1"
set "MSVC_ROOT=D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"
set "SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
rem CMake treats backslashes in variable values as escapes, so pass forward slashes there
set "SDK_ROOT_FWD=C:/Program Files (x86)/Windows Kits/10"
set "SDK_VER=10.0.26100.0"
set "PROJ_ROOT=D:/VSProject/QTest/third_party"

set "PATH=%MSVC_ROOT%\bin\Hostx64\x64;%SDK_ROOT%\bin\%SDK_VER%\x64;%PATH%"
set "INCLUDE=%MSVC_ROOT%\include;%SDK_ROOT%\Include\%SDK_VER%\ucrt;%SDK_ROOT%\Include\%SDK_VER%\um;%SDK_ROOT%\Include\%SDK_VER%\shared;%SDK_ROOT%\Include\%SDK_VER%\winrt;%SDK_ROOT%\Include\%SDK_VER%\cppwinrt"
set "LIB=%MSVC_ROOT%\lib\x64;%SDK_ROOT%\Lib\%SDK_VER%\ucrt\x64;%SDK_ROOT%\Lib\%SDK_VER%\um\x64"

rem 1. configure
cmake -S "%PROJ_ROOT%/src-qwindowkit" ^
      -B "%PROJ_ROOT%/build-qwindowkit/%BUILD_TYPE%" ^
      -G Ninja ^
      -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
      -DCMAKE_PREFIX_PATH="D:/Qt/6.11.0/msvc2022_64" ^
      -DCMAKE_INSTALL_PREFIX="%PROJ_ROOT%/qwindowkit/%BUILD_TYPE%" ^
      -DCMAKE_RC_COMPILER="%SDK_ROOT_FWD%/bin/%SDK_VER%/x64/rc.exe" ^
      -DCMAKE_MT="%SDK_ROOT_FWD%/bin/%SDK_VER%/x64/mt.exe" ^
      -DQWINDOWKIT_BUILD_STATIC=ON ^
      -DQWINDOWKIT_BUILD_WIDGETS=ON ^
      -DQWINDOWKIT_BUILD_QUICK=OFF ^
      -DQWINDOWKIT_BUILD_EXAMPLES=OFF ^
      -DQWINDOWKIT_BUILD_TESTS=OFF ^
      -DCMAKE_MAKE_PROGRAM="D:/Qt/Tools/Ninja/ninja.exe"
if errorlevel 1 goto failed

rem 2. build
cmake --build "%PROJ_ROOT%/build-qwindowkit/%BUILD_TYPE%"
if errorlevel 1 goto failed

rem 3. install headers + static lib
cmake --install "%PROJ_ROOT%/build-qwindowkit/%BUILD_TYPE%"
if errorlevel 1 goto failed

echo [OK] QWindowKit %BUILD_TYPE% installed to %PROJ_ROOT%/qwindowkit/%BUILD_TYPE%
exit /b 0

:usage
echo Usage: build-qwindowkit.bat Release ^| Debug
exit /b 1

:failed
echo [ERROR] QWindowKit %BUILD_TYPE% build failed
exit /b 1
