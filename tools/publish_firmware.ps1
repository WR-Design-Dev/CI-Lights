<#
Windows-Helfer fuer publish.bat; Voraussetzungen und Beispiele stehen dort.
Keine Passwoerter oder Schluessel eintragen: gh nutzt seine gespeicherte
Anmeldung, GitHub Actions den bereits eingerichteten Environment-Secret.

Standard: Browser-Tests, lokaler signierter ESP-IDF-Build, Signatur-/Pakettests,
lokales Paket unter dist/local, Commit/Push nach main und GitHub-Build abwarten.
GitHub baut erneut aus dem Commit und veroeffentlicht seine eigene CI-Version.
-SkipLocalBuild: lokale Browser-Tests und GitHub-Build ohne lokalen OTA-Key/SDK.
-CheckOnly: Voraussetzungen pruefen, nichts bauen oder veroeffentlichen.
-Yes: Commit-Bestaetigung fuer ausdruecklich gewollte Automatisierung auslassen.

Ein leerer Git-Index ist erforderlich; bereits vorgemerkte Aenderungen zuerst
selbst committen oder gezielt aus dem Index nehmen. main darf eigene neue
Commits enthalten, muss aber den aktuellen origin/main-Stand enthalten.
Bei einem veralteten Branch zuerst selbst git pull --ff-only ausfuehren.
Es gibt keinen Force-Push, keine Schluesselerzeugung und kein Hardware-Flashing.
Nach einem CI-Fehler zeigt das Skript den Lauf-Link und beendet sich mit Fehler.
#>
[CmdletBinding()]
param(
    [string]$Message = '',
    [string]$IdfPath = '',
    [switch]$SkipLocalBuild,
    [switch]$CheckOnly,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
$repository = 'WR-Design-Dev/CI-Lights'
$workflow = 'firmware.yml'
$originalLocation = Get-Location

function Invoke-Checked {
    param([string]$Program, [string[]]$CommandArguments)
    & $Program @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Befehl fehlgeschlagen (Exit $LASTEXITCODE): $Program $($CommandArguments -join ' ')"
    }
}

function Get-Checked {
    param([string]$Program, [string[]]$CommandArguments)
    $output = & $Program @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Befehl fehlgeschlagen (Exit $LASTEXITCODE): $Program $($CommandArguments -join ' ')"
    }
    return ($output -join "`n").Trim()
}

try {
    $git = (Get-Command git.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    # Zuerst Git-Root bestimmen, dann den Workspace-Vorrang anwenden.
    $root = Get-Checked $git @('-C', $PSScriptRoot, 'rev-parse', '--show-toplevel')
    if (Test-Path -LiteralPath (Join-Path $root 'Workspace') -PathType Container) {
        $root = Join-Path $root 'Workspace'
    }
    Set-Location -LiteralPath $root
    if (-not (Test-Path -LiteralPath 'CMakeLists.txt')) {
        throw "Kein CI-Lights-Projekt in $root gefunden."
    }
    $branch = Get-Checked $git @('branch', '--show-current')
    if ($branch -ne 'main') { throw 'Veroeffentlichung ist nur auf Branch main erlaubt.' }
    $remote = Get-Checked $git @('remote', 'get-url', 'origin')
    if ($remote -notmatch '^(https://github\.com/|git@github\.com:|ssh://git@github\.com/)WR-Design-Dev/CI-Lights(?:\.git)?/?$') {
        throw 'origin muss auf WR-Design-Dev/CI-Lights zeigen.'
    }
    $staged = Get-Checked $git @('diff', '--cached', '--name-only')
    if ($staged) { throw 'Der Git-Index ist nicht leer. Vorgemerkte Aenderungen zuerst selbst bearbeiten.' }
    foreach ($state in @('MERGE_HEAD', 'CHERRY_PICK_HEAD', 'REVERT_HEAD', 'rebase-merge', 'rebase-apply')) {
        $statePath = Get-Checked $git @('rev-parse', '--git-path', $state)
        if (Test-Path -LiteralPath $statePath) { throw "Zuerst den laufenden Git-Vorgang abschliessen: $state" }
    }

    $node = (Get-Command node.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $nodeVersion = Get-Checked $node @('--version')
    if ([int]($nodeVersion.TrimStart('v').Split('.')[0]) -lt 24) {
        throw 'Node.js 24 oder neuer installieren.'
    }
    $ghCommand = Get-Command gh.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($ghCommand) { $gh = $ghCommand.Source }
    else {
        $gh = Join-Path $root 'build\github-cli\bin\gh.exe'
        if (-not (Test-Path -LiteralPath $gh -PathType Leaf)) {
            throw 'GitHub CLI installieren und gh auth login ausfuehren (siehe publish.bat).'
        }
    }
    $login = Get-Checked $gh @('api', 'user', '--jq', '.login')
    if ($login -ne 'WR-Design-Dev') {
        throw 'Mit dem GitHub-Konto WR-Design-Dev anmelden: gh auth switch --user WR-Design-Dev'
    }
    Write-Host "Projekt: $root"
    Write-Host "GitHub: $repository ($login), Node $nodeVersion"

    if (-not $SkipLocalBuild) {
        if (-not (Test-Path -LiteralPath 'secrets\ota_signing_key.pem' -PathType Leaf)) {
            throw 'Originalen OTA-Key aus Backup nach secrets\ota_signing_key.pem kopieren oder -SkipLocalBuild verwenden.'
        }
        if (-not $IdfPath) { $IdfPath = $env:IDF_PATH }
        if (-not $IdfPath -and (Test-Path -LiteralPath '.vscode\settings.json')) {
            $settings = Get-Content -LiteralPath '.vscode\settings.json' -Raw | ConvertFrom-Json
            $IdfPath = $settings.'idf.currentSetup'
        }
        if (-not $IdfPath) { $IdfPath = 'C:\esp\v6.1\esp-idf' }
        if (-not (Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1') -PathType Leaf)) {
            throw 'ESP-IDF 6.1 fehlt. Installation mit -IdfPath angeben (siehe publish.bat).'
        }
        Write-Host "ESP-IDF: $IdfPath; vorhandener lokaler OTA-Key wird verwendet."
    }
    if ($CheckOnly) {
        Write-Host 'Voraussetzungen erfuellt. Kein Build, Commit oder Push ausgefuehrt.'
        exit 0
    }

    $env:GIT_TERMINAL_PROMPT = '0'
    # Nur fuer diese Befehle gh als Credential-Helfer nutzen, keine globale Git-Konfiguration aendern.
    $ghShellPath = $gh.Replace('\', '/').Replace("'", "'\''")
    $gitAuth = @('-c', 'credential.helper=', '-c', "credential.helper=!'$ghShellPath' auth git-credential")
    Invoke-Checked $git ($gitAuth + @('fetch', 'origin', 'main'))
    & $git merge-base --is-ancestor origin/main HEAD
    if ($LASTEXITCODE -ne 0) {
        throw 'main ist veraltet oder verzweigt. Zuerst git pull --ff-only ausfuehren und Konflikte selbst klaeren.'
    }

    Write-Host "`nBrowser-Tests ..."
    Invoke-Checked $node @('tests/test_ota_ui.js')
    Invoke-Checked $node @('tests/test_web_flasher.mjs')

    if (-not $SkipLocalBuild) {
        # EIM kennt seine eigenen Werkzeugpfade; sein passendes Aktivierungsskript verwenden.
        $activationScript = $null
        $eimFiles = @('C:\Espressif\tools\eim_idf.json')
        if ($env:IDF_TOOLS_PATH) { $eimFiles = @((Join-Path $env:IDF_TOOLS_PATH 'eim_idf.json')) + $eimFiles }
        foreach ($eimFile in $eimFiles) {
            if (Test-Path -LiteralPath $eimFile) {
                $eim = Get-Content -LiteralPath $eimFile -Raw | ConvertFrom-Json
                $installation = $eim.idfInstalled | Where-Object {
                    $_.path.TrimEnd('\', '/').Replace('/', '\') -eq $IdfPath.TrimEnd('\', '/').Replace('/', '\')
                } | Select-Object -First 1
                if ($installation -and (Test-Path -LiteralPath $installation.activationScript -PathType Leaf)) {
                    $activationScript = $installation.activationScript
                    break
                }
            }
        }
        if ($activationScript) {
            . $activationScript
        } else {
            if (-not $env:IDF_TOOLS_PATH -and (Test-Path -LiteralPath 'C:\Espressif')) {
                $env:IDF_TOOLS_PATH = 'C:\Espressif'
            }
            if ($env:IDF_PYTHON_ENV_PATH) {
                $env:PATH = (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts') + ';' + $env:PATH
            }
            # Klassischer ESP-IDF-Installer: offizielles Exportskript verwenden.
            . (Join-Path $IdfPath 'export.ps1')
        }
        $env:IDF_TARGET = 'esp32s3'
        $python = (Get-Command python.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
        $idfTool = Join-Path $IdfPath 'tools\idf.py'
        $idfVersion = Get-Checked $python @($idfTool, '--version')
        if ($idfVersion -notmatch 'ESP-IDF v6\.1(?:[.\s-]|$)') {
            throw "ESP-IDF 6.1 erforderlich; gefunden: $idfVersion"
        }
        # --generate wird nur bei bereits vorhandenem Key aufgerufen: Identitaet pruefen und Key wiederverwenden.
        if (-not (Test-Path -LiteralPath 'secrets\ota_signing_key.pem' -PathType Leaf)) {
            throw 'Lokaler OTA-Key fehlt; Build abgebrochen.'
        }
        Invoke-Checked $python @('tools/prepare_ota_key.py', '--generate')
        $env:CI_LIGHTS_RELEASE_VERSION = $null
        Write-Host "`nLokaler signierter Firmware-Build ..."
        Invoke-Checked $python @($idfTool, 'build')
        Invoke-Checked $python @('-m', 'espsecure', 'verify-signature', '--version', '2', '--keyfile', 'secrets/ota_signing_key.pem', 'build/CI-Lights.bin')
        Invoke-Checked $python @('-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py')
        Invoke-Checked $python @('tools/package_firmware.py', '--output-dir', 'dist/local', '--repository', $repository)
    }

    Invoke-Checked $git @('-c', 'core.whitespace=cr-at-eol', 'diff', '--check')
    # Build, dist, secrets und Backups sind per .gitignore ausgeschlossen; COM-Port bleibt lokal.
    Invoke-Checked $git @('add', '--all', '--', '.', ':(exclude).vscode/settings.json')
    # Auch versehentlich in andere Dateien kopierte PEM-Privatschluessel nie veroeffentlichen.
    $privateFiles = & $git grep --cached -I -l -E -e '-----BEGIN ([A-Z0-9]+ )?PRIVATE KEY-----' -- .
    if ($LASTEXITCODE -eq 0) {
        throw "Private Schluessel im Git-Index erkannt. Kein Commit/Push. Dateien aus dem Index nehmen: $($privateFiles -join ', ')"
    }
    if ($LASTEXITCODE -ne 1) { throw 'Pruefung des Git-Index auf private Schluessel fehlgeschlagen.' }
    Invoke-Checked $git @('-c', 'core.whitespace=cr-at-eol', 'diff', '--cached', '--check')
    $staged = Get-Checked $git @('diff', '--cached', '--name-only')
    if ($staged) {
        Write-Host "`nDiese Aenderungen werden veroeffentlicht:"
        Invoke-Checked $git @('diff', '--cached', '--stat')
        if (-not $Yes -and (Read-Host 'Commit und Veroeffentlichung bestaetigen: JA eingeben') -cne 'JA') {
            Write-Host 'Abgebrochen. Dateien bleiben zur Pruefung im Git-Index; nichts hochgeladen.'
            exit 0
        }
        if (-not $Message) {
            if (-not $Yes) { $Message = Read-Host 'Commit-Nachricht (leer = Update CI-Lights)' }
            if (-not $Message.Trim()) { $Message = 'Update CI-Lights' }
        }
        Invoke-Checked $git @('commit', '-m', $Message)
    }

    $head = Get-Checked $git @('rev-parse', 'HEAD')
    $ahead = [int](Get-Checked $git @('rev-list', '--count', 'origin/main..HEAD'))
    $previousRuns = @(ConvertFrom-Json (Get-Checked $gh @('run', 'list', '--repo', $repository, '--workflow', $workflow, '--limit', '20', '--json', 'databaseId')))
    $previousIds = @($previousRuns | ForEach-Object { $_.databaseId })
    if ($ahead -gt 0) {
        Invoke-Checked $git ($gitAuth + @('push', 'origin', 'main'))
        $event = 'push'
    } else {
        Write-Host 'Keine neuen Commits. Neuer GitHub-Build wird fuer den aktuellen Stand gestartet.'
        Invoke-Checked $gh @('workflow', 'run', $workflow, '--repo', $repository, '--ref', 'main')
        $event = 'workflow_dispatch'
    }
    $run = $null
    $deadline = [DateTime]::UtcNow.AddMinutes(2)
    do {
        $runs = @(ConvertFrom-Json (Get-Checked $gh @('run', 'list', '--repo', $repository, '--workflow', $workflow, '--branch', 'main', '--event', $event, '--limit', '20', '--json', 'databaseId,headSha,url')))
        $run = $runs | Where-Object { $_.headSha -eq $head -and $_.databaseId -notin $previousIds } | Select-Object -First 1
        if (-not $run) { Start-Sleep -Seconds 5 }
    } while (-not $run -and [DateTime]::UtcNow -lt $deadline)
    if (-not $run) { throw "GitHub-Lauf noch nicht gefunden. Status pruefen: https://github.com/$repository/actions" }
    Write-Host "`nGitHub baut, signiert und veroeffentlicht: $($run.url)"
    Invoke-Checked $gh @('run', 'watch', [string]$run.databaseId, '--repo', $repository, '--interval', '15', '--exit-status')
    Write-Host "`nErfolgreich veroeffentlicht."
    Write-Host "Firmware: https://github.com/$repository/releases/latest"
    Write-Host 'Flash-Seite: https://wr-design-dev.github.io/CI-Lights/'
} catch {
    Write-Host "`nFEHLER: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
} finally {
    Set-Location -LiteralPath $originalLocation.Path
}
