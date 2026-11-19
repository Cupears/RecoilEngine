param(
    [Parameter(Mandatory)][string]$Adb,
    [Parameter(Mandatory)][string]$Stage,
    [switch]$VersionOnly
)

$ErrorActionPreference = 'Stop'
$adbPath = (Resolve-Path -LiteralPath $Adb).Path
$stagePath = (Resolve-Path -LiteralPath $Stage).Path

function Invoke-Adb {
    & $adbPath @args
    if ($LASTEXITCODE -ne 0) {
        throw "ADB failed ($LASTEXITCODE): $args"
    }
}

$state = & $adbPath get-state 2>$null
if ($LASTEXITCODE -ne 0 -or $state -ne 'device') {
    throw 'Connect and authorize one Android device with USB debugging.'
}

$abis = Invoke-Adb shell getprop ro.product.cpu.abilist
$api = [int](Invoke-Adb shell getprop ro.build.version.sdk)
if (($abis -split ',') -notcontains 'arm64-v8a' -or $api -lt 33) {
    throw "The dedicated binary requires ARM64 and Android 13 or later (found $abis, API $api)."
}

$remote = '/data/local/tmp/recoil-dedicated-probe'
Invoke-Adb shell "mkdir -p $remote"
Invoke-Adb push "$stagePath/spring-dedicated" "$remote/spring-dedicated"
Invoke-Adb shell "chmod 700 $remote/spring-dedicated && $remote/spring-dedicated --version"
if ($VersionOnly) {
    return
}

foreach ($part in 'base', 'games', 'maps') {
    Invoke-Adb push "$stagePath/$part" "$remote/"
}
Invoke-Adb push "$PSScriptRoot/StartScript.txt" "$remote/start.txt"

$consoleLog = Join-Path $stagePath 'android-startup-console.log'
$logPath = Join-Path $stagePath 'android-startup.log'
& $adbPath shell "cd $remote && SPRING_WRITEDIR=$remote timeout 180 ./spring-dedicated --nocolor --isolation-dir=$remote start.txt" 2>&1 |
    Tee-Object -FilePath $consoleLog
$result = $LASTEXITCODE
Invoke-Adb pull "$remote/infolog.txt" $logPath
if ($result -notin 0, 124 -or !(Select-String -Quiet -Path $logPath -Pattern 'Server started on port 8452')) {
    throw "Could not verify the server started. Android exit code $result. See $logPath"
}
Write-Output "Android loaded the start script and bound the loopback server socket. Log: $logPath"
