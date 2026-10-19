# Pinned official NuGet SDK, extracted locally; does not install a runtime.
$ErrorActionPreference = 'Stop'
$destination = Join-Path (Split-Path $PSScriptRoot -Parent) 'build/deps'
New-Item -ItemType Directory -Force $destination | Out-Null
$version = '1.0.3537.50'
$archive = Join-Path $destination "webview2-$version.nupkg"
if (-not (Test-Path $archive)) {
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" -OutFile $archive
}
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '5ea526bbd728adda0da4d31219267e96460494a427e4894c4e09d9f320f4b9aa') {
    throw "SHA-256 mismatch: $archive. Inspect/remove before retrying."
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$target = Join-Path $destination 'webview2'
New-Item -ItemType Directory -Force $target | Out-Null
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $path = [IO.Path]::GetFullPath((Join-Path $target $entry.FullName))
        if (-not $path.StartsWith([IO.Path]::GetFullPath($target) + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid archive path' }
        New-Item -ItemType Directory -Force (Split-Path $path -Parent) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $true)
    }
} finally { $zip.Dispose() }
Write-Host "Verified WebView2 SDK $version (runtime not installed)"
