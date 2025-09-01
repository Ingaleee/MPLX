# Build MPLX C API DLL with MSVC for .NET compatibility
Write-Host "=== Building MPLX C API DLL with MSVC ===" -ForegroundColor Green

# Set up MSVC environment
$vcvarsPath = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (Test-Path $vcvarsPath) {
    Write-Host "Found MSVC Build Tools 2019" -ForegroundColor Yellow
} else {
    Write-Host "MSVC Build Tools not found at: $vcvarsPath" -ForegroundColor Red
    Write-Host "Please install Visual Studio Build Tools 2019 or later" -ForegroundColor Red
    exit 1
}

# Create build directory
$buildDir = "build\msvc-dll"
if (!(Test-Path $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
}

# Compile C API with MSVC
Write-Host "Compiling C API with MSVC..." -ForegroundColor Yellow

$sourceFiles = @(
    "Infrastructure\mplx-capi\capi.cpp",
    "Application\mplx-compiler\compiler.cpp", 
    "Application\mplx-vm\vm.cpp",
    "Domain\mplx-lang\lexer.cpp",
    "Domain\mplx-lang\parser.cpp"
)

$includeDirs = @(
    "Infrastructure\mplx-capi",
    "Application\mplx-compiler",
    "Application\mplx-vm", 
    "Domain\mplx-lang"
)

$includeFlags = $includeDirs | ForEach-Object { "/I`"$_`"" }
$sourceFlags = $sourceFiles | ForEach-Object { "`"$_`"" }

# MSVC command - compile each file separately then link
Write-Host "Compiling source files..." -ForegroundColor Cyan

$env:Path = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\bin\Hostx64\x64;$env:Path"
$env:INCLUDE = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\include;$env:INCLUDE"
$env:LIB = "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Tools\MSVC\14.29.30133\lib\x64;$env:LIB"

# Compile each source file to object files
$objectFiles = @()
foreach ($sourceFile in $sourceFiles) {
    $objFile = "$buildDir\" + [System.IO.Path]::GetFileNameWithoutExtension($sourceFile) + ".obj"
    $objectFiles += $objFile
    
    $compileCmd = "cl.exe /c /std:c++20 $includeFlags `"$sourceFile`" /Fo:`"$objFile`""
    Write-Host "Compiling: $sourceFile" -ForegroundColor Gray
    & cmd /c $compileCmd
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Compilation failed for: $sourceFile" -ForegroundColor Red
        exit 1
    }
}

# Link object files into DLL
Write-Host "Linking DLL..." -ForegroundColor Cyan
$objFlags = $objectFiles | ForEach-Object { "`"$_`"" }
$linkCmd = "link.exe /DLL $objFlags /OUT:`"$buildDir\mplx_native.dll`""

& cmd /c $linkCmd

if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ DLL built successfully!" -ForegroundColor Green
    Write-Host "DLL location: $buildDir\mplx_native.dll" -ForegroundColor Green
    
    # Copy to .NET project
    Copy-Item "$buildDir\mplx_native.dll" "Infrastructure\Mplx.DotNet\" -Force
    Copy-Item "$buildDir\mplx_native.dll" "Presentation\Mplx.DotNet.Sample\" -Force
    
    Write-Host "✅ DLL copied to .NET projects" -ForegroundColor Green
} else {
    Write-Host "❌ DLL linking failed!" -ForegroundColor Red
    exit 1
}
