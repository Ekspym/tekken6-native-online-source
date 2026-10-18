# Runs net_x360.ps1 once and compares both instances per 30-frame window (sound call results hash, LCG, CRT rand):
# prints the number of windows, how many differ and the first differing one.
param([string]$Name = 'c1', [int]$Seconds = 110, [int]$Char1 = 3, [int]$Char2 = 33, [int]$Stage = 5, [string]$PageHash = '')
. "$PSScriptRoot\t6_env.ps1"
$r = "$root\runs"
if ($PageHash) { $env:T6_NET_PAGEHASH = $PageHash } else { $env:T6_NET_PAGEHASH = $null }
& "$root\net_x360.ps1" -Name $Name -Rec menu -Seconds $Seconds -Char1 $Char1 -Char2 $Char2 -Stage $Stage 2>$null | Out-Null
$env:T6_NET_PAGEHASH = $null
# keyed by frame over the rotated log too (tekken6.1.log = older part); window 0 = before the session, not compared
$rows = @{}
foreach ($p in 1, 2) {
    $rows[$p] = @{}
    foreach ($log in "$r\${Name}_p$p\tekken6.1.log", "$r\${Name}_p$p\tekken6.log") {
        if (-not (Test-Path $log)) { continue }
        Select-String -Path $log -Pattern 'frame ([0-9]+) sound results ([0-9A-F]+) \(([0-9]+) calls\) LCG ([0-9A-F]+) CRT ([0-9A-F]+)' |
            ForEach-Object { $m = $_.Matches[0].Groups; $rows[$p][[int]$m[1].Value] = "$($m[1].Value) $($m[2].Value) $($m[3].Value) $($m[4].Value) $($m[5].Value)" }
    }
}
$a = $rows[1]; $b = $rows[2]; $d = 0; $first = ''
foreach ($k in ($a.Keys | Sort-Object)) {
    if ($k -eq 0 -or -not $b.ContainsKey($k)) { continue }
    if ($a[$k] -ne $b[$k]) { if (-not $first) { $first = "$($a[$k]) | $($b[$k])" }; $d++ }
}
"${Name}: windows $($a.Count)/$($b.Count), differing $d $first"
foreach ($p in 1, 2) {
    Select-String -Path "$r\${Name}_p$p\tekken6.log" -Pattern 'T6 crash: host|loader still|not done after' | Select-Object -First 2 |
        ForEach-Object { "  p${p}: " + $_.Line.Substring(60, [Math]::Min(110, $_.Line.Length - 60)) }
}
# Desync criterion proper: fight player objects + third RNG per confirmed frame (the main LCG = stage particles, cosmetic)
& $py "$root\tools\state_diff.py" "$r\${Name}_p1\fight_state.csv" "$r\${Name}_p2\fight_state.csv" |
    Select-String -Pattern 'common|players|rng3' | ForEach-Object { "  " + $_.Line.Trim() }
