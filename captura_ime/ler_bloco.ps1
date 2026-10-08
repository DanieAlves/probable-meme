# Lê o bloco de ~1.8 MB de código privado que o ReetFPS injeta no PointBlank.
# Precisa de PROCESS_VM_READ além de PROCESS_QUERY_INFORMATION.
# RODAR COMO ADMINISTRADOR, com o jogo E o ReetFPS abertos.
# NAO modifica nada. Apenas lê e salva em disco.
param([string]$saida = '')

$dir = $PSScriptRoot

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MemLer {
    [StructLayout(LayoutKind.Sequential)]
    public struct MBI {
        public IntPtr BaseAddress, AllocationBase;
        public uint AllocationProtect; public ushort PartitionId;
        public IntPtr RegionSize; public uint State, Protect, Type;
    }
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")]
    public static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MBI mbi, IntPtr sz);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr h, IntPtr baseAddr, byte[] buf, IntPtr nBytes, out IntPtr read);
}
'@

$exec       = @(0x10, 0x20, 0x40, 0x80)
$limite     = [int64]0x100000000
$MEM_COMMIT = 0x1000
$PRIVATE    = 0x20000
$NOACCESS   = 0x01
$QUERY_VM   = 0x400 -bor 0x10   # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ

$log = Join-Path $dir 'bloco_info.txt'
"$(Get-Date -f 'yyyy-MM-dd HH:mm:ss')  admin=$(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators'))" | Set-Content $log

$encontrou = $false

foreach ($p in Get-Process PointBlank -ErrorAction SilentlyContinue) {
    "===== PID $($p.Id)  inicio $($p.StartTime) =====" | Add-Content $log
    $h = [MemLer]::OpenProcess($QUERY_VM, $false, $p.Id)
    if ($h -eq [IntPtr]::Zero) {
        "  ERRO OpenProcess: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" | Add-Content $log
        continue
    }

    # --- coletar mapa ---
    $regioes = [System.Collections.Generic.List[object]]::new()
    $addr = [IntPtr]::Zero
    $mbi  = [MemLer+MBI]::new()
    $sz   = [IntPtr][Runtime.InteropServices.Marshal]::SizeOf([type][MemLer+MBI])
    while ([MemLer]::VirtualQueryEx($h, $addr, [ref]$mbi, $sz) -ne [IntPtr]::Zero) {
        if ($mbi.State -eq $MEM_COMMIT) {
            $regioes.Add([pscustomobject]@{
                Base      = [int64]$mbi.BaseAddress
                Alloc     = [int64]$mbi.AllocationBase
                Tam       = [int64]$mbi.RegionSize
                Prot      = $mbi.Protect
                AllocProt = $mbi.AllocationProtect
                Tipo      = $mbi.Type
            })
        }
        $next = [int64]$mbi.BaseAddress + [int64]$mbi.RegionSize
        if ($next -ge $limite -or $next -le [int64]$addr) { break }
        $addr = [IntPtr]$next
    }

    # --- localizar bloco: PRIVATE ~1.8 MB quase inteiro executável ---
    $suspeitas = $regioes | Where-Object Tipo -eq $PRIVATE | Select-Object -ExpandProperty Alloc -Unique
    foreach ($a in $suspeitas) {
        $partes = @($regioes | Where-Object { $_.Alloc -eq $a -and $_.Tipo -eq $PRIVATE } | Sort-Object Base)
        $total  = ($partes | Measure-Object Tam -Sum).Sum
        $codigo = ($partes | Where-Object { $exec -contains ($_.Prot -band 0xFF) } | Measure-Object Tam -Sum).Sum
        # filtro: 1.6–2.2 MB, >90% executável
        if ($total -lt 0x190000 -or $total -gt 0x230000) { continue }
        if ($codigo -lt ($total * 0.90)) { continue }

        "  bloco 0x{0:X8}  total {1:N0}  codigo {2:N0}  partes {3}" -f $a, $total, $codigo, $partes.Count | Add-Content $log
        $encontrou = $true

        # --- ler ---
        $buf = [byte[]]::new($total)
        $ofs = 0
        foreach ($parte in $partes) {
            $n = [int]$parte.Tam
            if (($parte.Prot -band 0xFF) -eq $NOACCESS) {
                "    0x{0:X8}  NOACCESS  {1} bytes (zerada)" -f $parte.Base, $n | Add-Content $log
                $ofs += $n; continue
            }
            $chunk = [byte[]]::new($n)
            $read  = [IntPtr]::Zero
            if ([MemLer]::ReadProcessMemory($h, [IntPtr]$parte.Base, $chunk, [IntPtr]$n, [ref]$read)) {
                [Buffer]::BlockCopy($chunk, 0, $buf, $ofs, [int]$read)
                "    0x{0:X8}  lido {1:N0} bytes" -f $parte.Base, [int64]$read | Add-Content $log
            } else {
                "    0x{0:X8}  ERRO {1}" -f $parte.Base, [Runtime.InteropServices.Marshal]::GetLastWin32Error() | Add-Content $log
            }
            $ofs += $n
        }

        # --- salvar ---
        $nome = if ($saida) { Join-Path $dir $saida } else { Join-Path $dir ("bloco_PID{0}_0x{1:X8}.bin" -f $p.Id, $a) }
        [IO.File]::WriteAllBytes($nome, $buf)
        "  -> salvo em $nome" | Add-Content $log

        # --- inspecionar cabecalho PE ---
        "  --- cabecalho ---" | Add-Content $log
        if ($buf.Length -ge 2 -and $buf[0] -eq 0x4D -and $buf[1] -eq 0x5A) {
            "  assinatura: MZ (PE valido)" | Add-Content $log
            $pe_off = [BitConverter]::ToInt32($buf, 0x3C)
            "  e_lfanew: 0x{0:X}" -f $pe_off | Add-Content $log
            if ($pe_off -gt 0 -and ($pe_off + 24) -lt $buf.Length) {
                $sig4 = [Text.Encoding]::ASCII.GetString($buf, $pe_off, 4)
                "  PE sig: $sig4 ($(if($sig4 -eq "PE`0`0"){'OK'}else{'INVALIDO'}))" | Add-Content $log
                if ($sig4 -eq "PE`0`0") {
                    $machine     = [BitConverter]::ToUInt16($buf, $pe_off + 4)
                    $num_sec     = [BitConverter]::ToUInt16($buf, $pe_off + 6)
                    $ts          = [BitConverter]::ToUInt32($buf, $pe_off + 8)
                    $opt_sz      = [BitConverter]::ToUInt16($buf, $pe_off + 20)
                    $arch        = switch ($machine) { 0x014C {'x86'} 0x8664 {'x64'} default {'0x{0:X4}' -f $machine} }
                    "  Machine: $arch" | Add-Content $log
                    "  Secoes: $num_sec" | Add-Content $log
                    $ts_dt = [DateTimeOffset]::FromUnixTimeSeconds($ts).ToLocalTime()
                    "  Timestamp: 0x{0:X8}  ({1})" -f $ts, $ts_dt | Add-Content $log
                    # OptionalHeader: SizeOfImage, ImageBase, Subsystem
                    $opt_base = $pe_off + 24
                    if ($opt_sz -ge 60 -and ($opt_base + $opt_sz) -lt $buf.Length) {
                        $magic = [BitConverter]::ToUInt16($buf, $opt_base)
                        "  OptHdr magic: 0x{0:X4} ($(if($magic -eq 0x10B){'PE32'}elseif($magic -eq 0x20B){'PE32+'}else{'?'}))" -f $magic | Add-Content $log
                        if ($magic -eq 0x10B) {   # PE32
                            $image_base  = [BitConverter]::ToUInt32($buf, $opt_base + 28)
                            $size_img    = [BitConverter]::ToUInt32($buf, $opt_base + 56)
                            $subsystem   = [BitConverter]::ToUInt16($buf, $opt_base + 68)
                            "  ImageBase: 0x{0:X8}" -f $image_base | Add-Content $log
                            "  SizeOfImage: 0x{0:X8}  ({1:N0} bytes)" -f $size_img, $size_img | Add-Content $log
                            "  Subsystem: $subsystem ($(if($subsystem -eq 2){'GUI'}elseif($subsystem -eq 3){'CUI/DLL'}else{'?'}))" | Add-Content $log
                        }
                        # Nomes das secoes
                        $sec_base = $opt_base + $opt_sz
                        "  Secoes:" | Add-Content $log
                        for ($i = 0; $i -lt $num_sec; $i++) {
                            $s = $sec_base + $i * 40
                            if ($s + 40 -gt $buf.Length) { break }
                            $nome_sec = [Text.Encoding]::ASCII.GetString($buf, $s, 8).TrimEnd("`0")
                            $virt_sz  = [BitConverter]::ToUInt32($buf, $s + 16)
                            $virt_rva = [BitConverter]::ToUInt32($buf, $s + 12)
                            $flags    = [BitConverter]::ToUInt32($buf, $s + 36)
                            "    [{0}]  RVA 0x{1:X8}  vsize 0x{2:X8}  flags 0x{3:X8}" -f $nome_sec, $virt_rva, $virt_sz, $flags | Add-Content $log
                        }
                    }
                }
            }
        } else {
            "  assinatura: NAO e MZ (bytes: 0x{0:X2} 0x{1:X2})" -f $buf[0], $buf[1] | Add-Content $log
        }
    }
    [void][MemLer]::CloseHandle($h)
}

if (-not $encontrou) { "NENHUM bloco encontrado. O jogo esta aberto com o ReetFPS?" | Add-Content $log }
Get-Content $log
"fim. Log completo em $log"
