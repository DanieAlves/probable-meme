# Observa System32 e SysWOW64 e copia qualquer *.ime criado/alterado
# (alvo: window.ime, gravado pela ReetFPS.dll so durante a injecao).
# Nao toca no processo do jogo. Fica FORA do %TEMP% porque a abertura do
# jogo (2026-10-07 08:20) esvaziou o %TEMP%.
$dest = $PSScriptRoot
$log  = Join-Path $dest 'log.txt'
"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss.fff') watcher iniciado (PID $PID)" | Add-Content $log

$acao = {
    $path = $Event.SourceEventArgs.FullPath
    $dest = $Event.MessageData
    $log  = Join-Path $dest 'log.txt'
    "$(Get-Date -f HH:mm:ss.fff) evento $($Event.SourceEventArgs.ChangeType) $path" | Add-Content $log
    for ($i = 0; $i -lt 400; $i++) {
        try {
            $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite, Delete')
            $ms = New-Object IO.MemoryStream; $fs.CopyTo($ms); $fs.Close()
            if ($ms.Length -gt 0) {
                $out = Join-Path $dest ("{0}_{1:HHmmss_fff}_{2}.bin" -f [IO.Path]::GetFileName($path), (Get-Date), $ms.Length)
                [IO.File]::WriteAllBytes($out, $ms.ToArray())
                "$(Get-Date -f HH:mm:ss.fff) COPIADO -> $out sha256=$((Get-FileHash $out).Hash)" | Add-Content $log
                return
            }
        } catch { $erro = $_.Exception.Message }
        Start-Sleep -Milliseconds 5
    }
    "$(Get-Date -f HH:mm:ss.fff) FALHOU $path ($erro)" | Add-Content $log
}

foreach ($d in "$env:WINDIR\System32", "$env:WINDIR\SysWOW64") {
    $w = New-Object IO.FileSystemWatcher $d, '*.ime'
    $w.NotifyFilter = 'FileName, LastWrite, Size'
    $w.InternalBufferSize = 65536
    foreach ($ev in 'Created', 'Changed', 'Renamed') {
        Register-ObjectEvent $w $ev -Action $acao -MessageData $dest | Out-Null
    }
    $w.EnableRaisingEvents = $true
}
"Observando System32 e SysWOW64 -> $dest"
while ($true) { Start-Sleep 1 }
