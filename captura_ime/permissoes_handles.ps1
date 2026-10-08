# Somente leitura: permissoes (granted access) dos handles de PROCESSO e
# THREAD que o ReetFPS.exe tem para o PointBlank.exe.
# RODAR COMO ADMINISTRADOR, com o jogo e o ReetFPS abertos.
$h   = Join-Path $PSScriptRoot 'tools\handle64.exe'
$out = Join-Path $PSScriptRoot 'permissoes.txt'
$csv = Join-Path $PSScriptRoot 'handles_reetfps.csv'

& $h -accepteula -nobanner -a -g -v -p ReetFPS.exe | Set-Content $csv
$rows = Import-Csv $csv | Where-Object { $_.Type -in 'Process', 'Thread' }

"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss')  PointBlank PIDs: $((Get-Process PointBlank -ErrorAction SilentlyContinue).Id -join ', ')" | Set-Content $out

"`n===== handles de PROCESSO (alvo e permissoes) =====" | Add-Content $out
$rows | Where-Object Type -eq 'Process' | ForEach-Object { "$($_.Handle)  $($_.Name)`n      => $($_.'Access Mask')" } | Add-Content $out

"`n===== handles de THREAD para o jogo, agrupados por permissao =====" | Add-Content $out
$rows | Where-Object { $_.Type -eq 'Thread' -and $_.Name -notmatch 'ReetFPS\.exe' } |
    Group-Object 'Access Mask' | Sort-Object Count -Descending |
    ForEach-Object { "{0,5} handles  => {1}" -f $_.Count, $_.Name } | Add-Content $out

"`n===== handles de THREAD do proprio ReetFPS, agrupados por permissao =====" | Add-Content $out
$rows | Where-Object { $_.Type -eq 'Thread' -and $_.Name -match 'ReetFPS\.exe' } |
    Group-Object 'Access Mask' | Sort-Object Count -Descending |
    ForEach-Object { "{0,5} handles  => {1}" -f $_.Count, $_.Name } | Add-Content $out
"fim. Resultado em $out"
