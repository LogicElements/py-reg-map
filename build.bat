@echo off
setlocal

rem Run from the repository root regardless of where the script was called from
cd /d "%~dp0"

set "VENV_PYTHON=%~dp0.venv\Scripts\python.exe"

rem Publishing is opt-in: "build.bat" only builds, "build.bat upload" also publishes.
set "PUBLISH="
if /i "%~1"=="upload" set "PUBLISH=1"

if not exist "%VENV_PYTHON%" (
    echo ERROR: No virtual environment found at "%VENV_PYTHON%".
    echo Create it first:
    echo     py -3.14 -m venv .venv
    echo     .venv\Scripts\python -m pip install -e ".[dev]" build twine
    exit /b 1
)

"%VENV_PYTHON%" -c "import build" 2>nul
if errorlevel 1 (
    echo ERROR: build is missing in the virtual environment.
    echo     .venv\Scripts\python -m pip install build
    exit /b 1
)

if defined PUBLISH (
    "%VENV_PYTHON%" -c "import twine" 2>nul
    if errorlevel 1 (
        echo ERROR: twine is missing in the virtual environment.
        echo     .venv\Scripts\python -m pip install twine
        exit /b 1
    )
)

echo === Building with "%VENV_PYTHON%"

echo === Run the tests
"%VENV_PYTHON%" -m pytest -q
if errorlevel 1 (
    echo ERROR: Tests failed, nothing was built.
    exit /b 1
)

echo === Delete the build directories and stale package metadata
if exist "dist" rmdir /s /q "dist"
if exist "build" rmdir /s /q "build"
for /d %%D in ("src\*.egg-info") do rmdir /s /q "%%D"

echo === Build Python package
"%VENV_PYTHON%" -m build
if errorlevel 1 (
    echo ERROR: Build failed, nothing was uploaded.
    exit /b 1
)

if not defined PUBLISH (
    echo.
    echo === Done. The packages are in dist\
    echo === Run "build.bat upload" to publish them.
    exit /b 0
)

echo === Upload python package to PIP
"%VENV_PYTHON%" -m twine upload dist/*
