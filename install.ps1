param(
    [string]$Binaries,
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

# Works from the repository root (uses dist\) and from an extracted release (files side by side).
$srcDir = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $srcDir "steam_api64.dll")) -and
    (Test-Path -LiteralPath (Join-Path $srcDir "dist\steam_api64.dll"))) {
    $srcDir = Join-Path $srcDir "dist"
}
$srcDll   = Join-Path $srcDir "steam_api64.dll"
$srcPanel = Join-Path $srcDir "nms-ime-panel.exe"
$srcIni   = Join-Path $srcDir "nms-ime-proxy.ini"

function Find-GameBinaries {
    if ($Binaries) {
        $p = (Resolve-Path -LiteralPath $Binaries).Path
        if (-not (Test-Path -LiteralPath (Join-Path $p "steam_api64.dll"))) {
            throw "No steam_api64.dll in $p"
        }
        return $p
    }

    $roots = @()
    $steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    if ($steam) { $roots += ($steam -replace '/', '\') }
    if ($roots.Count -gt 0) {
        $vdf = Join-Path $roots[0] 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $vdf) {
            foreach ($m in Select-String -Path $vdf -Pattern '"path"\s+"(.+?)"') {
                foreach ($g in $m.Matches) {
                    $roots += ($g.Groups[1].Value -replace '\\\\', '\')
                }
            }
        }
    }
    foreach ($root in ($roots | Select-Object -Unique)) {
        $cand = Join-Path $root "steamapps\common\No Man's Sky\Binaries"
        if (Test-Path -LiteralPath (Join-Path $cand "steam_api64.dll")) { return $cand }
    }
    throw "Could not find No Man's Sky. Rerun with -Binaries 'X:\...\No Man's Sky\Binaries'."
}

if (Get-Process -Name 'NMS' -ErrorAction SilentlyContinue) {
    throw "No Man's Sky is running. Quit the game first."
}

$game      = Find-GameBinaries
$live      = Join-Path $game 'steam_api64.dll'
$orig      = Join-Path $game 'steam_api64_orig.dll'
$panelLive = Join-Path $game 'nms-ime-panel.exe'
$iniLive   = Join-Path $game 'nms-ime-proxy.ini'

if ($Uninstall) {
    $status = if (Test-Path -LiteralPath $live) { (Get-AuthenticodeSignature -LiteralPath $live).Status } else { 'NotFound' }
    if ($status -eq 'Valid') {
        Write-Host "steam_api64.dll is already the official Valve DLL; no restore needed."
    } elseif (Test-Path -LiteralPath $orig) {
        Remove-Item -LiteralPath $live -Force -ErrorAction SilentlyContinue
        Move-Item -LiteralPath $orig -Destination $live
        Write-Host "Restored original steam_api64.dll."
    } else {
        Write-Host "No steam_api64_orig.dll backup found. Use Steam 'Verify integrity of game files' to restore the game."
    }
    Remove-Item -LiteralPath $panelLive -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $iniLive -Force -ErrorAction SilentlyContinue
    Write-Host "Removed proxy panel/config files."
    exit 0
}

foreach ($f in @($srcDll, $srcPanel, $srcIni)) {
    if (-not (Test-Path -LiteralPath $f)) {
        throw "Missing file: $f (run build.sh first, or extract the whole release zip)"
    }
}

$srcHash  = (Get-FileHash -LiteralPath $srcDll).Hash
$liveHash = if (Test-Path -LiteralPath $live) { (Get-FileHash -LiteralPath $live).Hash } else { '' }

if ($liveHash -eq $srcHash) {
    if (-not (Test-Path -LiteralPath $orig)) {
        throw "Proxy is already installed but steam_api64_orig.dll is missing. Use Steam 'Verify integrity of game files', then rerun this script."
    }
    Write-Host "Proxy already installed; original backup kept."
} else {
    if (-not (Test-Path -LiteralPath $live)) { throw "steam_api64.dll missing in $game" }
    $sig = Get-AuthenticodeSignature -LiteralPath $live
    if ($sig.Status -eq 'Valid') {
        Copy-Item -LiteralPath $live -Destination $orig -Force
        Write-Host "Backed up original steam_api64.dll -> steam_api64_orig.dll"
    } elseif (Test-Path -LiteralPath $orig) {
        Write-Host "Existing steam_api64.dll is a previous proxy; keeping the original backup."
    } else {
        throw "steam_api64.dll is unsigned and steam_api64_orig.dll is missing. Use Steam 'Verify integrity of game files', then rerun."
    }
}

Copy-Item -LiteralPath $srcDll -Destination $live -Force
Copy-Item -LiteralPath $srcPanel -Destination $panelLive -Force
if (Test-Path -LiteralPath $iniLive) {
    Write-Host "Kept existing nms-ime-proxy.ini."
} else {
    Copy-Item -LiteralPath $srcIni -Destination $iniLive -Force
    Write-Host "Installed nms-ime-proxy.ini."
}
Write-Host "Installed proxy and panel into: $game"
Get-FileHash -LiteralPath $live, $orig | Format-Table Path, Hash -AutoSize