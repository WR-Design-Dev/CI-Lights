# Regression: Windows PowerShell 5.1 must select one new workflow run from gh JSON.
# Load only the pure selection function; never build, commit, push or contact GitHub.
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$scriptPath = Join-Path $PSScriptRoot '../tools/publish_firmware.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Publishing script has syntax errors.' }
$function = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Find-NewWorkflowRun'
}, $true)
if (-not $function) { throw 'Workflow selection function is missing.' }
Invoke-Expression $function.Extent.Text

$head = 'a' * 40
$otherHead = 'b' * 40
$runs = @(
    @{databaseId = 500; headSha = $otherHead; url = 'other-commit'},
    @{databaseId = 400; headSha = $head; url = 'new-run'},
    @{databaseId = 300; headSha = $head; url = 'previous-run'}
)
$selected = Find-NewWorkflowRun (ConvertTo-Json -InputObject $runs -Compress) $head @(300)
if (@($selected).Count -ne 1 -or $selected.databaseId -ne 400 -or $selected.url -ne 'new-run') {
    throw 'Must select exactly one fresh run for the pushed commit.'
}
if ($null -ne (Find-NewWorkflowRun (ConvertTo-Json -InputObject $runs -Compress) $head @(300, 400))) {
    throw 'Already known runs must not be selected again.'
}
if ($null -ne (Find-NewWorkflowRun '[]' $head @())) {
    throw 'An empty run list must keep waiting.'
}
$single = ConvertTo-Json -InputObject @($runs[1]) -Compress
if ((Find-NewWorkflowRun $single $head @()).databaseId -ne 400) {
    throw 'A single-record JSON array must also select correctly.'
}
Write-Host 'Publishing script checks passed: exact commit, fresh run, one result and empty-list handling.'
