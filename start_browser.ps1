param(
    [string]$Url = "https://disk.yandex.ru/i/CVYHu7Bjcx6xOg"
)

$profileDir = Join-Path $env:LOCALAPPDATA "YandexDocSession"
$edgePath = "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
$chromePath = "C:\Program Files\Google\Chrome\Application\chrome.exe"

$browserPath = $null
if (Test-Path $edgePath) {
    $browserPath = $edgePath
} elseif (Test-Path $chromePath) {
    $browserPath = $chromePath
}

if ($browserPath) {
    Write-Host "[OK] Запуск браузера: $browserPath" -ForegroundColor Green
    Write-Host "[OK] Документ: $Url" -ForegroundColor Cyan
    
    # Запускаем браузер с портом отладки 9222
    Start-Process -FilePath $browserPath -ArgumentList @(
        "--remote-debugging-port=9222",
        "--user-data-dir=$profileDir",
        $Url
    )
    Write-Host "[OK] Браузер успешно запущен. Теперь можно запускать .\yandex_doc_writer.exe" -ForegroundColor Yellow
} else {
    Write-Error "Браузер Microsoft Edge или Chrome не найден!"
}
