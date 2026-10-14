param([Parameter(Mandatory=$true)][string]$KeyDirectory)
$ErrorActionPreference = 'Stop'
$target = [IO.Path]::GetFullPath($KeyDirectory)
New-Item -ItemType Directory -Path $target -Force | Out-Null
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User
& icacls.exe $target '/inheritance:r' '/grant:r' "*$($identity.Value):(OI)(CI)F" | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Could not protect private-key directory' }
# Python's Windows mkdir(mode=0700) can create explicit Administrators entries.
# Remove these from the new temporary directory before any plaintext is written.
foreach ($rule in (Get-Acl -LiteralPath $target).Access) {
    $sid = $rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value
    if ($sid -ne $identity.Value) {
        & icacls.exe $target '/remove' "*$sid" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw 'Could not remove an extra private-directory access rule' }
    }
}
$acl = Get-Acl -LiteralPath $target
foreach ($rule in $acl.Access) {
    if ($rule.IdentityReference.Translate([Security.Principal.SecurityIdentifier]).Value -ne $identity.Value) {
        throw 'Private-key directory still grants another account access; no key may be written'
    }
}
Write-Output 'Private-key directory restricted to the current Windows account.'
