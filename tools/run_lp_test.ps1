# Run the log-pairing self-test. Offline: no game launch, no build needed.
# ⭐ This guards the helper that five wrong readings in one session paid for --
# see the header of tools/logpair.py.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
python "$root\tools\test_logpair.py"
exit $LASTEXITCODE
