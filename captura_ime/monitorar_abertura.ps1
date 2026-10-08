# Somente leitura: durante a ABERTURA do jogo, tira fotos repetidas dos
# handles do ReetFPS.exe e registra toda permissao nova que aparecer para o
# PointBlank.exe (processo ou thread). Pega handles que existem so por
# instantes (ex.: escrita de memoria na inicializacao).
# RODAR COMO ADMINISTRADOR, com o ReetFPS aberto e o jogo FECHADO; depois de
# "pronto", abra o jogo. Para sozinho apos $segundos.
param([int]$segundos = 150)
$h   = Join-Path $PSScriptRoot 'tools\handle64.exe'
$out = Join-Path $PSScriptRoot 'abertura.txt'
"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss.fff') inicio, PointBlank aberto: $([bool](Get-Process PointBlank -ErrorAction SilentlyContinue))" | Set-Content $out
& $h -accepteula -nobanner -p ReetFPS.exe | Out-Null

$vistos = @{}
$fim = (Get-Date).AddSeconds($segundos)
"pronto: abra o Point Blank agora (monitorando por $segundos s)"
while ((Get-Date) -lt $fim) {
    $t = Get-Date -f 'HH:mm:ss.fff'
    $rows = & $h -nobanner -a -g -v -p ReetFPS.exe | ConvertFrom-Csv |
        Where-Object { $_.Type -in 'Process', 'Thread' -and $_.Name -match 'PointBlank' }
    $proc = @($rows | Where-Object Type -eq 'Process')
    $thr  = @($rows | Where-Object Type -eq 'Thread')
    foreach ($r in $proc) {
        $k = "P|$($r.Handle)|$($r.Name)|$($r.'Access Mask')"
        if (-not $vistos[$k]) { $vistos[$k] = 1; "$t  NOVO handle de processo $($r.Handle) -> $($r.Name)  [$($r.'Access Mask')]" | Add-Content $out }
    }
    foreach ($g in $thr | Group-Object 'Access Mask') {
        $k = "T|$($g.Name)"
        if (-not $vistos[$k]) { $vistos[$k] = 1; "$t  NOVA permissao de thread [$($g.Name)]" | Add-Content $out }
    }
    "$t  processo=$($proc.Count) threads=$($thr.Count)" | Add-Content $out
}
"$(Get-Date -f 'HH:mm:ss.fff') fim" | Add-Content $out
"fim. Resultado em $out"
