# Paths of the local test scripts, dot-sourced by them (. "$PSScriptRoot\t6_env.ps1"):
#   $root = the project folder (the parent of tools\), $py = Python (T6_PYTHON, else the python on PATH)
$root = Split-Path -Parent $PSScriptRoot
$py = if ($env:T6_PYTHON) { $env:T6_PYTHON } else { (Get-Command python -ErrorAction SilentlyContinue).Source }
