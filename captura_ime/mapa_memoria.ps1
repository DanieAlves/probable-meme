# Somente CONSULTA: percorre o mapa de memoria do PointBlank.exe com
# VirtualQueryEx (pedindo so PROCESS_QUERY_INFORMATION) e lista as regioes
# EXECUTAVEIS que nao pertencem a nenhum arquivo (memoria privada, ou mapeada
# sem nome). NAO le o conteudo da memoria.
# RODAR COMO ADMINISTRADOR, com o jogo aberto (de preferencia ja no lobby).
param([string]$alvo = 'PointBlank', [int64]$limite = 0x100000000, [string]$saida = 'memoria.txt')
$out = Join-Path $PSScriptRoot $saida

Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class Mapa {
    [StructLayout(LayoutKind.Sequential)]
    public struct MBI { public IntPtr BaseAddress, AllocationBase; public uint AllocationProtect; public ushort PartitionId;
                        public IntPtr RegionSize; public uint State, Protect, Type; }
    [DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint a, bool i, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr a, out MBI m, IntPtr len);
    [DllImport("psapi.dll", CharSet=CharSet.Unicode)] public static extern uint GetMappedFileNameW(IntPtr h, IntPtr a, StringBuilder s, uint n);
    public static string Arquivo(IntPtr h, IntPtr a) { var sb = new StringBuilder(1024); return GetMappedFileNameW(h, a, sb, 1024) > 0 ? sb.ToString() : ""; }
}
'@

$exec = 0x10, 0x20, 0x40, 0x80   # EXECUTE, EXECUTE_READ, EXECUTE_READWRITE, EXECUTE_WRITECOPY
function Prot($p) { switch ($p -band 0xFF) { 0x01 {'NOACCESS'} 0x02 {'R'} 0x04 {'RW'} 0x08 {'WC'} 0x10 {'X'} 0x20 {'RX'} 0x40 {'RWX'} 0x80 {'WCX'} default {'0x{0:X}' -f $p} } }
function Tipo($t) { switch ($t) { 0x20000 {'PRIVATE'} 0x40000 {'MAPPED'} 0x1000000 {'IMAGE'} default {'?'} } }

"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss')  admin=$(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators'))" | Set-Content $out

foreach ($p in Get-Process $alvo -ErrorAction SilentlyContinue) {
    "`n===== $($p.ProcessName) PID $($p.Id) (inicio $($p.StartTime)) =====" | Add-Content $out
    $h = [Mapa]::OpenProcess(0x400, $false, $p.Id)   # PROCESS_QUERY_INFORMATION
    if ($h -eq [IntPtr]::Zero) { "ERRO OpenProcess: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" | Add-Content $out; continue }

    $regioes = New-Object System.Collections.Generic.List[object]
    $addr = [IntPtr]::Zero; $mbi = New-Object Mapa+MBI
    $sz = [IntPtr][Runtime.InteropServices.Marshal]::SizeOf([type][Mapa+MBI])
    while ([Mapa]::VirtualQueryEx($h, $addr, [ref]$mbi, $sz) -ne [IntPtr]::Zero) {
        if ($mbi.State -eq 0x1000) {   # MEM_COMMIT
            $regioes.Add([pscustomobject]@{ Base=[int64]$mbi.BaseAddress; Alloc=[int64]$mbi.AllocationBase; Tam=[int64]$mbi.RegionSize
                Prot=$mbi.Protect; AllocProt=$mbi.AllocationProtect; Tipo=$mbi.Type })
        }
        $next = [int64]$mbi.BaseAddress + [int64]$mbi.RegionSize
        if ($next -ge $limite -or $next -le [int64]$addr) { break }
        $addr = [IntPtr]$next
    }

    $totExec = @($regioes | Where-Object { $exec -contains ($_.Prot -band 0xFF) })
    "regioes commitadas: $($regioes.Count); executaveis: $($totExec.Count)" | Add-Content $out

    # Executaveis IMAGE: resumo por arquivo (para conferir que todo codigo de imagem tem arquivo)
    "--- codigo de IMAGEM por arquivo ---" | Add-Content $out
    $totExec | Where-Object { $_.Tipo -eq 0x1000000 } | Group-Object Alloc | ForEach-Object {
        $f = [Mapa]::Arquivo($h, [IntPtr][int64]$_.Name)
        "{0,-12} {1,10:N0} bytes  {2}" -f ('0x{0:X8}' -f [int64]$_.Name), ($_.Group | Measure-Object Tam -Sum).Sum, $(if ($f) { $f } else { '<SEM ARQUIVO>' })
    } | Sort-Object | Add-Content $out

    # Executaveis fora de imagem: o que interessa
    "--- codigo FORA de imagem (PRIVATE/MAPPED), agrupado por alocacao ---" | Add-Content $out
    $suspeitas = $totExec | Where-Object { $_.Tipo -ne 0x1000000 } | Select-Object -ExpandProperty Alloc -Unique
    foreach ($a in $suspeitas) {
        $partes = @($regioes | Where-Object Alloc -eq $a | Sort-Object Base)
        $total  = ($partes | Measure-Object Tam -Sum).Sum
        $layout = ($partes | ForEach-Object { "{0}:{1:N0}" -f (Prot $_.Prot), $_.Tam }) -join ' '
        $mix    = @($partes | Where-Object { $exec -notcontains ($_.Prot -band 0xFF) }).Count -gt 0
        $codigo = ($partes | Where-Object { $exec -contains ($_.Prot -band 0xFF) } | Measure-Object Tam -Sum).Sum
        $grande = $total -ge 0x20000 -and $codigo -ge 0x10000
        $f      = if ($partes[0].Tipo -eq 0x40000) { [Mapa]::Arquivo($h, [IntPtr][int64]$a) } else { '' }
        "0x{0:X8}  {1}  alocado {2}  total {3,10:N0} bytes  codigo {8,10:N0}  partes {4}{9}{5}{6}`n      layout: {7}" -f $a, (Tipo $partes[0].Tipo), (Prot $partes[0].AllocProt), $total, $partes.Count,
            $(if ($mix) { '  [MISTO: codigo + dados, layout tipo PE]' } else { '' }), $(if ($f) { "  arquivo: $f" } else { '' }), $layout, $codigo, $(if ($grande) { '  [GRANDE]' } else { '' }) | Add-Content $out
    }
    [void][Mapa]::CloseHandle($h)
}
"fim. Resultado em $out"



