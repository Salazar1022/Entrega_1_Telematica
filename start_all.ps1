# start_all.ps1 - Levanta todos los servicios del proyecto CyberGame
# USO: .\start_all.ps1  (desde la raiz de Entrega_1_Telematica)

$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$serverDir  = Join-Path $root "server"
$httpDir    = Join-Path $root "cliente_web_y_juego\servidor_http"
$identityDir = Join-Path $root "Protocol_ServiceID"
$identityPy = Join-Path $identityDir "identity_server.py"
$serverBin  = Join-Path $serverDir "server.exe"

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  CyberOps - Iniciando servicios..." -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# 1. Servicio de identidad
Write-Host "[1/3] Iniciando servicio de Identidad (puerto 9090)..." -ForegroundColor Yellow
$cmd1 = "Set-Location '$identityDir'; Write-Host 'IDENTIDAD (Protocol_ServiceID) iniciando...' -ForegroundColor Cyan; python '$identityPy' 9090 identity.log"
Start-Process powershell -ArgumentList "-NoExit", "-Command", $cmd1

Start-Sleep -Milliseconds 800

# 2. Servidor CGSP (C)
Write-Host "[2/3] Iniciando servidor CGSP en C (puerto 8081)..." -ForegroundColor Yellow

if (Test-Path $serverBin) {
    $cmd2 = "Set-Location '$serverDir'; `$env:IDENTITY_HOST='localhost'; `$env:IDENTITY_PORT='9090'; Write-Host 'CGSP SERVER iniciando...' -ForegroundColor Green; .\server 8081 server.log"
    Start-Process powershell -ArgumentList "-NoExit", "-Command", $cmd2
} else {
    Write-Host "      Intentando via WSL..." -ForegroundColor DarkGray
    $wslHostIp = wsl bash -c "ip route show default | grep -oE '\b([0-9]{1,3}\.){3}[0-9]{1,3}\b'"
    $wslHostIp = $wslHostIp.Trim()
    if (-not $wslHostIp) { $wslHostIp = "localhost" }
    $cmd2 = "Set-Location '$serverDir'; `$env:WSLENV='IDENTITY_HOST:IDENTITY_PORT'; `$env:IDENTITY_HOST='$wslHostIp'; `$env:IDENTITY_PORT='9090'; Write-Host 'Compilando y ejecutando via WSL...' -ForegroundColor Green; wsl make run"
    Start-Process powershell -ArgumentList "-NoExit", "-Command", $cmd2
}

Start-Sleep -Milliseconds 1200

# 3. Servidor HTTP / Lobby
Write-Host "[3/3] Iniciando servidor HTTP (puerto 8080)..." -ForegroundColor Yellow
$httpPy = Join-Path $httpDir "server_http.py"
$cmd3 = "Set-Location '$httpDir'; `$env:GAME_HOST='localhost'; `$env:GAME_PORT='8081'; Write-Host 'HTTP SERVER en http://127.0.0.1:8080' -ForegroundColor Magenta; python '$httpPy'"
Start-Process powershell -ArgumentList "-NoExit", "-Command", $cmd3

Start-Sleep -Milliseconds 1500

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Todos los servicios iniciados!" -ForegroundColor Green
Write-Host ""
Write-Host "  Web:       http://127.0.0.1:8080" -ForegroundColor White
Write-Host "  CGSP:      localhost:8081" -ForegroundColor White
Write-Host "  Identidad: localhost:9090" -ForegroundColor White
Write-Host ""
Write-Host "  Usuarios de prueba:" -ForegroundColor Yellow
Write-Host "    atacante1 / pass123  (ATTACKER)" -ForegroundColor White
Write-Host "    hacker    / hack2026 (ATTACKER)" -ForegroundColor White
Write-Host "    defensor1 / pass123  (DEFENDER)" -ForegroundColor White
Write-Host "    seguridad / seg2026  (DEFENDER)" -ForegroundColor White
Write-Host ""
Write-Host "  Presiona Enter para abrir el navegador..."
Read-Host | Out-Null
Start-Process "http://127.0.0.1:8080"
