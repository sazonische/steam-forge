param(
	[Parameter(Mandatory = $true)]
	[string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'

$dumpbinOutput = & dumpbin /nologo /dependents $ExecutablePath
if ($LASTEXITCODE -ne 0) {
	throw "dumpbin failed with exit code $LASTEXITCODE"
}

$dependencyNames = @()
foreach ($outputLine in $dumpbinOutput) {
	if ($outputLine -match '^\s+(\S+\.dll)\s*$') {
		$dependencyNames += $Matches[1]
	}
}

Write-Host "Imported DLLs of ${ExecutablePath}:"
foreach ($dependencyName in $dependencyNames) {
	Write-Host "  $dependencyName"
}

$forbiddenPattern = '^(Qt6.*|VCRUNTIME.*|MSVCP.*|api-ms-win-crt-.*|ucrtbase.*|concrt.*|vcomp.*|libcrypto.*|libssl.*)\.dll$'
$systemDirectory = [Environment]::SystemDirectory
$unexpectedDependencies = @()

foreach ($dependencyName in $dependencyNames) {
	if ($dependencyName -match $forbiddenPattern) {
		$unexpectedDependencies += $dependencyName
		continue
	}
	# API sets are resolved by the Windows loader; the CRT set is rejected above.
	if ($dependencyName -match '^(api-ms-win-|ext-ms-)') {
		continue
	}
	if (-not (Test-Path (Join-Path $systemDirectory $dependencyName))) {
		$unexpectedDependencies += $dependencyName
	}
}

if ($unexpectedDependencies.Count -gt 0) {
	throw "Unexpected DLL dependencies: $($unexpectedDependencies -join ', ')"
}

Write-Host 'Dependency check passed.'
