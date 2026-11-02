# Build Basaltc native Windows binaries (MinGW gcc)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Root

$env:Path = "C:\ProgramData\mingw64\mingw64\bin;C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64\bin;" + $env:Path
Get-Command gcc.exe | Format-Table -AutoSize

New-Item -ItemType Directory -Force -Path bin | Out-Null
New-Item -ItemType Directory -Force -Path payload\bin | Out-Null

$inc = "-Ibootstrap/stage0-c/include"
$stage0src = @(
  "bootstrap/stage0-c/src/main.c",
  "bootstrap/stage0-c/src/util.c",
  "bootstrap/stage0-c/src/lexer.c",
  "bootstrap/stage0-c/src/parser.c",
  "bootstrap/stage0-c/src/type.c",
  "bootstrap/stage0-c/src/sema.c",
  "bootstrap/stage0-c/src/codegen_c.c"
)

Write-Host "[1/4] stage0 (codegen_c only)..."
& gcc.exe -O1 -pipe -o bin/stage0.exe @stage0src $inc
if ($LASTEXITCODE -ne 0) { throw "stage0 failed" }

Write-Host "[2/4] runtime (Windows portable)..."
# float_fmt.c provides _n / _d helpers; runtime.c provides scalar pack/unpack — no name clash after rename
$rtSources = @(
  "runtime/runtime.c",
  "runtime/float_fmt.c",
  "runtime/arena.c",
  "runtime/accel.c"
)
$rtObjs = @()
foreach ($f in $rtSources) {
  if (-not (Test-Path $f)) { Write-Host "  skip missing $f"; continue }
  $o = "bin\" + [IO.Path]::GetFileNameWithoutExtension($f) + ".o"
  Write-Host "  cc $f"
  & gcc.exe -O2 -pipe -c -o $o $f
  if ($LASTEXITCODE -ne 0) { throw "compile $f failed" }
  $rtObjs += $o
}
if ($rtObjs.Count -eq 0) { throw "no runtime objects" }

Write-Host "  partial link basalt_rt.o"
& gcc.exe -r -nostdlib -o bin/basalt_rt.o @rtObjs 2>$null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path bin\basalt_rt.o)) {
  & ld.exe -r -o bin/basalt_rt.o @rtObjs
  if ($LASTEXITCODE -ne 0) {
    Write-Host "  ld -r failed — use runtime.o only as basalt_rt.o"
    Copy-Item -Force bin\runtime.o bin\basalt_rt.o
  }
}

Write-Host "[3/4] basaltc via stage0..."
$env:BASALT_HOME = $Root

$skipSelfHost = $false
if ((Test-Path bin\basaltc.exe) -and (Test-Path compiler\basaltc.bd)) {
  $exeTime = (Get-Item bin\basaltc.exe).LastWriteTime
  $srcTime = (Get-Item compiler\basaltc.bd).LastWriteTime
  if ($exeTime -ge $srcTime) {
    Write-Host "  bin\basaltc.exe is up-to-date — skip stage0 self-host"
    $skipSelfHost = $true
  }
}

if (-not $skipSelfHost) {
  # Clean stale codegen outputs
  @(
    "$env:TEMP\basaltc.bd.c",
    "basaltc.bd.c",
    "bin\basaltc.bd.c",
    ".\basaltc.bd.c",
    "bin\basaltc_tmp.exe"
  ) | ForEach-Object { if (Test-Path $_) { Remove-Item -Force $_ } }

  Write-Host "  running stage0 on compiler\basaltc.bd (timeout 25 min)..."
  $job = Start-Job -ScriptBlock {
    param($root)
    Set-Location $root
    $env:BASALT_HOME = $root
    & .\bin\stage0.exe compiler\basaltc.bd -o bin\basaltc_tmp.exe 2>&1
    return $LASTEXITCODE
  } -ArgumentList $Root

  $ok = Wait-Job $job -Timeout 1500
  if (-not $ok) {
    Stop-Job $job -ErrorAction SilentlyContinue
    Remove-Job $job -Force -ErrorAction SilentlyContinue
    throw "stage0 basaltc self-host TIMED OUT (25min)"
  }
  $out = Receive-Job $job
  $code = 0
  if ($out -is [int]) { $code = $out }
  Remove-Job $job -Force -ErrorAction SilentlyContinue
  Write-Host $out
  $outStr = ($out | Out-String)
  if ($outStr -match "gcc failed") {
    throw "stage0 internal gcc failed compiling generated C (see log above)"
  }

  # Prefer linking generated C if present
  $cfile = $null
  foreach ($cand in @(
    "$env:TEMP\basaltc.bd.c",
    "basaltc.bd.c",
    "bin\basaltc.bd.c",
    ".\basaltc.bd.c"
  )) {
    if (Test-Path $cand) { $cfile = $cand; break }
  }

  if ($null -ne $cfile) {
    Write-Host "  linking $cfile → bin\basaltc.exe"
    $linkArgs = @("-O1", "-pipe", "-o", "bin\basaltc.exe", $cfile)
    foreach ($o in @("bin\basalt_rt.o","bin\runtime.o","bin\float_fmt.o","bin\arena.o","bin\accel.o")) {
      if (Test-Path $o) { $linkArgs += $o }
    }
    Write-Host "  gcc $($linkArgs -join ' ')"
    & gcc.exe @linkArgs
    if ($LASTEXITCODE -ne 0) { throw "link basaltc.exe failed" }
  } elseif (Test-Path bin\basaltc_tmp.exe) {
    Move-Item -Force bin\basaltc_tmp.exe bin\basaltc.exe
  } elseif (-not (Test-Path bin\basaltc.exe)) {
    throw "basaltc.exe not produced (stage0 exit=$code, no .c and no tmp exe)"
  }
}

if (-not (Test-Path bin\basaltc.exe)) { throw "basaltc.exe missing" }

Write-Host "[4/4] payload..."
Copy-Item -Force bin\basaltc.exe payload\bin\
if (Test-Path bin\basm.exe) { Copy-Item -Force bin\basm.exe payload\bin\ }
elseif (Test-Path bin\basm) { Copy-Item -Force bin\basm payload\bin\basm.exe }
if (Test-Path bin\basalt_rt.o) { Copy-Item -Force bin\basalt_rt.o payload\bin\ }
if (Test-Path bin\pe_tool.exe) { Copy-Item -Force bin\pe_tool.exe payload\bin\ }
elseif (Test-Path bin\pe_tool) { Copy-Item -Force bin\pe_tool payload\bin\pe_tool.exe }

Write-Host "Windows native build OK"
Get-ChildItem payload\bin | Format-Table Name, Length
