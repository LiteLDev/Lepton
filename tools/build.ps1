# Builds Lepton for the server (BDS), the client, or both.
#
#   pwsh tools/build.ps1                    # both platforms, release
#   pwsh tools/build.ps1 -Target server     # one platform
#   pwsh tools/build.ps1 -Tests             # also build and run the unit tests (lp-tests)
#
# Each platform has its own build dir (build/<target>); outputs land in
#   bin/<target>/Lepton/  (dll, pdb, manifest.json, lang/, behavior_packs/, resource_packs/)
# Copy that folder to `plugins/` (BDS) or `mods/` (LeviLauncher client).
#
# Builds of every worktree on this machine share a few build slots, so parallel work does not
# exhaust CPU and memory: a build waits for a free slot (`$env:LP_BUILD_SLOTS`, default 2) and
# compiles with `-Jobs` jobs (`$env:LP_BUILD_JOBS`, default half the logical processors).
param(
    [ValidateSet('server', 'client', 'all')][string]$Target = 'all',
    [ValidateSet('release', 'debug')][string]$Mode = 'release',
    [switch]$Tests,
    [int]$Jobs = $(if ($env:LP_BUILD_JOBS) { [int]$env:LP_BUILD_JOBS } else { [Math]::Max(2, [int]([Environment]::ProcessorCount / 2)) })
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/..").Path

# Machine-wide build slots: an exclusively opened lock file per slot, released by the OS when the
# process exits (so a killed build never leaves a stale lock).
$slotCount = if ($env:LP_BUILD_SLOTS) { [Math]::Max(1, [int]$env:LP_BUILD_SLOTS) } else { 2 }
$slotDir = Join-Path $env:LOCALAPPDATA 'lp-build'
New-Item -ItemType Directory -Force $slotDir | Out-Null
$slotLock = $null
$waitStart = Get-Date
while (-not $slotLock) {
    for ($i = 1; $i -le $slotCount -and -not $slotLock; $i++) {
        try {
            $slotLock = [IO.File]::Open((Join-Path $slotDir "slot$i.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
            Write-Host "==> build slot $i/$slotCount ($Jobs jobs)"
        } catch [IO.IOException] {}
    }
    if (-not $slotLock) {
        $waited = [int]((Get-Date) - $waitStart).TotalSeconds
        if ($waited % 60 -lt 5) { Write-Host "==> waiting for a free build slot ($waited s)" }
        Start-Sleep -Seconds 5
    }
}

Push-Location $root
try {
    # Fresh shells may not have the tools on PATH yet. Append (never prepend) the registry PATH so
    # the caller's PATH wins: on CI, setup-llvm puts LLVM 22 on PATH, and the runner's preinstalled
    # C:\Program Files\LLVM (listed in the machine PATH) must not shadow it.
    $env:Path = $env:Path + ';' + [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($env:LLVM_PATH -and (Test-Path (Join-Path $env:LLVM_PATH 'bin\clang-cl.exe'))) {
        $env:Path = (Join-Path $env:LLVM_PATH 'bin') + ";$env:Path"
    }
    if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
        foreach ($dir in "$env:ProgramFiles\LLVM\bin", "${env:ProgramFiles(x86)}\LLVM\bin") {
            if (Test-Path (Join-Path $dir 'clang-cl.exe')) { $env:Path = "$dir;$env:Path"; break }
        }
    }
    if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { throw 'xmake not found on PATH' }
    $clang = Get-Command clang-cl -ErrorAction SilentlyContinue
    if ($clang) { Write-Host "==> using $($clang.Source) ($((& $clang.Source --version | Select-Object -First 1)))" }
    git config --global core.longpaths true | Out-Null

    # xmake needs to be told about Visual Studio 18 (2026) explicitly.
    $vsArgs = @()
    foreach ($pf in $env:ProgramFiles, ${env:ProgramFiles(x86)}) {
        if ($pf -and (Test-Path (Join-Path $pf 'Microsoft Visual Studio\18'))) { $vsArgs = @('--vs=2026'); break }
    }

    $targets = if ($Target -eq 'all') { @('server', 'client') } else { @($Target) }
    foreach ($t in $targets) {
        Write-Host "==> configuring $t ($Mode)"
        xmake f -P $root -a x64 -m $Mode -p windows --target_type=$t -o "build/$t" @vsArgs -y
        if ($LASTEXITCODE) { throw "xmake config failed for $t" }
        Write-Host "==> building $t"
        xmake build -P $root -y -j $Jobs Lepton
        if ($LASTEXITCODE) { throw "build failed for $t" }
        $out = Join-Path $root "bin/$t/Lepton"
        if (-not (Test-Path (Join-Path $out 'Lepton.dll'))) { throw "missing output $out" }
        Write-Host "==> $t ready: $out"
    }
    if ($Tests) {
        Write-Host '==> unit tests'
        xmake build -P $root -y -j $Jobs lp-tests
        if ($LASTEXITCODE) { throw 'lp-tests build failed' }
        xmake run -P $root lp-tests
        if ($LASTEXITCODE) { throw 'unit tests failed' }
    }
} finally {
    Pop-Location
    $slotLock.Dispose()
}
