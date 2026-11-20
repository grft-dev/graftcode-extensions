param(
    [switch]$SkipHosts,
    [int]$IngestionWaitSeconds = 90
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

$config = Get-Content (Join-Path $root "matrix-config.json") -Raw | ConvertFrom-Json
$gg = "C:\dev\TestGgLib\TestGgLib\bin\Release\net8.0\gg.exe"
$mvn = if ($env:MAVEN_HOME) { Join-Path $env:MAVEN_HOME "bin\mvn.cmd" } else { "C:\dev\_tools\apache-maven-3.9.9\bin\mvn.cmd" }
$logs = Join-Path $root "logs"
New-Item -ItemType Directory -Force -Path $logs | Out-Null

if (-not $env:APPLICATIONINSIGHTS_CONNECTION_STRING) {
    $env:APPLICATIONINSIGHTS_CONNECTION_STRING = [Environment]::GetEnvironmentVariable(
        "APPLICATIONINSIGHTS_CONNECTION_STRING", "User")
}
if (-not $env:APPLICATIONINSIGHTS_CONNECTION_STRING) {
    throw "Set APPLICATIONINSIGHTS_CONNECTION_STRING in .env, process env, or User environment variables."
}

function Test-PortListen([int]$Port) {
    return [bool](Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1)
}

function Start-HostProcess([string]$Name, $HostCfg) {
    if (Test-PortListen $HostCfg.port) {
        Write-Host "Host $Name already listening on $($HostCfg.port)"
        return
    }

    if ($Name -eq "Python") {
        $psi = Start-Process powershell -ArgumentList @(
            "-NoProfile", "-ExecutionPolicy", "Bypass",
            "-File", (Join-Path "C:\dev\TestGgPythonLib" "run-host.ps1")
        ) -WorkingDirectory "C:\dev\TestGgPythonLib" -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $logs "host-$Name-out.log") `
            -RedirectStandardError (Join-Path $logs "host-$Name-err.log")
        Write-Host "Started Python host pid=$($psi.Id)"
        return
    }

    $cwd = $HostCfg.start.cwd
    $args = @($HostCfg.start.args)
    $psi = Start-Process $gg -ArgumentList $args -WorkingDirectory $cwd -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logs "host-$Name-out.log") `
        -RedirectStandardError (Join-Path $logs "host-$Name-err.log")
    Write-Host "Started $Name host pid=$($psi.Id) cwd=$cwd"
}

if (-not $SkipHosts) {
    foreach ($entry in $config.hosts.PSObject.Properties) {
        $p = [int]$entry.Value.port
        $conn = Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($conn) {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$($conn.OwningProcess)"
            if ($proc.CommandLine -notmatch $entry.Name) {
                Write-Host "Stopping pid $($conn.OwningProcess) on port $p (not $($entry.Name))"
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

Write-Host "Building .NET caller..."
Push-Location (Join-Path $root "dotnet")
dotnet build CrossTechOneToOneAppInsightsRunner.csproj -c Release -v q --source https://api.nuget.org/v3/index.json | Out-Null
$dnOut = Join-Path (Get-Location) "bin\Release\net8.0\CrossTechOneToOneAppInsightsRunner.exe"
Pop-Location

Write-Host "Running .NET caller..."
& $dnOut $configPath | ForEach-Object {
    if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ }
}

if (Test-Path $mvn) {
    Write-Host "Building Java caller..."
    Push-Location (Join-Path $root "java")
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $mvn -q -DskipTests package 2>&1 | Out-Null
    Write-Host "Running Java caller..."
    & $mvn -q exec:java "-Dexec.args=$configPath" 2>&1 | ForEach-Object {
        if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ } else { Write-Host $_ }
    }
    $ErrorActionPreference = $prevEap
    Pop-Location
} else {
    Write-Warning "Maven not found at $mvn; skipping Java caller"
}

Write-Host "Running Python caller..."
$py = "C:\dev\TestGgPythonWebApi\.venv\Scripts\python.exe"
if (Test-Path $py) {
    & $py (Join-Path $root "python\matrix_caller.py") $configPath | ForEach-Object {
        if ($_ -match '^(OK|FAIL)\|') { $results += [pscustomobject]@{ Line = $_ } ; $_ }
    }
} else {
    Write-Warning "Python venv not found at $py; skipping Python caller"
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

if ($nodeExe) {
    if (-not (Test-Path (Join-Path $nodeDir "node_modules"))) {
        Write-Host "Installing Node caller dependencies (npm ci)..."
        Push-Location $nodeDir
        npm ci --silent 2>$null
        if ($LASTEXITCODE -ne 0) { npm install --silent }
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

Start-Sleep $IngestionWaitSeconds

$rows = foreach ($line in ($results | ForEach-Object Line)) {
    $p = $line -split '\|', 5
    [pscustomobject]@{
        Status            = $p[0]
        Caller            = $p[1]
        Called            = $p[2]
        TraceId           = $p[3]
        Result            = if ($p.Count -ge 5) { $p[4] } else { "" }
        AppInsightsSearch = if ($p[3]) {
            "union requests,dependencies,traces | where operation_Id == `"$($p[3])`""
        } else { "" }
    }
}

Write-Host ""
Write-Host "=== CrossTech OneToOne App Insights ($((Get-Date).ToString('o'))) ==="
$rows | Format-Table -AutoSize | Out-String | Write-Host
$csv = Join-Path $root "CrossTech_OneToOne_AppInsights-results.csv"
$rows | Export-Csv -NoTypeInformation -Path $csv
Write-Host "Saved: $csv"