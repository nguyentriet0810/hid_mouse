param(
    [ValidateSet('build','reconfigure','menuconfig','flash','monitor','size')]
    [string]$Action = 'build',
    [string]$Port = '',
    [string]$IdfPath = 'C:\Espressif\frameworks\esp-idf-v5.3.1',
    [string]$ToolsPath = 'C:\Espressif'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$idfPython = Join-Path $ToolsPath 'python_env\idf5.3_py3.11_env\Scripts\python.exe'
$idfGit = Join-Path $ToolsPath 'tools\idf-git\2.44.0\cmd'
if (!(Test-Path -LiteralPath $idfPython)) { throw "Missing IDF Python: $idfPython" }
if (!(Test-Path -LiteralPath (Join-Path $IdfPath 'export.ps1'))) { throw "Missing SDK: $IdfPath" }
$env:IDF_PATH = $IdfPath
$env:IDF_TOOLS_PATH = $ToolsPath
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:PATH = "$idfGit;$(Split-Path -Parent $idfPython);$env:PATH"
. (Join-Path $IdfPath 'export.ps1')
if ($LASTEXITCODE -ne 0) { throw 'ESP-IDF environment initialization failed.' }
$idfArguments = @('-C', $projectRoot)
if ($Port -ne '') { $idfArguments += @('-p', $Port) }
$idfArguments += $Action
& $idfPython (Join-Path $IdfPath 'tools\idf.py') @idfArguments
if ($LASTEXITCODE -ne 0) { throw "ESP-IDF $Action failed." }
