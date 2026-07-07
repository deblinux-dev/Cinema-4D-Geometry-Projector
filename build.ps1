# build.ps1
$ErrorActionPreference = "Stop"

# Определяем корень (папка где лежит скрипт)
$RootDir = Get-Item . | Select-Object -ExpandProperty FullName
$Env:MAXON_ROOTDIR = $RootDir

Write-Host "--- Starting Build ---"
Write-Host "Root: $RootDir"

# Путь к вашему проекту плагина
$ProjectFile = "$RootDir\plugins\geometry_projection\project\geometry_projection.vcxproj"

# Запуск MSBuild
# Мы принудительно указываем PlatformToolset v143 (для VS 2022) 
# или v142 (если захотите оставить VS 2019)
& msbuild $ProjectFile `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /p:PlatformToolset=v143 `
    /p:TreatWarningsAsErrors=false `
    /p:AdditionalOptions="/wd5220 /wd5219 /wd4003 /wd4834" `
    /m

if ($LASTEXITCODE -ne 0) { throw "Build failed!" }
Write-Host "--- Build Success! ---"