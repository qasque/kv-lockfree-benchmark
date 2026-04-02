@echo off
if defined MINGW_HOME (
  if exist "%MINGW_HOME%\bin\libmcfgthread-2.dll" (
    set "PATH=%MINGW_HOME%\bin;%PATH%"
    echo [bench] MinGW runtime: %MINGW_HOME%\bin
    goto :eof
  )
)

for %%D in (
  "%USERPROFILE%\Desktop\mingw64\bin"
  "%USERPROFILE%\Desktop\mingw32\bin"
  "%USERPROFILE%\scoop\apps\mingw\current\bin"
  "%USERPROFILE%\scoop\apps\llvm\current\bin"
  "C:\msys64\mingw64\bin"
  "C:\msys64\ucrt64\bin"
  "C:\msys64\clang64\bin"
  "C:\ProgramData\mingw64\mingw64\bin"
  "C:\TDM-GCC-64\bin"
) do (
  if exist "%%~D\libmcfgthread-2.dll" (
    set "PATH=%%~D;%PATH%"
    echo [bench] MinGW runtime: %%~D
    goto :eof
  )
)

where gcc >nul 2>&1 && for /f "delims=" %%G in ('where gcc 2^>nul') do (
  if exist "%%~dpGlibmcfgthread-2.dll" (
    set "PATH=%%~dpG;%PATH%"
    echo [bench] MinGW runtime: %%~dpG
    goto :eof
  )
)

echo [bench] libmcfgthread-2.dll not found. Set MINGW_HOME or copy DLL next to bench.exe.
