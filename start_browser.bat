@echo off
set "PROFILE_DIR=%LOCALAPPDATA%\YandexDocSession"
set "DOC_URL=%~1"
if "%DOC_URL%"=="" set "DOC_URL=https://disk.yandex.ru/i/CVYHu7Bjcx6xOg"

if exist "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" (
    echo [OK] Starting Microsoft Edge...
    start "" "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --remote-debugging-port=9222 --user-data-dir="%PROFILE_DIR%" "%DOC_URL%"
    goto done
)

if exist "C:\Program Files\Google\Chrome\Application\chrome.exe" (
    echo [OK] Starting Google Chrome...
    start "" "C:\Program Files\Google\Chrome\Application\chrome.exe" --remote-debugging-port=9222 --user-data-dir="%PROFILE_DIR%" "%DOC_URL%"
    goto done
)

echo [ERROR] Browser not found!
:done
echo [OK] Browser started. Now run yandex_doc_writer.exe
