param(
    [switch]$SkipHosts,
    [int]$IngestionWaitSeconds = 30
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

$dotEnv = Join-Path $root ".env"
if (Test-Path $dotEnv) {
    Get-Content $dotEnv | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#") -and $line -match "^([^=]+)=(.*)$") {
            $name = $matches[1].Trim()
            $value = $matches[2].Trim()
            if ($value.StartsWith('"') -and $value.EndsWith('"')) { $value = $value.Substring(1, $value.Length - 2) }
            Set-Item -Path "Env:$name" -Value $value
        }
    }
}

if (-not $env:OTEL_EXPORTER_OTLP_ENDPOINT) {
    $env:OTEL_EXPORTER_OTLP_ENDPOINT = [Environment]::GetEnvironmentVariable('OTEL_EXPORTER_OTLP_ENDPOINT', 'User')
}
if (-not $env:OTEL_EXPORTER_OTLP_ENDPOINT) {
    $env:OTEL_EXPORTER_OTLP_ENDPOINT = 'http://127.0.0.1:4318'
}

$config = Get-Content (Join-Path $root "matrix-config.json") -Raw | ConvertFrom-Json
$gg = "C:\dev\TestGgLib\TestGgLib\bin\Release\net8.0\gg.exe"
$phpExe = if (Test-Path "C:\php84\php.exe") { "C:\php84\php.exe" } else { (Get-Command php -ErrorAction SilentlyContinue).Source }
$rubyHome = if ($env:RUBY_HOME) { $env:RUBY_HOME } else { "C:\Ruby34-x64" }
$rubyExe = Join-Path $rubyHome "bin\ruby.exe"
$logs = Join-Path $root "logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

function Test-PortListen([int]$Port) {
    return [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1)
}

function Start-HostProcess([string]$Name, $HostCfg) {
    if (Test-PortListen $HostCfg.port) {
        Write-Host "Host $Name already listening on $($HostCfg.port)"
        return
    }

    if ($Name -eq "Php") {
        $psi = Start-Process powershell -ArgumentList @(
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", "C:\dev\TestGgPhpLib\run-host.ps1",
            "-Port", [string]$HostCfg.port
        ) -WorkingDirectory "C:\dev\TestGgPhpLib" -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $logs "host-$Name-out.log") `
            -RedirectStandardError (Join-Path $logs "host-$Name-err.log")
        Write-Host "Started Php host pid=$($psi.Id) port=$($HostCfg.port)"
        return
    }

    if ($Name -eq "Ruby") {
        $psi = Start-Process powershell -ArgumentList @(
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", "C:\dev\TestGgRubyLib\run-host.ps1",
            "-Port", [string]$HostCfg.port
        ) -WorkingDirectory "C:\dev\TestGgRubyLib" -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $logs "host-$Name-out.log") `
            -RedirectStandardError (Join-Path $logs "host-$Name-err.log")
        Write-Host "Started Ruby host pid=$($psi.Id) port=$($HostCfg.port)"
        return
    }

    throw "Unknown host $Name"
}

if (-not $SkipHosts) {
    foreach ($entry in $config.hosts.PSObject.Properties) {
        $p = [int]$entry.Value.port
        $conn = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($conn) {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$($conn.OwningProcess)"
            if ($proc.CommandLine -notmatch $entry.Name -and $proc.CommandLine -notmatch 'TestGg') {
                Write-Host "Stopping pid $($conn.OwningProcess) on port $p"
                Stop-Process -Id $conn.OwningProcess -Force -ErrorAction SilentlyContinue
                Start-Sleep 2
            }
        }
    }

    foreach ($entry in $config.hosts.PSObject.Properties) {
        Start-HostProcess $entry.Name $entry.Value
    }

    $deadline = (Get-Date).AddMinutes(3)
    foreach ($entry in $config.hosts.PSObject.Properties) {
        $p = [int]$entry.Value.port
        while ((Get-Date) -lt $deadline -and -not (Test-PortListen $p)) { Start-Sleep 3 }
        if (-not (Test-PortListen $p)) {
            Write-Warning "Host $($entry.Name) did not open port $p; calls to this callee may FAIL"
        }
    }
}

$configPath = Join-Path $root "matrix-config.json"
$results = @()

if (-not $phpExe -or -not (Test-Path $phpExe)) {
    throw "php.exe not found (set PATH or install to C:\php84)"
}

Write-Host "Running PHP caller ($phpExe)..."
& $phpExe (Join-Path $root "php\matrix_caller.php") $configPath | ForEach-Object {
    if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ }
}

$rubyDir = Join-Path $root "ruby"
if (Test-Path $rubyExe) {
    if (-not (Test-Path (Join-Path $rubyDir "Gemfile.lock"))) {
        Write-Host "Installing Ruby caller gems (bundle install)..."
        Push-Location $rubyDir
        $env:BUNDLE_GEMFILE = Join-Path $rubyDir "Gemfile"
        $env:PATH = (Join-Path $rubyHome "bin") + ";" + $env:PATH
        bundle install --quiet 2>&1 | Out-Null
        Pop-Location
    }

    Write-Host "Running Ruby caller ($rubyExe)..."
    Push-Location $rubyDir
    $env:BUNDLE_GEMFILE = Join-Path $rubyDir "Gemfile"
    $env:PATH = (Join-Path $rubyHome "bin") + ";" + (Join-Path $rubyHome "bin\ruby_builtin_dlls") + ";" + $env:PATH
    & $rubyExe (Join-Path $rubyDir "matrix_caller.rb") $configPath | ForEach-Object {
        if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ }
    }
    Pop-Location
} else {
    Write-Warning "ruby.exe not found at $rubyExe; skipping Ruby caller"
}

Start-Sleep $IngestionWaitSeconds

$searchTemplate = $config.datadogSearchTemplate
$rows = foreach ($line in ($results | ForEach-Object Line)) {
    $p = $line -split '\|', 5
    $traceId = if ($p.Count -ge 4) { $p[3] } else { "" }
    [pscustomobject]@{
        Status          = $p[0]
        Caller          = $p[1]
        Called          = $p[2]
        TraceId         = $traceId
        Result          = if ($p.Count -ge 5) { $p[4] } else { "" }
        DatadogSearch   = if ($traceId -and $searchTemplate) { $searchTemplate -replace '\{traceId\}', $traceId } else { "" }
    }
}

Write-Host ""
Write-Host "=== CrossTech OneToOne DataDog ($((Get-Date).ToString('o'))) ==="
$rows | Format-Table -AutoSize | Out-String | Write-Host
$csv = Join-Path $root "CrossTech_OneToOne_DataDog-results.csv"
$rows | Export-Csv -NoTypeInformation -Path $csv
Write-Host "Saved: $csv"