# SPDX-FileCopyrightText: Furoshiki contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Furoshiki's OCR helper on a Windows computer: the small service that reads a game window's
# text (furoshiki_ocr), with a Python of its own, in one folder, which Furoshiki finds and starts
# when a title is read by OCR.
#
#   install.cmd                         (double-click; or:)
#   powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 [-Root FOLDER] [-Port 8794]
#       [-PythonVersion 3.13.17] [-StopRunning] [-NoCheck] [-Uninstall]
#
# Unzip the package into Furoshiki's own folder and run it: the helper then goes into
# FuroshikiOcrHelper in that folder (-Root names another place; Furoshiki looks in its own folder
# and beside it). Everything goes under that folder and nowhere else: python\ (python.org's own
# build, from its NuGet package: no installer is run, nothing is registered), the packages of
# requirements\ inside it, app\furoshiki_ocr (the helper's code), hf\ (meikiocr's model), home\
# (where OneOCR's and Chrome Screen AI's own files are kept at their first use), state\ (the
# text areas and the engine chosen), config.json (loopback only, with a token made here),
# helper.json (what Furoshiki reads: the address, the token and how to start it) and start.cmd
# (to start it by hand). No service, scheduled task, firewall rule, registry key or PATH entry is
# made: the helper listens on 127.0.0.1 only and runs only while Furoshiki has started it.
#
# Run again, it is an update: Python and the packages are left when they are already the ones
# asked for, the code is replaced, the token and the state are kept. It needs the internet the
# first time (nuget.org, pypi.org, huggingface.co: about 1 GB in all) and no administrator.
param(
    [string]$Root = "",
    [int]$Port = 8794,
    [string]$PythonVersion = "3.13.17",
    [string]$Source = "furoshiki",
    [switch]$StopRunning,
    [switch]$NoCheck,
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Root) { $Root = Join-Path (Split-Path -Parent $here) "FuroshikiOcrHelper" }
$did = New-Object System.Collections.Generic.List[string]
function Say([string]$text) { Write-Host $text; $script:did.Add($text) }
function Fail([string]$text) { Write-Host "install.ps1: $text"; exit 1 }

# Runs a program with its output in a file (PowerShell 5.1 makes an error of anything a
# program writes to its standard error otherwise); the exit code.
function Run([string]$program, [string[]]$arguments, [string]$log) {
    $quoted = $arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
    $process = Start-Process -FilePath $program -ArgumentList $quoted -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
    Get-Content "$log.out", "$log.err" -ErrorAction SilentlyContinue | Set-Content -Encoding UTF8 $log
    Remove-Item "$log.out", "$log.err" -ErrorAction SilentlyContinue
    return $process.ExitCode
}

function WriteText([string]$path, [string]$text) {
    [System.IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

# The helper's own processes: a python.exe of this folder's Python.
function Running {
    $python = (Join-Path $Root "python\python.exe").ToLowerInvariant()
    @(Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" -ErrorAction SilentlyContinue |
        Where-Object { $_.ExecutablePath -and $_.ExecutablePath.ToLowerInvariant() -eq $python })
}

function PortTaken([int]$port) {
    $listener = $null
    try {
        $listener = New-Object System.Net.Sockets.TcpListener([System.Net.IPAddress]::Loopback, $port)
        $listener.Start()
        return $false
    } catch { return $true } finally { if ($listener) { $listener.Stop() } }
}

$running = Running
if ($running.Count -gt 0) {
    if (-not $StopRunning) {
        Fail "the helper is running (pid $(($running | ForEach-Object { $_.ProcessId }) -join ', ')): close Furoshiki, or run this again with -StopRunning"
    }
    $running | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    Start-Sleep -Milliseconds 800
    Say "stopped the running helper"
}

if ($Uninstall) {
    if (Test-Path $Root) {
        Remove-Item -Recurse -Force $Root
        Say "removed $Root (Furoshiki's own setting, program\sources.json's `"ocr`", is left as it is)"
    } else { Say "nothing at $Root" }
    exit 0
}

# ---- the code and the requirements: beside this script, as the package has them ----
$code = Join-Path $here "furoshiki_ocr"
$requirements = Join-Path $here "requirements"
foreach ($file in "app.py", "engine.py", "stabiliser.py", "cleaner.py") {
    if (-not (Test-Path (Join-Path $code $file))) { Fail "furoshiki_ocr\$file is not beside this script: run it from the unzipped package" }
}
foreach ($file in "runtime.txt", "helper-windows.txt") {
    if (-not (Test-Path (Join-Path $requirements $file))) { Fail "requirements\$file is missing" }
}
if (-not [Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -ne "AMD64") { Fail "this is for 64-bit Windows on x86-64" }

New-Item -ItemType Directory -Force -Path $Root, (Join-Path $Root "state"), (Join-Path $Root "hf"), (Join-Path $Root "logs"), (Join-Path $Root "home") | Out-Null
$logs = Join-Path $Root "logs"
$python = Join-Path $Root "python\python.exe"
# Nothing of the user's own Python setup is read or written: no user site, no pip settings.
$env:PYTHONNOUSERSITE = "1"; $env:PYTHONUTF8 = "1"; $env:PIP_CONFIG_FILE = "nul"; $env:PIP_NO_CACHE_DIR = "1"
Remove-Item Env:PYTHONPATH, Env:PYTHONHOME -ErrorAction SilentlyContinue

# ---- Python ----
$have = ""
if (Test-Path $python) { $have = (& $python -c "import platform; print(platform.python_version())" 2>$null) }
if ($have -ne $PythonVersion) {
    $package = Join-Path $Root "python-$PythonVersion.zip"
    $url = "https://api.nuget.org/v3-flatcontainer/python/$PythonVersion/python.$PythonVersion.nupkg"
    & curl.exe -fsSL --retry 3 -o $package $url
    if ($LASTEXITCODE -ne 0) { Fail "could not fetch $url" }
    $unpacked = Join-Path $Root "python.unpacking"
    if (Test-Path $unpacked) { Remove-Item -Recurse -Force $unpacked }
    Expand-Archive -Path $package -DestinationPath $unpacked
    if (-not (Test-Path (Join-Path $unpacked "tools\python.exe"))) { Fail "the Python package has no tools\python.exe" }
    if (Test-Path (Join-Path $Root "python")) { Remove-Item -Recurse -Force (Join-Path $Root "python") }
    Move-Item (Join-Path $unpacked "tools") (Join-Path $Root "python")
    Remove-Item -Recurse -Force $unpacked
    Remove-Item -Force $package
    Remove-Item -Force (Join-Path $Root "packages.stamp") -ErrorAction SilentlyContinue
    $have = (& $python -c "import platform; print(platform.python_version())")
    if ($have -ne $PythonVersion) { Fail "the Python unpacked says it is '$have'" }
    Say "Python $PythonVersion (python.org's NuGet package) in $Root\python"
} else { Say "Python $PythonVersion was there already" }

# ---- the packages ----
$wanted = (Get-Content (Join-Path $requirements "runtime.txt"), (Join-Path $requirements "helper-windows.txt") |
    Where-Object { $_ -and -not $_.StartsWith("#") }) -join "`n"
$stamp = Join-Path $Root "packages.stamp"
$stamped = ""
if (Test-Path $stamp) { $stamped = [System.IO.File]::ReadAllText($stamp) }
if ($stamped -ne "$PythonVersion`n$wanted") {
    $code_ = Run $python @("-m", "pip", "--version") (Join-Path $logs "pip-version.log")
    if ($code_ -ne 0) {
        $code_ = Run $python @("-m", "ensurepip", "--upgrade") (Join-Path $logs "ensurepip.log")
        if ($code_ -ne 0) { Fail "this Python has no pip; see $logs\ensurepip.log" }
    }
    $code_ = Run $python @("-m", "pip", "install", "--isolated", "--no-deps", "--only-binary=:all:",
        "--disable-pip-version-check", "--no-warn-script-location", "--no-input",
        "-r", (Join-Path $requirements "runtime.txt"), "-r", (Join-Path $requirements "helper-windows.txt")) (Join-Path $logs "pip-install.log")
    if ($code_ -ne 0) { Fail "the packages could not be installed; see $logs\pip-install.log" }
    WriteText $stamp "$PythonVersion`n$wanted"
    Say "the packages of runtime.txt and helper-windows.txt ($(@($wanted -split "`n").Count)) installed into that Python"
} else { Say "the packages were there already" }

# ---- the code ----
$app = Join-Path $Root "app"
if (Test-Path $app) { Remove-Item -Recurse -Force $app }
New-Item -ItemType Directory -Force -Path $app | Out-Null
Copy-Item -Recurse -Path $code -Destination (Join-Path $app "furoshiki_ocr")
Get-ChildItem -Path $app -Recurse -Directory -Filter "__pycache__" | Remove-Item -Recurse -Force
$build = ""
if (Test-Path (Join-Path $here "BUILD")) { $build = (Get-Content (Join-Path $here "BUILD") -TotalCount 1) }
Say "furoshiki_ocr copied to $app$(if ($build) { " (build $build)" })"

# ---- its settings: the token is kept from one install to the next ----
$configFile = Join-Path $Root "config.json"
$token = ""
if (Test-Path $configFile) {
    try {
        $old = Get-Content -Raw -Encoding UTF8 $configFile | ConvertFrom-Json
        if ("$($old.token)".Length -ge 16) { $token = "$($old.token)" }
    } catch { }
}
if ($token) { Say "the token was kept" } else {
    $bytes = New-Object byte[] 24
    $random = [System.Security.Cryptography.RandomNumberGenerator]::Create()
    $random.GetBytes($bytes)
    $token = ([System.BitConverter]::ToString($bytes) -replace "-", "").ToLowerInvariant()
    Say "a new token was made for the picture source '$Source'"
}
# A helper running from another folder, or anything else, may hold the port: the next free pair.
$chosen = $Port
while (PortTaken $chosen) {
    $chosen += 1
    if ($chosen -gt $Port + 40) { Fail "no free loopback port from $Port on" }
}
if ($chosen -ne $Port) { Say "port $Port is taken on this computer: using $chosen" }
$config = [ordered]@{ listen_port = $chosen; source = $Source; token = $token; state_dir = "state"; engine = "meikiocr" }
WriteText $configFile ($config | ConvertTo-Json -Depth 5)
# What Furoshiki reads (home/src/ocr_helper.h). "{folder}" is this folder.
$helper = [ordered]@{
    format = 1; name = "Furoshiki OCR helper"
    address = "http://127.0.0.1:$chosen"; source = $Source; token = $token
    program = @("python/python.exe", "-m", "furoshiki_ocr", "--config", "config.json", "--stop-with-stdin")
    env = [ordered]@{ PYTHONPATH = "{folder}/app"; HF_HOME = "{folder}/hf"; HF_HUB_OFFLINE = "1"
                      PYTHONNOUSERSITE = "1"; PYTHONUTF8 = "1"; LOGURU_COLORIZE = "False"
                      USERPROFILE = "{folder}/home"; HOME = "{folder}/home" }
    build = $build; python = $PythonVersion
}
WriteText (Join-Path $Root "helper.json") ($helper | ConvertTo-Json -Depth 5)
WriteText (Join-Path $Root "start.cmd") (@(
    "@echo off",
    "rem The OCR helper by hand, in this window (Ctrl+C or closing it stops it). Furoshiki starts",
    "rem it by itself, hidden, when a window is read by OCR; this is for looking at what it says.",
    "cd /d ""%~dp0""",
    "set PYTHONPATH=%~dp0app",
    "set HF_HOME=%~dp0hf",
    "set USERPROFILE=%~dp0home",
    "set HOME=%~dp0home",
    "set HF_HUB_OFFLINE=1",
    "set PYTHONNOUSERSITE=1",
    "set PYTHONUTF8=1",
    "set LOGURU_COLORIZE=False",
    """%~dp0python\python.exe"" -m furoshiki_ocr --config ""%~dp0config.json"" %*",
    ""
) -join "`r`n")
Say "config.json (127.0.0.1:$chosen), helper.json and start.cmd written"

# ---- meikiocr's model, and that the engine loads here ----
$env:PYTHONPATH = $app; $env:HF_HOME = (Join-Path $Root "hf"); $env:LOGURU_COLORIZE = "False"
Remove-Item Env:HF_HUB_OFFLINE -ErrorAction SilentlyContinue
$started = Get-Date
$code_ = Run $python @("-c", "import onnxruntime; from owocr.ocr import MeikiOCR; ok = MeikiOCR().available; print('onnxruntime', onnxruntime.__version__, onnxruntime.get_available_providers()); raise SystemExit(0 if ok else 'meikiocr did not load')") (Join-Path $logs "model.log")
if ($code_ -ne 0) { Fail "meikiocr did not load; see $logs\model.log" }
$size = [math]::Round(((Get-ChildItem -Recurse -File (Join-Path $Root "hf") | Measure-Object Length -Sum).Sum / 1MB), 0)
Say "meikiocr loads (its model: $size MB in $Root\hf; $([math]::Round(((Get-Date) - $started).TotalSeconds, 1)) s). $((Get-Content (Join-Path $logs "model.log") | Where-Object { $_ -like 'onnxruntime *' } | Select-Object -Last 1))"

# ---- a start as Furoshiki makes it: hidden, answering on loopback, stopped again ----
if (-not $NoCheck) {
    $env:HF_HUB_OFFLINE = "1"
    $started = Get-Date
    $process = Start-Process -FilePath $python -ArgumentList @("-m", "furoshiki_ocr", "--config", "`"$configFile`"") `
        -WorkingDirectory $Root -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logs "check.out.log") -RedirectStandardError (Join-Path $logs "check.log")
    $answer = $null
    for ($i = 0; $i -lt 120 -and -not $process.HasExited; $i++) {
        Start-Sleep -Milliseconds 500
        try {
            $engines = Invoke-RestMethod -Uri "http://127.0.0.1:$chosen/api/engines" -TimeoutSec 3 -Headers @{ Authorization = "Bearer $token" }
            if (@($engines | Where-Object { $_.loaded }).Count -gt 0) {
                $answer = Invoke-RestMethod -Uri "http://127.0.0.1:$chosen/api/health" -TimeoutSec 3
                break
            }
        } catch { }
    }
    $seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)
    $listening = @(Get-NetTCPConnection -State Listen -OwningProcess $process.Id -ErrorAction SilentlyContinue |
        ForEach-Object { "$($_.LocalAddress):$($_.LocalPort)" }) -join ", "
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    if (-not $answer -or $answer.role -ne "helper") { Fail "the helper did not answer at http://127.0.0.1:$chosen; see $logs\check.log" }
    Say "started hidden, it answered with its engine loaded after $seconds s (listening on $listening), and was stopped again"
}

$summary = [ordered]@{ installed = (Get-Date).ToString("s"); root = $Root; python = $PythonVersion; build = $build
                       address = "http://127.0.0.1:$chosen"; source = $Source; did = $did.ToArray() }
WriteText (Join-Path $Root "installed.json") ($summary | ConvertTo-Json -Depth 5)
Write-Host ""
Write-Host "The OCR helper is installed in $Root. It is not running: Furoshiki starts it when a window"
Write-Host "is read by OCR and stops it when it quits. In Furoshiki: Add a title, B, a game window."
