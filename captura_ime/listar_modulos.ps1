# Somente leitura: lista os modulos carregados no PointBlank.exe e no
# ReetFPS.exe e procura modulos "estranhos" (window.ime, ReetFPS.dll, *.ime,
# DLLs fora do Windows/pasta do jogo). RODAR COMO ADMINISTRADOR.
# Se o anti-cheat negar o acesso, isso tambem fica registrado.
$out = Join-Path $PSScriptRoot 'modulos.txt'

# Processos 32-bit precisam ser lidos por um PowerShell 32-bit.
if ([Environment]::Is64BitProcess) {
    & "$env:WINDIR\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath
    return
}

$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss') admin=$admin 32bit=$(-not [Environment]::Is64BitProcess)" | Set-Content $out

foreach ($p in Get-Process PointBlank, ReetFPS, ReetHub -ErrorAction SilentlyContinue) {
    "`n===== $($p.ProcessName) PID $($p.Id) inicio $($p.StartTime) =====" | Add-Content $out
    try {
        $mods = $p.Modules
        "modulos: $($mods.Count)" | Add-Content $out
        $suspeitos = $mods | Where-Object {
            $_.FileName -notmatch '^C:\\WINDOWS\\' -or $_.ModuleName -match '\.ime$|reet|window'
        }
        "--- fora do Windows ou suspeitos ---" | Add-Content $out
        $suspeitos | Select-Object ModuleName, FileName, @{n='Base';e={'0x{0:X8}' -f [int64]$_.BaseAddress}}, ModuleMemorySize |
            Format-Table -AutoSize | Out-String -Width 400 | Add-Content $out
        "--- todos ---" | Add-Content $out
        $mods | Select-Object ModuleName, FileName, @{n='Base';e={'0x{0:X8}' -f [int64]$_.BaseAddress}}, ModuleMemorySize |
            Format-Table -AutoSize | Out-String -Width 400 | Add-Content $out
    } catch {
        "ERRO: $($_.Exception.Message)" | Add-Content $out
    }
}

"`n===== quem tem ReetFPS.dll / window.ime carregado (todos os processos 32-bit acessiveis) =====" | Add-Content $out
foreach ($p in Get-Process) {
    try {
        foreach ($m in $p.Modules) {
            if ($m.ModuleName -match 'reetfps\.dll|window\.ime|\.ime$') {
                "$($p.ProcessName) PID $($p.Id): $($m.FileName)" | Add-Content $out
            }
        }
    } catch { }
}
"`nfim. Resultado em $out"
