$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $here
try {
    $sdkDir = "C:\ps5-payload-sdk"
    $env:PATH = "C:\llvm-ps5\bin;$sdkDir\win;" + $env:PATH

    $toolExe = "$here\ps5\tooling\build\Release\ps5-native-tool.exe"
    $libcBuilderExe = "$here\ps5\tooling\build\Release\libc-builder.exe"

    if (-not (Test-Path $toolExe) -or -not (Test-Path $libcBuilderExe)) {
        Write-Host "[0/5] Compilando herramientas nativas de empaquetado PS5 (ps5-native-tool y libc-builder)..." -ForegroundColor Cyan
        cmake -B "ps5\tooling\build" -S "ps5\tooling"
        cmake --build "ps5\tooling\build" --config Release
    }

    New-Item -ItemType Directory -Path "ps5\obj" -Force | Out-Null
    New-Item -ItemType Directory -Path "ps5\stubs" -Force | Out-Null

    Write-Host "[1/5] Compilando stubs de sistema (libSceVideodec2.prx, libSceVideoOut.prx)..." -ForegroundColor Cyan
    & clang -target x86_64-sie-ps5 -fPIC -fvisibility=default -O2 -c "ps5\stubs\libSceVideodec2.c" -o "ps5\obj\libSceVideodec2.o"
    & ld.lld -m elf_x86_64 -shared -soname "libSceVideodec2.prx" -o "ps5\stubs\libSceVideodec2.so" "ps5\obj\libSceVideodec2.o"

    & clang -target x86_64-sie-ps5 -fPIC -fvisibility=default -O2 -c "ps5\stubs\libSceVideoOut.c" -o "ps5\obj\libSceVideoOut.o"
    & ld.lld -m elf_x86_64 -shared -soname "libSceVideoOut.prx" -o "ps5\stubs\libSceVideoOut.so" "ps5\obj\libSceVideoOut.o"

    Write-Host "[2/5] Compilando fuentes C++20 y runtime CRT de PS5..." -ForegroundColor Cyan
    & clang++ -target x86_64-sie-ps5 -isysroot "$sdkDir" -isystem "$sdkDir\target\include\c++\v1" -isystem "$sdkDir\target\include" `
        -std=c++20 -O2 -fPIC -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections `
        -c "ps5\tooling\app_crt.cpp" -o "ps5\obj\app_crt.o"

    & clang++ -target x86_64-sie-ps5 -isysroot "$sdkDir" -isystem "$sdkDir\target\include\c++\v1" -isystem "$sdkDir\target\include" `
        -std=c++20 -O2 -fPIC -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections `
        -c "ps5\tooling\app_cpp_runtime.cpp" -o "ps5\obj\app_cpp_runtime.o"

    Set-Content -Path "ps5\obj\libpthread.a" -Value "!<arch>`n" -Encoding Ascii -NoNewline

    $sources = @(
        "main",
        "util/crash_logger",
        "util/image_loader",
        "net/http_client",
        "auth/cbl_client",
        "session/luna_session",
        "input/pad_manager",
        "stream/webrtc_client",
        "display/font_inter_bold",
        "display/font_renderer",
        "display/display_manager",
        "audio/audio_manager",
        "media/media_decoder"
    )

    & clang -target x86_64-sie-ps5 -isysroot "$sdkDir" -isystem "$sdkDir\target\include" `
        -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections `
        -I"src" -c "src\util\qrcodegen.c" -o "ps5\obj\qrcodegen.o"
    if ($LASTEXITCODE -ne 0) { throw "Error compilando qrcodegen.c" }

    & clang -target x86_64-sie-ps5 -isysroot "$sdkDir" -isystem "$sdkDir\target\include" `
        -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections `
        -I"src" -c "src\platform\libc_compat.c" -o "ps5\obj\libc_compat.o"
    if ($LASTEXITCODE -ne 0) { throw "Error compilando libc_compat.c" }

    $objs = @("ps5\obj\app_crt.o", "ps5\obj\app_cpp_runtime.o", "ps5\obj\qrcodegen.o", "ps5\obj\libc_compat.o")
    foreach ($s in $sources) {
        $outName = $s.Replace("/", "_")
        $objPath = "ps5\obj\$outName.o"
        & clang++ -target x86_64-sie-ps5 `
            -isysroot "$sdkDir" `
            -isystem "$sdkDir\target\include\c++\v1" `
            -isystem "$sdkDir\target\include" `
            -std=c++20 -O2 -fPIC -Wall -Wextra `
            -fexceptions -fcxx-exceptions -frtti -fno-stack-protector -fno-plt -femulated-tls `
            -ffunction-sections -fdata-sections `
            -DLUNA_TARGET_PS5=1 `
            -I"src" `
            -I"$here\..\..\tools\reference\xbox-cloud-gaming-ps5\deps\libdatachannel\include" `
            -I"$here\..\..\tools\reference\xbox-cloud-gaming-ps5\deps\mbedtls\include" `
            -c "src\$s.cpp" -o $objPath
        if ($LASTEXITCODE -ne 0) { throw "Error compilando $s.cpp" }
        $objs += $objPath
    }

    Write-Host "[3/5] Enlazando imagen intermedia PIE y convirtiendo a modulo PS5 (0xFE10 + NIDs + ORBI)..." -ForegroundColor Cyan
    $sdkStubs = Get-ChildItem "$sdkDir\target\lib\*.so" | Where-Object { $_.Name -notmatch "WebKit|mono|VSH" } | ForEach-Object { $_.FullName }

    $webrtcLibs = @(
        "$here\ps5\lib\libdatachannel-static.a",
        "$here\ps5\lib\libjuice-static.a",
        "$here\ps5\lib\libsrtp2.a",
        "$here\ps5\lib\libusrsctp.a",
        "$here\ps5\lib\libmbedtls.a",
        "$here\ps5\lib\libmbedx509.a",
        "$here\ps5\lib\libmbedcrypto.a"
    )

    & ld.lld `
        -m elf_x86_64 -pie `
        -T "ps5\tooling\ps5-pie.ld" `
        --eh-frame-hdr `
        --version-script "ps5\tooling\app-symbols.map" `
        --exclude-libs=ALL `
        -z max-page-size=0x4000 `
        -L "ps5\obj" `
        -e _start `
        -o "ps5\obj\llvm-pie.elf" `
        $objs `
        --start-group $webrtcLibs --end-group `
        "$sdkDir\target\lib\libc++.a" `
        "$sdkDir\target\lib\libc++abi.a" `
        "$sdkDir\target\lib\libunwind.a" `
        "ps5\stubs\libSceVideodec2.so" `
        "ps5\stubs\libSceVideoOut.so" `
        --as-needed $sdkStubs
    if ($LASTEXITCODE -ne 0) { throw "Error en ld.lld PIE" }

    & $toolExe link `
        --in "ps5\obj\llvm-pie.elf" `
        --out "ps5\obj\eboot.elf" `
        --stub-dir "$sdkDir\target\lib" `
        --stub "ps5\stubs\libSceVideodec2.so" `
        --stub "ps5\stubs\libSceVideoOut.so" `
        --module-sdk 0x02000009 `
        --companion-sdk 0x08050001 `
        --file-name eboot.elf
    if ($LASTEXITCODE -ne 0) { throw "Error en ps5-native-tool link" }

    Write-Host "[4/5] Generando y firmando contenedor FSELF (eboot.bin y sce_module/libc.prx)..." -ForegroundColor Cyan
    $appRoot = "$here\..\..\PPSA99825"
    Remove-Item -Recurse -Force "$here\dist" -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $appRoot -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path "$appRoot\sce_sys" -Force | Out-Null
    New-Item -ItemType Directory -Path "$appRoot\sce_module" -Force | Out-Null

    & $toolExe self --sign `
        --in "ps5\obj\eboot.elf" `
        --out "$appRoot\eboot.bin" `
        --magic 0x1D3D154F
    if ($LASTEXITCODE -ne 0) { throw "Error firmando eboot.bin" }

    & $libcBuilderExe "ps5\tooling\api-surface.txt" "ps5\tooling\imports.txt" "ps5\obj\libc.elf"
    & $toolExe self --sign `
        --in "ps5\obj\libc.elf" `
        --out "$appRoot\sce_module\libc.prx" `
        --magic 0x1D3D154F
    if ($LASTEXITCODE -ne 0) { throw "Error firmando libc.prx" }

    Copy-Item "ps5\sce_sys\param.json" "$appRoot\sce_sys\param.json" -Force
    Copy-Item "ps5\sce_sys\icon0.png" "$appRoot\sce_sys\icon0.png" -Force

    Write-Host "[5/5] Verificando integridad FSELF de los modulos y auditoria de simbolos..." -ForegroundColor Cyan
    & $toolExe self --inspect --file "$appRoot\eboot.bin"
    & $toolExe self --inspect --file "$appRoot\sce_module\libc.prx"

    & python "$here\..\..\scratch\check_syscalls.py"
    if ($LASTEXITCODE -ne 0) { throw "Fallo en la verificacion de syscalls (0f 05)" }

    & python "$here\..\..\scratch\verify_all_imports.py"
    if ($LASTEXITCODE -ne 0) { throw "Fallo en la auditoria estricta de imports dinamicos" }

    Write-Host "`nPaquete nativo PS5 generado con exito en: $(Resolve-Path $appRoot)" -ForegroundColor Green
}
finally {
    Pop-Location
}
