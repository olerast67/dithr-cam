# Запуск idf.py с окружением ESP-IDF 6.1 из C:\Espressif.
#   .\idf.ps1 build
#   .\idf.ps1 -p COM5 flash monitor
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v6.1"
$env:IDF_TOOLS_PATH = "C:\Espressif\tools"
. "$env:IDF_PATH\export.ps1" *> $null
Set-Location $PSScriptRoot
idf.py @args
exit $LASTEXITCODE
