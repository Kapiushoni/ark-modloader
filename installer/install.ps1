# ArkModLoader installer for ARK Mobile 2.0.29 on the MuMu emulator (Windows).
#   install.bat            - install / update the loader and the bundled mods
#   install.bat -Xapk F    - also install the game from an .xapk first
#   uninstall.bat          - restore the original game files (ArkMods folder is kept)
param(
    [switch]$Uninstall,
    [string]$Xapk = "",
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$Root    = Split-Path -Parent $PSScriptRoot          # distribution folder
$Pkg     = "com.studiowildcard.wardrumstudios.ark"
$WantVer = "152"                                     # ARK Mobile 2.0.29
$Version = (Get-Content (Join-Path $Root "VERSION") -ErrorAction SilentlyContinue | Select-Object -First 1)
# Kept outside the installer folder so replacing/deleting the folder never loses the backup.
$Data    = Join-Path $env:LOCALAPPDATA "ArkModLoader"
$Backup  = Join-Path $Data "backup"
$ModsDir = "/sdcard/Android/data/$Pkg/files/ArkMods"

function Say($t, $c = "Gray") { Write-Host $t -ForegroundColor $c }
function Step($t) { Say "`n== $t" "Cyan" }
function Fail($t) { Say "`nОШИБКА: $t" "Red"; exit 1 }

# ------------------------------------------------------------------ MuMu / adb
function Find-MuMu {
    $roots = @("$env:ProgramFiles\Netease", "${env:ProgramFiles(x86)}\Netease", "D:\Program Files\Netease", "D:\Netease")
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        $mgr = Get-ChildItem $r -Recurse -Filter MuMuManager.exe -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($mgr) {
            $adb = Join-Path $mgr.DirectoryName "adb.exe"
            if (-not (Test-Path $adb)) {
                $adb = (Get-ChildItem $r -Recurse -Filter adb.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
            }
            return @{ Manager = $mgr.FullName; Adb = $adb }
        }
    }
    return $null
}

function MuMu-Info($mgr) { (& $mgr info -v 0 2>$null | Out-String | ConvertFrom-Json) }

$script:Serial = ""
function Adb {
    # Runs adb against the emulator; returns stdout as a string. Never throws on stderr.
    $all = @("-s", $script:Serial) + $args
    $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    $out = & $script:AdbExe @all 2>$null | Out-String
    $ErrorActionPreference = $prev
    return $out.Trim()
}
function Su($cmd) { Adb shell "su -c '$cmd'" }

# ------------------------------------------------------------------ start
Say "ArkModLoader $Version — установщик для ARK Mobile на MuMu" "White"

Step "Ищу MuMu"
$mumu = Find-MuMu
if (-not $mumu -or -not $mumu.Adb) { Fail "MuMu Player не найден. Установите MuMu Player 12 и запустите установщик снова." }
$script:AdbExe = $mumu.Adb
Say "  $($mumu.Manager)"

$info = MuMu-Info $mumu.Manager
if (-not $info.is_android_started) {
    Say "  Эмулятор выключен — запускаю..."
    & $mumu.Manager control -v 0 launch | Out-Null
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep 3; $info = MuMu-Info $mumu.Manager; if ($info.is_android_started) { break } }
    if (-not $info.is_android_started) { Fail "MuMu не запустился за 3 минуты." }
    Start-Sleep 8
}
$port = if ($info.adb_port) { $info.adb_port } else { 16384 }
$script:Serial = "127.0.0.1:$port"
for ($i = 0; $i -lt 10; $i++) {
    $c = & $script:AdbExe connect $script:Serial 2>&1 | Out-String
    if ($c -match "connected") { break }
    Start-Sleep 3
}
if ((Adb get-state) -ne "device") { Fail "Не удалось подключиться к эмулятору через adb ($script:Serial). Перезапустите MuMu." }
Say "  подключено: $script:Serial" "Green"

Step "Проверяю root"
if ((Su "id") -notmatch "uid=0") {
    Fail "В MuMu выключен root. Включите: Настройки MuMu -> Другое -> Root-права, перезапустите эмулятор и запустите установщик снова."
}
Say "  root есть" "Green"

# ------------------------------------------------------------------ game
Step "Проверяю ARK Mobile"
$apkPath = (Adb shell pm path $Pkg) -replace "^package:", "" -split "`n" | Select-Object -First 1
if (-not $apkPath -and -not $Uninstall) {
    if (-not $Xapk) {
        $guess = Get-ChildItem "$env:USERPROFILE\Downloads" -Filter "*ARK*Survival*.xapk" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($guess) {
            $ans = Read-Host "  Игра не установлена. Установить из $($guess.Name)? (y/n)"
            if ($ans -match "^[yYдД]") { $Xapk = $guess.FullName }
        }
    }
    if (-not $Xapk) { Fail "ARK Mobile не установлена в MuMu. Установите игру (или запустите: install.bat -Xapk путь\к\файлу.xapk)." }

    Say "  Устанавливаю игру из $Xapk (2.3 ГБ, несколько минут)..."
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $tmp = Join-Path $env:TEMP ("arkxapk_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory $tmp | Out-Null
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($Xapk)
        $apk = $zip.Entries | Where-Object { $_.FullName -like "*.apk" } | Select-Object -First 1
        if (-not $apk) { Fail "В $Xapk нет APK." }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($apk, "$tmp\base.apk", $true)
        $r = Adb install -r "$tmp\base.apk"
        if ($r -notmatch "Success") { Fail "Не удалось установить APK: $r" }
        Adb shell mkdir -p "/sdcard/Android/obb/$Pkg" | Out-Null
        foreach ($e in ($zip.Entries | Where-Object { $_.FullName -like "*.obb" })) {
            Say "  копирую $($e.Name)..."
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, "$tmp\$($e.Name)", $true)
            Adb push "$tmp\$($e.Name)" "/sdcard/Android/obb/$Pkg/$($e.Name)" | Out-Null
            Remove-Item "$tmp\$($e.Name)"
        }
        $zip.Dispose()
    } finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }
    $apkPath = (Adb shell pm path $Pkg) -replace "^package:", "" -split "`n" | Select-Object -First 1
    if (-not $apkPath) { Fail "Игра так и не появилась в эмуляторе." }
    Say "  игра установлена" "Green"
}
if (-not $apkPath) { Fail "ARK Mobile не установлена — удалять нечего." }

$verMatch = [regex]::Match((Adb shell dumpsys package $Pkg), "versionCode=(\d+)")
$verCode = if ($verMatch.Success) { $verMatch.Groups[1].Value } else { "unknown" }
Say "  версия (versionCode): $verCode"
if ($verCode -ne $WantVer -and -not $Uninstall) {
    Say "  ВНИМАНИЕ: загрузчик сделан под ARK Mobile 2.0.29 (versionCode $WantVer). На другой версии он может не работать." "Yellow"
    $ans = Read-Host "  Всё равно продолжить? (y/n)"
    if ($ans -notmatch "^[yYдД]") { exit 1 }
}

$libDir = ((Su "dirname $($apkPath.Trim())") + "/lib/arm64").Trim()
if ((Su "ls $libDir/libgnustl_shared.so") -notmatch "libgnustl_shared") { Fail "Не найдена папка библиотек игры ($libDir). Переустановите игру." }
$backupFile = Join-Path $Backup "$verCode\libgnustl_shared.so"

# Python + LIEF in a private venv next to the installer (created on first use).
function Get-PatchPython {
    $venv = Join-Path $Data "python"
    $vpy = Join-Path $venv "Scripts\python.exe"
    if (Test-Path $vpy) { return $vpy }
    # Pick the first Python 3 that actually runs (the "py" launcher may exist without a Python).
    $py = $null; $pyArgs = @()
    foreach ($cand in @(@("python", @()), @("python3", @()), @("py", @("-3")))) {
        $cmd = (Get-Command $cand[0] -ErrorAction SilentlyContinue).Source
        if (-not $cmd -or $cmd -like "*WindowsApps*") { continue }  # skip the Microsoft Store stub
        $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
        $v = & $cmd @($cand[1]) -c "import sys; print(sys.version_info[0])" 2>$null
        $ErrorActionPreference = $prev
        if ($LASTEXITCODE -eq 0 -and "$v".Trim() -eq "3") { $py = $cmd; $pyArgs = $cand[1]; break }
    }
    if (-not $py) { Fail "Нужен Python 3 (python.org). Установите его с галочкой 'Add to PATH' и запустите установщик снова." }
    Say "  готовлю Python-окружение (один раз, нужен интернет)..."
    & $py @pyArgs -m venv $venv
    if (-not (Test-Path $vpy)) { Fail "Не удалось создать Python-окружение ($venv)." }
    & $vpy -m pip install --quiet --disable-pip-version-check lief
    if ($LASTEXITCODE -ne 0) { Fail "Не удалось установить пакет lief (нужен интернет)." }
    return $vpy
}

function Install-Lib($local, $name) {
    Adb push $local "/data/local/tmp/$name" | Out-Null
    Su "cp /data/local/tmp/$name $libDir/$name && chown system:system $libDir/$name && chmod 755 $libDir/$name && chcon u:object_r:apk_data_file:s0 $libDir/$name && rm /data/local/tmp/$name" | Out-Null
}

Su "am force-stop $Pkg" | Out-Null

# ------------------------------------------------------------------ uninstall
if ($Uninstall) {
    Step "Удаляю загрузчик"
    if (Test-Path $backupFile) {
        Install-Lib $backupFile "libgnustl_shared.so"
        Say "  оригинальный файл игры восстановлен из резервной копии" "Green"
    } else {
        # No backup (installed some other way): strip the loader reference from the game's file.
        $vpy = Get-PatchPython
        $work = Join-Path $env:TEMP ("arkml_" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory $work | Out-Null
        Su "cp $libDir/libgnustl_shared.so /data/local/tmp/gnustl.so && chmod 644 /data/local/tmp/gnustl.so" | Out-Null
        Adb pull /data/local/tmp/gnustl.so "$work\current.so" | Out-Null
        Su "rm /data/local/tmp/gnustl.so" | Out-Null
        & $vpy -I (Join-Path $PSScriptRoot "patch_needed.py") "$work\current.so" "$work\clean.so" "libarkmodloader.so" --remove
        if ($LASTEXITCODE -ne 0) { Fail "Не удалось убрать загрузчик из файла игры. Переустановите игру." }
        Install-Lib "$work\clean.so" "libgnustl_shared.so"
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
        Say "  резервной копии не было: ссылка на загрузчик убрана из файла игры" "Green"
    }
    Su "rm -f $libDir/libarkmodloader.so" | Out-Null
    Say "`nГотово. Загрузчик удалён. Папка с модами ($ModsDir) оставлена." "Green"
    exit 0
}

# ------------------------------------------------------------------ install
Step "Готовлю файлы игры"
$work = Join-Path $env:TEMP ("arkml_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory $work | Out-Null
try {
    Su "cp $libDir/libgnustl_shared.so /data/local/tmp/gnustl.so && chmod 644 /data/local/tmp/gnustl.so" | Out-Null
    Adb pull /data/local/tmp/gnustl.so "$work\current.so" | Out-Null
    Su "rm /data/local/tmp/gnustl.so" | Out-Null
    if (-not (Test-Path "$work\current.so")) { Fail "Не удалось скопировать файл игры с эмулятора." }

    $alreadyPatched = Select-String -Path "$work\current.so" -Pattern "libarkmodloader.so" -SimpleMatch -Quiet
    if (-not $alreadyPatched -and -not (Test-Path $backupFile)) {
        New-Item -ItemType Directory (Split-Path $backupFile) -Force | Out-Null
        Copy-Item "$work\current.so" $backupFile
        Say "  резервная копия: $backupFile" "Green"
    }

    if ($alreadyPatched) {
        Say "  файл игры уже подготовлен (обновление)"
        Copy-Item "$work\current.so" "$work\patched.so"
    } else {
        # Patch on this PC with Python + LIEF (the game's file is never shipped with the installer).
        $vpy = Get-PatchPython
        & $vpy -I (Join-Path $PSScriptRoot "patch_needed.py") "$work\current.so" "$work\patched.so" "libarkmodloader.so"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path "$work\patched.so")) { Fail "Не удалось подготовить файл игры." }
    }

    Step "Устанавливаю загрузчик"
    Install-Lib (Join-Path $Root "files\libarkmodloader.so") "libarkmodloader.so"
    Install-Lib "$work\patched.so" "libgnustl_shared.so"
    $ls = Su "ls $libDir"
    if ($ls -notmatch "libarkmodloader") { Fail "Загрузчик не скопировался в папку игры." }
    Say "  загрузчик установлен" "Green"

    Step "Копирую моды"
    Adb shell mkdir -p $ModsDir | Out-Null
    foreach ($m in Get-ChildItem (Join-Path $Root "mods") -File) {
        Adb push $m.FullName "$ModsDir/$($m.Name)" | Out-Null
        Say "  $($m.Name)"
    }
} finally { Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue }

if ($NoLaunch) { Say "`nГотово." "Green"; exit 0 }

Step "Запускаю игру и проверяю загрузчик"
Adb logcat -c | Out-Null
Adb shell monkey -p $Pkg -c android.intent.category.LAUNCHER 1 | Out-Null
$ok = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep 3
    $log = Adb logcat -d -s "ArkModLoader:*"
    if ($log -match "watching ") { $ok = $true; break }
    if ((Adb logcat -d) -match "Fatal signal.*$Pkg") { break }
}
Start-Sleep 4
$log = Adb logcat -d -s "ArkModLoader:*"
if (-not $ok) {
    Say ($log -split "`n" | Select-Object -Last 15 | Out-String)
    Fail "Загрузчик не отозвался. Лог выше; удалить: uninstall.bat"
}
foreach ($l in ($log -split "`n")) {
    if ($l -match "ArkModLoader \d|loaded (lua )?mod|ERROR") { Say ("  " + ($l -replace "^.*ArkModLoader: ", "")) }
}
Say "`nГотово! В игре (одиночный режим) справа вверху появится кнопка MODS." "Green"
Say "Моды и их настройки: $ModsDir   (лог: loader.log)"
