param(
    [Parameter(Mandatory)][string]$SdlSource,
    [Parameter(Mandatory)][string]$SdlLibrary,
    [Parameter(Mandatory)][Alias('ProbeLibrary')][string]$MainLibrary,
    [Parameter(Mandatory)][string]$AndroidJar,
    [Parameter(Mandatory)][string]$BuildTools,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$Jdk,
    [string]$UnitAssets,
    [string]$TerrainAsset,
    [switch]$Debuggable,
    [string]$Manifest = "$PSScriptRoot/AndroidManifest.xml",
    [string[]]$ExtraLibraries = @(),
    [string]$ApkName = 'recoil-graphics-probe.apk'
)

$ErrorActionPreference = 'Stop'

function Invoke-BuildTool {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE"
    }
}

$sdkJar = (Resolve-Path -LiteralPath $AndroidJar).Path
$sdkTools = (Resolve-Path -LiteralPath $BuildTools).Path
$sdlRoot = (Resolve-Path -LiteralPath $SdlSource).Path
$javaBin = Join-Path (Resolve-Path -LiteralPath $Jdk).Path 'bin'
$env:JAVA_HOME = (Resolve-Path -LiteralPath $Jdk).Path
$output = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName
$work = (New-Item -ItemType Directory -Path (Join-Path $output ([guid]::NewGuid().ToString()))).FullName
$classes = (New-Item -ItemType Directory -Path "$work/classes").FullName
$javaSources = @(Get-ChildItem -LiteralPath "$sdlRoot/android-project/app/src/main/java/org/libsdl/app" -Filter '*.java' |
    ForEach-Object FullName)

Invoke-BuildTool "$javaBin/javac.exe" (@('--release', '8', '-classpath', $sdkJar, '-d', $classes) + $javaSources)
Invoke-BuildTool "$javaBin/jar.exe" @('--create', '--file', "$work/sdl-java.jar", '-C', $classes, '.')
Invoke-BuildTool "$sdkTools/d8.bat" @('--lib', $sdkJar, '--min-api', '28', '--output', $work, "$work/sdl-java.jar")
$linkArgs = @('link', '-I', $sdkJar, '--manifest', $Manifest, '-o', "$work/unsigned.apk")
if ($Debuggable) {
    $linkArgs += '--debug-mode'
}
Invoke-BuildTool "$sdkTools/aapt2.exe" $linkArgs

$archive = [IO.Compression.ZipFile]::Open("$work/unsigned.apk", [IO.Compression.ZipArchiveMode]::Update)
try {
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, "$work/classes.dex", 'classes.dex') | Out-Null
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Resolve-Path -LiteralPath $SdlLibrary).Path, 'lib/arm64-v8a/libSDL2.so') | Out-Null
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Resolve-Path -LiteralPath $MainLibrary).Path, 'lib/arm64-v8a/libmain.so') | Out-Null
    foreach ($library in $ExtraLibraries) {
        $libraryPath = (Resolve-Path -LiteralPath $library).Path
        $entry = 'lib/arm64-v8a/' + [IO.Path]::GetFileName($libraryPath)
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $libraryPath, $entry) | Out-Null
    }
    if ($TerrainAsset) {
        $terrainPath = (Resolve-Path -LiteralPath $TerrainAsset).Path
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $terrainPath, 'assets/terrain-draw.bin') | Out-Null
    }
    if ($UnitAssets) {
        foreach ($asset in 'unit-mesh.bin', 'unit-color.rgba', 'unit-material.rgba', 'unit-credits.txt',
                           'terrain-mesh.bin', 'terrain-color.rgba') {
            $assetPath = (Resolve-Path -LiteralPath (Join-Path $UnitAssets $asset)).Path
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $assetPath, "assets/$asset") | Out-Null
        }
    }
} finally {
    $archive.Dispose()
}

$key = "$output/probe-debug.p12"
if (!(Test-Path -LiteralPath $key)) {
    Invoke-BuildTool "$javaBin/keytool.exe" @('-genkeypair', '-keystore', $key, '-storetype', 'PKCS12',
        '-storepass', 'android', '-alias', 'recoil-probe', '-keyalg', 'RSA', '-validity', '3650',
        '-dname', 'CN=Recoil Graphics Probe Debug')
}

$apk = Join-Path $output $ApkName
Invoke-BuildTool "$sdkTools/zipalign.exe" @('-f', '4', "$work/unsigned.apk", "$work/aligned.apk")
Invoke-BuildTool "$sdkTools/apksigner.bat" @('sign', '--ks', $key, '--ks-pass', 'pass:android',
    '--out', $apk, "$work/aligned.apk")
Invoke-BuildTool "$sdkTools/apksigner.bat" @('verify', '--verbose', $apk)
Write-Output $apk
