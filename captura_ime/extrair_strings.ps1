# Extrai strings do bloco da ReetFPS.dll capturado em memoria.
# Metodos:
#   RAW_ASCII  - sequencias ASCII diretamente legiveis (todas as secoes)
#   RAW_WIDE   - strings Unicode UTF-16LE
#   KNOWN_RAW  - strings conhecidas da versao antiga, busca direta
#   KNOWN_XOR1 - idem, cifradas (1o byte = chave) com todas as chaves possiveis
# NOTA: XOR1 geral requer Ghidra (bytes nao sao contiguos no codigo compilado).
# Saida: strings_dll_nova.txt
param([string]$bin_path = '')

$dir = $PSScriptRoot
if (-not $bin_path) {
    $bin_path = Get-ChildItem $dir 'bloco_PID*_0x*.bin' |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $bin_path -or -not (Test-Path $bin_path)) { "ERRO: nao achei .bin em $dir"; exit 1 }
"lendo $([IO.Path]::GetFileName($bin_path)) ..."
$bin = [IO.File]::ReadAllBytes($bin_path)
$out = Join-Path $dir 'strings_dll_nova.txt'

# --- helper C# (busca de sequencia de bytes, muito mais rapido que loop PS) ---
Add-Type @'
using System;
using System.Collections.Generic;
public static class Busca {
    public static int[] Achar(byte[] hay, byte[] needle) {
        var r = new List<int>();
        if (needle.Length == 0 || hay.Length < needle.Length) return r.ToArray();
        byte first = needle[0];
        for (int i = 0; i <= hay.Length - needle.Length; i++) {
            if (hay[i] != first) continue;
            bool ok = true;
            for (int j = 1; j < needle.Length; j++) {
                if (hay[i+j] != needle[j]) { ok = false; break; }
            }
            if (ok) r.Add(i);
        }
        return r.ToArray();
    }
}
'@

# Secoes no arquivo (imagem mapeada: RVA == offset no .bin)
$secs = [ordered]@{
    '.text'    = [pscustomobject]@{ off=[int]0x001000; end=[int]0x0FF200 }
    '.rdata'   = [pscustomobject]@{ off=[int]0x100000; end=[int]0x14FA00 }
    '.data'    = [pscustomobject]@{ off=[int]0x150000; end=[int]0x1AC800 }
    '.fptable' = [pscustomobject]@{ off=[int]0x1AF000; end=[int]0x1AF200 }
}
function SecName([int]$off) {
    foreach ($n in $secs.Keys) {
        $s = $secs[$n]
        if ($off -ge $s.off -and $off -lt $s.end) { return $n }
    }
    return '?'
}

$results = [System.Collections.Generic.List[string]]::new()
function Adicionar([string]$s) { [void]$results.Add($s) }

# ── 1. RAW ASCII (min 5 chars, todas as secoes) ───────────────────────────────
"[1/4] RAW ASCII..."
$sb = [System.Text.StringBuilder]::new()
$soff = 0
for ($i = 0; $i -lt $bin.Length; $i++) {
    $b = $bin[$i]
    if ($b -ge 0x20 -and $b -le 0x7E) {
        if ($sb.Length -eq 0) { $soff = $i }
        [void]$sb.Append([char]$b)
    } else {
        if ($sb.Length -ge 5) {
            Adicionar ("RAW_ASCII   rva=0x{0:X6}  sec={1,-8}  {2}" -f $soff, (SecName $soff), $sb.ToString())
        }
        [void]$sb.Clear()
    }
}
if ($sb.Length -ge 5) {
    Adicionar ("RAW_ASCII   rva=0x{0:X6}  sec={1,-8}  {2}" -f $soff, (SecName $soff), $sb.ToString())
}

# ── 2. RAW Unicode UTF-16LE (min 4 chars) ────────────────────────────────────
"[2/4] RAW Unicode..."
$sb = [System.Text.StringBuilder]::new()
$soff = 0
$i = 0
while ($i -lt $bin.Length - 1) {
    $lo = $bin[$i]; $hi = $bin[$i+1]
    if ($hi -eq 0 -and $lo -ge 0x20 -and $lo -le 0x7E) {
        if ($sb.Length -eq 0) { $soff = $i }
        [void]$sb.Append([char]$lo)
        $i += 2
    } else {
        if ($sb.Length -ge 4) {
            Adicionar ("RAW_WIDE    rva=0x{0:X6}  sec={1,-8}  {2}" -f $soff, (SecName $soff), $sb.ToString())
        }
        [void]$sb.Clear()
        $i += if ($hi -ne 0) { 1 } else { 2 }
    }
}

# ── 3. KNOWN_RAW: strings da versao antiga, busca direta (C#) ─────────────────
"[3/4] KNOWN raw..."
$known = @(
    'PointBlank.exe'
    'Waiting process'
    'RESTART PROCESS AGAIN!'
    'GetProcessInformation->Failed to get process informations'
    '[ INIT ] GetProcessInformation->OK!'
    '[ INIT ] GetProcessInformation->ProcessInfo.hWND->0x%X'
    'Failed to load library, ID: 0x%d / Message: %s'
    '[ INIT ] Init->Sucess!'
    '[ INIT ] GetFile->FAIL!'
    'stub_path->%s'
    'File not found->%s'
    '[ INIT ] AUTH_CHECK->OK!'
    '[ INIT ] AUTH_CHECK->FAIL!'
    'window.ime'
    'WindowMsg'
    '1482301'
    'Control Panel\Desktop\Colors\'
    'Keyboard Layout\Preload'
)
foreach ($plain in $known) {
    $needle = [System.Text.Encoding]::ASCII.GetBytes($plain)
    $hits   = [Busca]::Achar($bin, $needle)
    foreach ($pos in $hits) {
        Adicionar ("KNOWN_RAW   rva=0x{0:X6}  sec={1,-8}  '{2}'" -f $pos, (SecName $pos), $plain)
    }
}

# ── 4. KNOWN_XOR1: idem, cifradas com cada chave possivel (C# para velocidade) ─
"[4/4] KNOWN XOR1 (255 chaves * 18 strings)..."
foreach ($plain in $known) {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($plain)
    for ($key = 1; $key -le 0xFE; $key++) {
        $enc = [byte[]]::new($bytes.Length)
        for ($j = 0; $j -lt $bytes.Length; $j++) { $enc[$j] = [byte]($bytes[$j] -bxor $key) }
        $hits = [Busca]::Achar($bin, $enc)
        foreach ($pos in $hits) {
            # verifica se o byte imediatamente anterior e a chave (padrao da DLL antiga)
            $k_antes = ($pos -gt 0 -and $bin[$pos - 1] -eq $key)
            Adicionar ("KNOWN_XOR1  rva=0x{0:X6}  sec={1,-8}  key=0x{2:X2}  chave_antes={3,-5}  '{4}'" -f $pos, (SecName $pos), $key, $k_antes, $plain)
        }
    }
}

# ── Salvar e resumir ──────────────────────────────────────────────────────────
$sorted = $results | Sort-Object
$header = @(
    "arquivo: $([IO.Path]::GetFileName($bin_path))"
    "data:    $(Get-Date -f 'yyyy-MM-dd HH:mm:ss')"
    "total:   $($results.Count) entradas  (raw_ascii=$(($results | Where-Object {$_ -like 'RAW_ASCII*'}).Count)  raw_wide=$(($results | Where-Object {$_ -like 'RAW_WIDE*'}).Count)  known_raw=$(($results | Where-Object {$_ -like 'KNOWN_RAW*'}).Count)  known_xor1=$(($results | Where-Object {$_ -like 'KNOWN_XOR1*'}).Count))"
    ""
    "NOTA: KNOWN_XOR1 = bytes cifrados encontrados contiguos no binario. Na DLL"
    "antiga os bytes sao atribuidos individualmente na pilha (nao sao contiguos);"
    "portanto KNOWN_XOR1 com chave_antes=False pode indicar coincidencia. Usar"
    "Ghidra para confirmacao das strings cifradas no codigo."
    ""
)
($header + $sorted) | Set-Content $out -Encoding UTF8
"fim: $($results.Count) strings salvas em $out"
