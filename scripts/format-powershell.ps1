param(
    [Parameter(Mandatory)]
    [string]$ManifestPath,
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$localModule = Join-Path $root 'build/format-tools/powershell/PSScriptAnalyzer/1.25.0/PSScriptAnalyzer.psd1'
if (Test-Path -LiteralPath $localModule) {
    Import-Module $localModule -Force
}
else {
    Import-Module PSScriptAnalyzer -RequiredVersion 1.25.0 -Force
}
$settings = Get-Content -Raw -LiteralPath (Join-Path $root '.powershell-format.json') | ConvertFrom-Json -AsHashtable
$files = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
$failures = 0

function Get-SignificantTokens([string]$Source, [string]$Name) {
    $tokens = $null
    $parseErrors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseInput($Source, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -gt 0) {
        throw "Cannot format invalid PowerShell: $Name ($($parseErrors[0].Message))"
    }
    return @($tokens | Where-Object {
            $_.Kind -notin @('NewLine', 'Comment', 'EndOfInput', 'LineContinuation')
        } | ForEach-Object { "$($_.Kind):$($_.Extent.Text.Replace("`r`n", "`n"))" })
}

foreach ($relative in $files) {
    $file = Join-Path $root $relative
    $original = [System.IO.File]::ReadAllText($file)
    $formatted = Invoke-Formatter -ScriptDefinition $original.Replace("`r`n", "`n") -Settings $settings
    $formatted = $formatted.Replace("`r`n", "`n").TrimEnd() + "`n"
    $before = Get-SignificantTokens $original $relative
    $after = Get-SignificantTokens $formatted $relative
    if (($before | ConvertTo-Json -Compress) -cne ($after | ConvertTo-Json -Compress)) {
        throw "Formatter changed PowerShell tokens: $relative"
    }
    if ($formatted -cne $original) {
        if ($Check) {
            [Console]::Error.WriteLine("Formatting differs: $relative")
            $failures++
        }
        else {
            [System.IO.File]::WriteAllText($file, $formatted, [System.Text.UTF8Encoding]::new($false))
        }
    }
}
if ($failures -gt 0) {
    exit 1
}
