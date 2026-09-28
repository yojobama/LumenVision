<#
.SYNOPSIS
  Thin Windows-side launcher for scripts/install-deps.sh.

.DESCRIPTION
  Runs scripts/install-deps.sh inside WSL or on the Orange Pi over SSH, after checking
  the Windows-side prerequisites.
.PARAMETER Target
  'wsl' (default) runs install-deps.sh inside the WSL 'Ubuntu' distro.
  'pi' runs it on the Orange Pi over SSH (-HostName/-User required, or edit the defaults below).

.PARAMETER Check
  Passes --check through: verify only, install nothing.

.PARAMETER Args
  Any additional arguments are passed through to install-deps.sh verbatim
  (e.g. -Args '--with-webrtc','--with-nt4').

.EXAMPLE
  ./scripts/install-deps.ps1 -Target wsl -Check
  ./scripts/install-deps.ps1 -Target pi -HostName 192.168.55.139 -User photon
#>
param(
    [ValidateSet('wsl', 'pi')]
    [string]$Target = 'wsl',

    [string]$WslDistro = 'Ubuntu',

    [string]$HostName = '192.168.55.139',
    [string]$User = 'photon',

    [switch]$Check,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Args
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$scriptRelPath = 'scripts/install-deps.sh'

function Test-CommandExists([string]$Name) {
    return [bool](Get-Command $Name -ErrorAction SilentlyContinue)
}

# --- checks that only make sense on the Windows side -----------------------
Write-Host "==> Checking Windows-side prerequisites" -ForegroundColor Cyan

if (-not (Test-CommandExists 'swig')) {
    Write-Warning "swig.exe not found on PATH. Server.csproj's pre-build step needs it to " +
        "regenerate the C# bindings (Interop) after any change to LumenCore's public API. " +
        "Install SWIG for Windows and add it to PATH: https://www.swig.org/download.html"
} else {
    Write-Host "    swig: $(Get-Command swig | Select-Object -ExpandProperty Source)"
}

if ($Target -eq 'wsl') {
    if (-not (Test-CommandExists 'wsl')) {
        throw "wsl.exe not found. Install WSL2 first: https://learn.microsoft.com/windows/wsl/install"
    }
    $distros = (wsl -l -q) -replace "`0", ""
    if (-not ($distros -contains $WslDistro)) {
        throw "WSL distro '$WslDistro' not found. Available: $($distros -join ', '). " +
            "Pass -WslDistro to match, or install it with: wsl --install -d Ubuntu-24.04"
    }
    Write-Host "    WSL distro '$WslDistro' found"

    $passthroughArgs = @()
    if ($Check) { $passthroughArgs += '--check' }
    $passthroughArgs += $Args

    Write-Host "==> Running install-deps.sh inside WSL:$WslDistro" -ForegroundColor Cyan
    $linuxPath = "/mnt/" + $repoRoot.Substring(0,1).ToLower() + $repoRoot.Substring(2).Replace('\','/') + "/$scriptRelPath"
    wsl -d $WslDistro -- bash "$linuxPath" @passthroughArgs
}
elseif ($Target -eq 'pi') {
    if (-not (Test-CommandExists 'ssh') -or -not (Test-CommandExists 'scp')) {
        throw "ssh/scp not found on PATH. Enable the Windows OpenSSH client feature or install Git for Windows."
    }

    Write-Host "==> This will run install-deps.sh on $User@$HostName over SSH." -ForegroundColor Yellow
    Write-Host "    Review scripts/install-deps.sh before running against real hardware." -ForegroundColor Yellow

    $passthroughArgs = @()
    if ($Check) { $passthroughArgs += '--check' }
    $passthroughArgs += $Args
    $remoteArgs = ($passthroughArgs -join ' ')

    $remoteDir = "/home/$User/lumenvision-install"
    Write-Host "==> Copying install-deps.sh to $($User)@$($HostName):$remoteDir" -ForegroundColor Cyan
    ssh "$User@$HostName" "mkdir -p $remoteDir"
    scp (Join-Path $repoRoot $scriptRelPath) "${User}@${HostName}:$remoteDir/install-deps.sh"

    Write-Host "==> Running install-deps.sh on the Orange Pi" -ForegroundColor Cyan
    ssh "$User@$HostName" "chmod +x $remoteDir/install-deps.sh && $remoteDir/install-deps.sh $remoteArgs"
}
