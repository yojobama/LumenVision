<#
.SYNOPSIS
  Builds and deploys LumenVision to the Orange Pi.

.DESCRIPTION
  Run from Windows after libLumenCore.so has been built for ARM64 on the Pi (Visual Studio
  Remote_GCC build). This script:
    1. Publishes Server self-contained for linux-arm64 (so the Pi needs no .NET install at all)
    2. Builds the WebUI (webui) and folds it into the publish output's wwwroot
    3. Copies both, plus the already-built libLumenCore.so, to /opt/lumenvision on the Pi
    4. Installs/refreshes the systemd unit and restarts the service

.PARAMETER PiHost
  Orange Pi hostname or IP.
.PARAMETER PiUser
  SSH user on the Pi.
.PARAMETER RemoteRootDir
  Directory on the Pi that holds the remote build (LumenCore/bin/ARM64/<config>/libLumenCore.so).
.PARAMETER SkipWebUI
  Skip building/deploying the WebUI (useful when only the server changed).
.PARAMETER PurgeOldInstall
  Archive an existing /opt/lumenvision to /opt/lumenvision.bak-<date> before deploying. By
  default a deploy only overwrites the published output, keeping calibrations.json,
  stereoCalibrations.json and data.json.

.EXAMPLE
  ./scripts/deploy.ps1 -PiHost 192.168.55.139 -PiUser photon
#>
param(
    [string]$PiHost = "192.168.55.139",
    [string]$PiUser = "photon",
    [string]$RemoteRootDir = "/home/photon/lumenvision-remote-build",
    [switch]$SkipWebUI,
    [switch]$PurgeOldInstall,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$remoteDeployDir = "/opt/lumenvision"
$publishDir = Join-Path $repoRoot "Server/bin/$Configuration/net10.0/linux-arm64/publish"

function Invoke-Remote([string]$Command) {
    ssh "$PiUser@$PiHost" $Command
}

Write-Host "==> Publishing Server (self-contained, linux-arm64, $Configuration)" -ForegroundColor Cyan
dotnet publish (Join-Path $repoRoot "Server/Server.csproj") `
    -c $Configuration -r linux-arm64 --self-contained true `
    -p:LumenCorePlatform=ARM64 -p:LumenCoreConfiguration=$Configuration
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed" }

if (-not $SkipWebUI) {
    $webUiDir = Join-Path $repoRoot "webui"
    if (Test-Path $webUiDir) {
        Write-Host "==> Building WebUI" -ForegroundColor Cyan
        Push-Location $webUiDir
        try {
            npm run build
            if ($LASTEXITCODE -ne 0) { throw "npm run build failed" }
            $wwwroot = Join-Path $publishDir "wwwroot"
            New-Item -ItemType Directory -Force -Path $wwwroot | Out-Null
            Copy-Item -Recurse -Force (Join-Path $webUiDir "dist\*") $wwwroot
        } finally {
            Pop-Location
        }
    } else {
        Write-Warning "webui not found, skipping WebUI build"
    }
}

Write-Host "==> Locating the Visual Studio remote build's libLumenCore.so on the Pi" -ForegroundColor Cyan
$remoteSoPath = "$RemoteRootDir/LumenCore/bin/ARM64/$Configuration/libLumenCore.so"
$soCheck = ssh "$PiUser@$PiHost" "test -f '$remoteSoPath' && echo FOUND || echo MISSING"
if ($soCheck.Trim() -ne "FOUND") {
    throw "libLumenCore.so not found at $remoteSoPath on the Pi - build the ARM64/$Configuration configuration in Visual Studio first (Remote_GCC toolset), or pass -RemoteRootDir matching Local.props"
}

if ($PurgeOldInstall) {
    Write-Host "==> Archiving existing $remoteDeployDir" -ForegroundColor Cyan
    Invoke-Remote "test -d $remoteDeployDir && sudo mv $remoteDeployDir ${remoteDeployDir}.bak-`$(date +%F) || true"
}

Write-Host "==> Copying published server to $PiUser@$PiHost`:$remoteDeployDir" -ForegroundColor Cyan
Invoke-Remote "sudo mkdir -p $remoteDeployDir && sudo chown ${PiUser}:${PiUser} $remoteDeployDir"
scp -r "$publishDir/*" "${PiUser}@${PiHost}:$remoteDeployDir/"
Invoke-Remote "cp '$remoteSoPath' $remoteDeployDir/libLumenCore.so && chmod +x $remoteDeployDir/Server"

Write-Host "==> Installing systemd unit" -ForegroundColor Cyan
$serviceFile = Join-Path $repoRoot "scripts/lumenvision.service"
scp $serviceFile "${PiUser}@${PiHost}:/tmp/lumenvision.service"
Invoke-Remote "sudo id lumen >/dev/null 2>&1 || sudo useradd --system --no-create-home --shell /usr/sbin/nologin lumen; sudo usermod -aG video,render lumen; sudo chown -R lumen:lumen $remoteDeployDir; sudo mv /tmp/lumenvision.service /etc/systemd/system/lumenvision.service; sudo systemctl daemon-reload; sudo systemctl enable lumenvision; sudo systemctl restart lumenvision"

Write-Host "==> Deployed. Status:" -ForegroundColor Green
Invoke-Remote "sudo systemctl status lumenvision --no-pager -l | head -15"
