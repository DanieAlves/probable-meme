# Somente leitura: quais handles o ReetFPS (e o ReetHub) tem abertos,
# com foco em handles de PROCESSO/THREAD apontando para o PointBlank.exe.
# RODAR COMO ADMINISTRADOR, com o jogo e o ReetFPS abertos.
$h   = Join-Path $PSScriptRoot 'tools\handle64.exe'
$out = Join-Path $PSScriptRoot 'handles.txt'
"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss')" | Set-Content $out
"PointBlank PIDs: $((Get-Process PointBlank -ErrorAction SilentlyContinue).Id -join ', ')" | Add-Content $out

foreach ($p in 'ReetFPS.exe', 'ReetHub.exe') {
    "`n===== handles de processo/thread em $p =====" | Add-Content $out
    & $h -accepteula -nobanner -a -p $p | Select-String -Pattern ':\s*(Process|Thread)\s' | ForEach-Object { $_.Line } | Add-Content $out
}

"`n===== qualquer processo com handle para PointBlank.exe =====" | Add-Content $out
& $h -accepteula -nobanner -a PointBlank.exe | Add-Content $out

"`n===== todos os handles do ReetFPS.exe (referencia) =====" | Add-Content $out
& $h -accepteula -nobanner -a -p ReetFPS.exe | Add-Content $out
"fim. Resultado em $out"
