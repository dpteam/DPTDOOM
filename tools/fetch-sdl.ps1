# Official prebuilt development packages, local to build/deps (never installed
# system-wide). SHA-256 values are GitHub release asset digests.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$destination = Join-Path $root 'build/deps'
New-Item -ItemType Directory -Force $destination | Out-Null

$packages = @(
    @{
        Name = 'SDL2-devel-2.32.10-VC.zip'
        Url = 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-VC.zip'
        Hash = 'af347939395a58b365846aaea27391e69f9ec9d4dd650d6ac40802159b418a6e'
    },
    @{
        Name = 'SDL2_mixer-devel-2.8.2-VC.zip'
        Url = 'https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.2/SDL2_mixer-devel-2.8.2-VC.zip'
        Hash = '13d7def40e7e3400c930c0d45df5b14e70c5ca75817232812f24b6e85577b37b'
    }
)

foreach ($package in $packages) {
    $archive = Join-Path $destination $package.Name
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest $package.Url -OutFile $archive
    }
    if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne $package.Hash) {
        throw "SHA-256 mismatch: $archive. Inspect/remove this archive before retrying."
    }
    Expand-Archive -Path $archive -DestinationPath $destination -Force
    Write-Host "Verified and extracted $($package.Name)"
}
