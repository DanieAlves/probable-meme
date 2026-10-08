# Busca no bin da ReetFPS.dll capturada as strings relacionadas a
# MAPLOADING (Mapas Instantaneos) e INTERFACEDELAY (Interface Sem Delay).
# Tambem busca FONTE_PERSONALIZADA como referencia.
#
# Metodos:
#   RAW_ASCII  - sequencias ASCII diretamente em qualquer offset
#   RAW_WIDE   - strings UTF-16LE
#   KNOWN_RAW  - chaves/valores conhecidos, busca direta
#   KNOWN_XOR1 - idem, cifradas com [key_byte_antes][enc_bytes][00]
#
# Saida: buscar_features_resultado.txt
param([string]$bin_path = '')

$dir = $PSScriptRoot
if (-not $bin_path) {
    $bin_path = Get-ChildItem $dir 'bloco_PID*_0x*.bin' |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1 -ExpandProperty FullName
}
if (-not $bin_path -or -not (Test-Path $bin_path)) {
    "ERRO: nao achei .bin em $dir"; exit 1
}
"lendo $([IO.Path]::GetFileName($bin_path)) ..."
$bin = [IO.File]::ReadAllBytes($bin_path)
$out = Join-Path $dir 'buscar_features_resultado.txt'

# ── helper C# para busca de sequencia ────────────────────────────────────────
Add-Type @'
using System; using System.Collections.Generic;
public static class BF {
    public static int[] Find(byte[] hay, byte[] needle) {
        var r = new List<int>();
        if (needle.Length == 0 || hay.Length < needle.Length) return r.ToArray();
        byte first = needle[0];
        for (int i = 0; i <= hay.Length - needle.Length; i++) {
            if (hay[i] != first) continue;
            bool ok = true;
            for (int j = 1; j < needle.Length; j++)
                if (hay[i+j] != needle[j]) { ok = false; break; }
            if (ok) r.Add(i);
        }
        return r.ToArray();
    }
}
'@

# ── mapa de secoes (RVA = offset no arquivo) ──────────────────────────────────
$secs = [ordered]@{
    '.text'    = [pscustomobject]@{ off=0x001000; fim=0x0FF200 }
    '.rdata'   = [pscustomobject]@{ off=0x100000; fim=0x14FA00 }
    '.data'    = [pscustomobject]@{ off=0x150000; fim=0x1AC800 }
    '.fptable' = [pscustomobject]@{ off=0x1AF000; fim=0x1AF200 }
}
function SecName([int]$off) {
    foreach ($n in $secs.Keys) {
        $s = $secs[$n]
        if ($off -ge $s.off -and $off -lt $s.fim) { return $n }
    }; return '?'
}

$results = [System.Collections.Generic.List[string]]::new()
function Add-Result([string]$s) { [void]$results.Add($s) }

# ── 1. STRINGS CONHECIDAS — busca RAW ────────────────────────────────────────
"[1/4] RAW direto..."

# Chaves JSON que a DLL leria para saber o que ativar
$known_keys = @(
    # MAPLOADING
    'LOADINGMAP'
    'LOADINGMAP_ACTIVE'
    # INTERFACEDELAY
    'INTERFACE'
    'SET_INTERFACE'
    'SET_INTERFACE2'
    'INTERFACE_ACTIVE'
    # FONTE_PERSONALIZADA
    'FONTE_PERSONALIZADA'
    'FONTE_PERSONALIZADA_VALUE'
    'Font.ini'
    'Locale'
    'bahnschrift'
    # Geral / JSON
    'ACTIVE'
    'ReetFPS_CFG'
    'Configuration file'
    # Possiveis strings de efeito
    'loading'
    'Loading'
    'LOADING'
    'lobby'
    'Lobby'
    'LOBBY'
    'interface'
    'Interface'
    'delay'
    'Delay'
    'map'
    'font'
    'Font'
    'FONT'
    # Possiveis API / hooks
    'WaitForSingleObject'
    'ReadFile'
    'CreateFile'
    'SetWindowPos'
    'ShowWindow'
    'SendMessage'
    'PostMessage'
    'GetProcAddress'
    'LoadLibrary'
)

foreach ($plain in $known_keys) {
    # ASCII
    $needle = [System.Text.Encoding]::ASCII.GetBytes($plain)
    $hits = [BF]::Find($bin, $needle)
    foreach ($pos in $hits) {
        # extrai contexto: 8 bytes antes + string + ate 24 bytes depois
        $ctx_start = [Math]::Max(0, $pos - 8)
        $ctx_end   = [Math]::Min($bin.Length - 1, $pos + $needle.Length + 24)
        $after = ''
        for ($k = $pos + $needle.Length; $k -le $ctx_end; $k++) {
            $b = $bin[$k]
            if ($b -ge 0x20 -and $b -le 0x7E) { $after += [char]$b } else { break }
        }
        Add-Result ("RAW_ASCII  rva=0x{0:X6}  sec={1,-8}  '{2}'{3}" -f
            $pos, (SecName $pos), $plain,
            $(if ($after) { "  +>'{0}'" -f $after } else { '' }))
    }
    # UTF-16LE
    $needle_w = [System.Text.Encoding]::Unicode.GetBytes($plain)
    $hits_w   = [BF]::Find($bin, $needle_w)
    foreach ($pos in $hits_w) {
        Add-Result ("RAW_WIDE   rva=0x{0:X6}  sec={1,-8}  '{2}'" -f
            $pos, (SecName $pos), $plain)
    }
}

# ── 2. SCAN ASCII GERAL em .rdata e .data ────────────────────────────────────
"[2/4] RAW ASCII geral (.rdata + .data, min 6 chars)..."
$scan_ranges = @(
    [pscustomobject]@{ ini=0x100000; fim=0x14FA00; nome='.rdata' }
    [pscustomobject]@{ ini=0x150000; fim=0x1AC800; nome='.data'  }
)
$sb = [System.Text.StringBuilder]::new()
$soff = 0
foreach ($rng in $scan_ranges) {
    [void]$sb.Clear()
    for ($i = $rng.ini; $i -lt [Math]::Min($rng.fim, $bin.Length); $i++) {
        $b = $bin[$i]
        if ($b -ge 0x20 -and $b -le 0x7E) {
            if ($sb.Length -eq 0) { $soff = $i }
            [void]$sb.Append([char]$b)
        } else {
            if ($sb.Length -ge 6) {
                $s = $sb.ToString()
                # Filtra: so strings que contenham palavras relacionadas
                if ($s -match 'load|map|lobby|interface|delay|font|locale|config|active|json|ini|reet|pb|point|blank|inject|hook|window|skin|crosshair|fps|counter') {
                    Add-Result ("ASCII_SCAN  rva=0x{0:X6}  sec={1,-8}  '{2}'" -f
                        $soff, $rng.nome, $s)
                }
            }
            [void]$sb.Clear()
        }
    }
    if ($sb.Length -ge 6) {
        $s = $sb.ToString()
        if ($s -match 'load|map|lobby|interface|delay|font|locale|config|active|json|ini|reet|pb|point|blank|inject|hook|window|skin|crosshair|fps|counter') {
            Add-Result ("ASCII_SCAN  rva=0x{0:X6}  sec={1,-8}  '{2}'" -f
                $soff, $rng.nome, $s)
        }
    }
}

# ── 3. SCAN WIDE GERAL em .rdata e .data ─────────────────────────────────────
"[3/4] RAW WIDE geral (.rdata + .data, min 5 chars)..."
foreach ($rng in $scan_ranges) {
    $sb2 = [System.Text.StringBuilder]::new()
    $soff2 = 0
    $i = $rng.ini
    while ($i -lt [Math]::Min($rng.fim - 1, $bin.Length - 1)) {
        $lo = $bin[$i]; $hi = $bin[$i + 1]
        if ($hi -eq 0 -and $lo -ge 0x20 -and $lo -le 0x7E) {
            if ($sb2.Length -eq 0) { $soff2 = $i }
            [void]$sb2.Append([char]$lo)
            $i += 2
        } else {
            if ($sb2.Length -ge 5) {
                $s2 = $sb2.ToString()
                if ($s2 -match 'load|map|lobby|interface|delay|font|locale|config|active|json|ini|reet|pb|point|blank|inject|hook|window|skin|crosshair|fps|counter') {
                    Add-Result ("WIDE_SCAN   rva=0x{0:X6}  sec={1,-8}  '{2}'" -f
                        $soff2, $rng.nome, $s2)
                }
            }
            [void]$sb2.Clear()
            $i += if ($hi -ne 0) { 1 } else { 2 }
        }
    }
}

# ── 4. KNOWN_XOR1: chaves principais com todas as chaves possiveis ────────────
"[4/4] XOR1 (chave antes) para chaves principais..."
$xor_targets = @(
    'LOADINGMAP'
    'INTERFACE'
    'SET_INTERFACE'
    'ACTIVE'
    'FONTE_PERSONALIZADA'
    'Font.ini'
    'lobby'
    'loading'
)
foreach ($plain in $xor_targets) {
    $bytes = [System.Text.Encoding]::ASCII.GetBytes($plain)
    for ($key = 1; $key -le 0xFE; $key++) {
        $enc = [byte[]]::new($bytes.Length)
        for ($j = 0; $j -lt $bytes.Length; $j++) { $enc[$j] = [byte]($bytes[$j] -bxor $key) }
        $hits = [BF]::Find($bin, $enc)
        foreach ($pos in $hits) {
            $k_antes = ($pos -gt 0 -and $bin[$pos - 1] -eq $key)
            # So reporta se o byte antes e a chave (padrao da DLL nova confirmado)
            if ($k_antes) {
                Add-Result ("XOR1_KEY   rva=0x{0:X6}  sec={1,-8}  key=0x{2:X2}  chave_antes=True   '{3}'" -f
                    $pos, (SecName $pos), $key, $plain)
            }
        }
    }
}

# ── Salvar ────────────────────────────────────────────────────────────────────
$header = @(
    "arquivo: $([IO.Path]::GetFileName($bin_path))"
    "data:    $(Get-Date -f 'yyyy-MM-dd HH:mm:ss')"
    "total:   $($results.Count) entradas"
    ""
    "COLUNAS: metodo  rva  secao  conteudo"
    "RAW_ASCII/WIDE = string direta no binario"
    "ASCII_SCAN/WIDE_SCAN = scan geral filtrado por palavras-chave"
    "XOR1_KEY = cifrado com [key][enc...][00], chave_antes=True = padrao confirmado da DLL nova"
    ""
)
($header + ($results | Sort-Object)) | Set-Content $out -Encoding UTF8
"`nTotal: $($results.Count) resultados -> $out"
