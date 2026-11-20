param(
    [switch]$SkipHosts,
    [int]$IngestionWaitSeconds = 60
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

if (-not $env:APPLICATIONINSIGHTS_CONNECTION_STRING) {
    $env:APPLICATIONINSIGHTS_CONNECTION_STRING = [Environment]::GetEnvironmentVariable(
        "APPLICATIONINSIGHTS_CONNECTION_STRING", "User")
}
if (-not $env:APPLICATIONINSIGHTS_CONNECTION_STRING) {
    throw "Set APPLICATIONINSIGHTS_CONNECTION_STRING in .env, process env, or User environment variables."
}

if (-not $env:OTEL_EXPORTER_OTLP_ENDPOINT) {
    $env:OTEL_EXPORTER_OTLP_ENDPOINT = [Environment]::GetEnvironmentVariable('OTEL_EXPORTER_OTLP_ENDPOINT', 'User')
}
if (-not $env:OTEL_EXPORTER_OTLP_ENDPOINT) {
    $env:OTEL_EXPORTER_OTLP_ENDPOINT = 'http://127.0.0.1:4318'
}

$config = Get-Content (Join-Path $root "matrix-config.json") -Raw | ConvertFrom-Json
$gg = "C:\dev\TestGgLib\TestGgLib\bin\Release\net8.0\gg.exe"
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

    if ($Name -eq "DotNet") {
        $cwd = "C:\dev\TestGgLib\TestGgLib\bin\Release\net8.0"
        $p = [int]$HostCfg.port
        $args = @(
            ".\TestGgLib.dll", "--initMethod", "TestGgLib.Program.Main",
            "--port", "$p", "--httpPort", "$($p + 1)", "--tcpPort", "$($p + 2)", "--http2Port", "$($p + 3)",
            "--doNotExtractBinaries"
        )
        $psi = Start-Process $gg -ArgumentList $args -WorkingDirectory $cwd -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $logs "host-$Name-out.log") `
            -RedirectStandardError (Join-Path $logs "host-$Name-err.log")
        Write-Host "Started DotNet host pid=$($psi.Id) port=$p"
        return
    }

    if ($Name -eq "Node") {
        $p = [int]$HostCfg.port
        $args = @(
            "./index.mjs", "--runtime", "nodejs", "--port", "$p",
            "--httpPort", "$($p + 1)", "--tcpPort", "$($p + 2)", "--http2Port", "$($p + 3)",
            "--doNotExtractBinaries"
        )
        $psi = Start-Process $gg -ArgumentList $args -WorkingDirectory "C:\dev\TestGgNodeLib" -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $logs "host-$Name-out.log") `
            -RedirectStandardError (Join-Path $logs "host-$Name-err.log")
        Write-Host "Started Node host pid=$($psi.Id) port=$p"
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
            $cmd = $proc.CommandLine
            if ($cmd -and $cmd -notmatch 'TestGg' -and $cmd -notmatch $entry.Name) {
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
            Write-Warning "Host $($entry.Name) did not open port $p; calls may FAIL"
        }
    }
}

$configPath = Join-Path $root "matrix-config.json"
$results = @()

Write-Host "Building .NET caller..."
Push-Location (Join-Path $root "dotnet")
dotnet build CrossTechOneToOneAppInsightsToDataDogRunner.csproj -c Release -v q --source https://api.nuget.org/v3/index.json | Out-Null
$dnOut = Join-Path (Get-Location) "bin\Release\net8.0\CrossTechOneToOneAppInsightsToDataDogRunner.exe"
Pop-Location

Write-Host "Running .NET caller..."
& $dnOut $configPath | ForEach-Object {
    if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ }
}

$nodeDir = Join-Path $root "node"
$nodeExe = (Get-Command node -ErrorAction SilentlyContinue).Source
if (-not $nodeExe) {
    foreach ($candidate in @(
        "$env:ProgramFiles\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\node\node.exe",
        "C:\dev\_tools\node\node.exe"
    )) {
        if (Test-Path $candidate) { $nodeExe = $candidate; break }
    }
}

$nodeBin = if ($nodeExe) { Split-Path $nodeExe -Parent } else { $null }
$npmExe = (Get-Command npm -ErrorAction SilentlyContinue).Source
if (-not $npmExe -and $nodeBin) {
    $npmCandidate = Join-Path $nodeBin "npm.cmd"
    if (Test-Path $npmCandidate) { $npmExe = $npmCandidate }
}
if ($nodeBin) {
    $env:PATH = "$nodeBin;" + $env:PATH
}

if ($nodeExe) {
    if (-not (Test-Path (Join-Path $nodeDir "node_modules"))) {
        if (-not $npmExe) {
            throw "node.exe found but npm.cmd not found; install Node.js npm or run npm install in $nodeDir"
        }
        Write-Host "Installing Node caller dependencies (npm ci)..."
        Push-Location $nodeDir
        & $npmExe ci --silent 2>$null
        if ($LASTEXITCODE -ne 0) { & $npmExe install --silent }
        Pop-Location
    }
    Write-Host "Running Node caller ($nodeExe)..."
    Push-Location $nodeDir
    & $nodeExe (Join-Path $nodeDir "matrix_caller.mjs") $configPath | ForEach-Object {
        if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ }
    }
    Pop-Location
} else {
    Write-Warning "node.exe not found; skipping Node caller"
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

$aiTemplate = $config.appInsightsSearchTemplate
$ddTemplate = $config.datadogSearchTemplate
$rows = foreach ($line in ($results | ForEach-Object Line)) {
    $p = $line -split '\|', 5
    $caller = $p[1]
    $callee = $p[2]
    $traceId = if ($p.Count -ge 4) { $p[3] } else { "" }
    $callerProvider = if ($caller -eq 'Ruby') { 'Datadog' } else { 'AppInsights' }
    $calleeHost = $config.hosts.$callee
    $calleeProvider = if ($calleeHost.telemetry -eq 'datadog') { 'Datadog' } else { 'AppInsights' }

    [pscustomobject]@{
        Status             = $p[0]
        Caller             = $caller
        Called             = $callee
        CallerProvider     = $callerProvider
        CalleeProvider     = $calleeProvider
        TraceId            = $traceId
        Result             = if ($p.Count -ge 5) { $p[4] } else { "" }
        AppInsightsSearch  = if ($traceId -and $callerProvider -eq 'AppInsights' -and $aiTemplate) {
            $aiTemplate -replace '\{traceId\}', $traceId
        } else { "" }
        DatadogSearch      = if ($traceId -and $callerProvider -eq 'Datadog' -and $ddTemplate) {
            $ddTemplate -replace '\{traceId\}', $traceId
        } else { "" }
    }
}

Write-Host ""
Write-Host "=== CrossTech OneToOne AppInsights <-> DataDog ($((Get-Date).ToString('o'))) ==="
$rows | Format-Table -AutoSize | Out-String | Write-Host
$csv = Join-Path $root "CrossTech_OneToOne_AppInsights_To_DataDog-results.csv"
$rows | Export-Csv -NoTypeInformation -Path $csv
Write-Host "Saved: $csv"