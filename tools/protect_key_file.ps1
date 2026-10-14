param([Parameter(Mandatory=$true)][string]$KeyFile)
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($KeyFile)
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw 'Private-key file is missing' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
& icacls.exe $target '/inheritance:r' '/grant:r' "*$($identity.Value):F" | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not protect private-key file' }
foreach ($rule in (Get-Acl -LiteralPath $target).Access) {
    if ($rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -ne $identity.Value) {
        throw 'Private-key file still grants another account access'
    }
}
