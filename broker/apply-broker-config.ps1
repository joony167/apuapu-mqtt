# 어푸어푸 팀 브로커 설정 적용 스크립트.
#
# 반드시 관리자 권한 PowerShell에서 실행할 것 (PowerShell 우클릭 ->
# "관리자 권한으로 실행"). C:\Program Files에 쓰고, 서비스를 재시작하고,
# 방화벽 규칙을 추가하기 때문이다.
#
#   powershell -ExecutionPolicy Bypass -File .\apply-broker-config.ps1

$ErrorActionPreference = 'Stop'

$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($id)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
    Write-Error "관리자 권한 PowerShell이 필요합니다 (관리자 권한으로 실행)."
}

$target = "C:\Program Files\mosquitto\mosquitto.conf"
$source = Join-Path $PSScriptRoot "mosquitto.conf"

# persistence 파일과 로그가 여기 쌓인다.
$dataDir = "C:\ProgramData\mosquitto"
if (-not (Test-Path $dataDir)) {
    New-Item -ItemType Directory -Path $dataDir | Out-Null
    Write-Host "생성: $dataDir"
}

# 기존 설정을 한 번만 백업해둔다.
$backup = "$target.bak"
if (-not (Test-Path $backup)) {
    Copy-Item $target $backup
    Write-Host "원본 설정 백업: $backup"
}

Copy-Item $source $target -Force
Write-Host "적용: $target"

# 학교/카페 WiFi는 Windows가 보통 "공용(Public)"으로 분류한다. 규칙을 Private
# 프로필에만 걸면 정작 교실에서 아무도 못 붙는다 (보드는 rc=-2로 실패한다).
# 그래서 두 포트만 콕 집어 모든 프로필에 연다. 네트워크 분류 자체를 Private으로
# 바꾸는 것보다 훨씬 좁은 변경이다 - 파일 공유 같은 다른 규칙은 그대로 둔다.
foreach ($port in 1883, 9001) {
    $name = "Mosquitto MQTT $port"
    $rule = Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue
    if (-not $rule) {
        New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow `
            -Protocol TCP -LocalPort $port -Profile Any | Out-Null
        Write-Host "방화벽 규칙 추가: $name (모든 프로필)"
    } else {
        Set-NetFirewallRule -DisplayName $name -Profile Any -Enabled True
        Write-Host "방화벽 규칙 갱신: $name (모든 프로필)"
    }
}

Write-Host "`n현재 네트워크 분류:"
Get-NetConnectionProfile | Select-Object Name, InterfaceAlias, NetworkCategory |
    Format-Table -AutoSize

Restart-Service mosquitto
Start-Sleep -Seconds 2
$svc = Get-Service mosquitto
Get-Service mosquitto | Format-List Name, Status

# 설정이 잘못되면 서비스가 곧바로 죽는데 Restart-Service는 이유를 알려주지
# 않는다. 포그라운드로 직접 띄워서 파싱 에러를 드러낸다.
if ($svc.Status -ne 'Running') {
    Write-Warning "mosquitto가 계속 실행되지 못했습니다 - 아래가 설정 오류입니다:"
    & "C:\Program Files\mosquitto\mosquitto.exe" -c $target -v
    exit 1
}

Write-Host "`n열린 소켓:"
netstat -ano | Select-String ":1883|:9001"

Write-Host "`n팀원에게 알려줄 브로커 주소:"
Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
    Select-Object IPAddress, InterfaceAlias | Format-Table -AutoSize
