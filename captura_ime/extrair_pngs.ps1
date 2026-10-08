# Extrai as imagens PNG embutidas na secao .data da ReetFPS.dll capturada.
# PNG comeca com assinatura de 8 bytes e termina com o chunk IEND (12 bytes fixos).
# Saida: captura_ime\pngs\  (PNG_001.png, PNG_002.png, ...)
param([string]$bin_path = '')

$dir = $PSScriptRoot
if (-not $bin_path) {
    $bin_path = Get-ChildItem $dir 'bloco_PID*_0x*.bin' |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $bin_path -or -not (Test-Path $bin_path)) { "ERRO: nao achei .bin em $dir"; exit 1 }

$out_dir = Join-Path $dir 'pngs'
New-Item -ItemType Directory -Force $out_dir | Out-Null

"lendo $([IO.Path]::GetFileName($bin_path)) ..."
$bin = [IO.File]::ReadAllBytes($bin_path)

# Assinatura PNG (8 bytes) e chunk IEND completo (12 bytes: 00000000 IEND crc)
$SIG  = [byte[]](0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A)
$IEND = [byte[]](0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,0x82)

# Busca de sequencia de bytes (C# para velocidade)
Add-Type @'
using System; using System.Collections.Generic;
public static class PngSearch {
    public static int[] FindAll(byte[] hay, byte[] needle) {
        var r = new List<int>();
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

$starts = [PngSearch]::FindAll($bin, $SIG)
$ends   = [PngSearch]::FindAll($bin, $IEND)   # cada fim inclui os 12 bytes do IEND

"Assinaturas PNG encontradas: $($starts.Count)"
"Chunks IEND encontrados:     $($ends.Count)"

$log_lines = [System.Collections.Generic.List[string]]::new()
$log_lines.Add("arquivo: $([IO.Path]::GetFileName($bin_path))")
$log_lines.Add("data:    $(Get-Date -f 'yyyy-MM-dd HH:mm:ss')")
$log_lines.Add("")
$log_lines.Add(("{0,-12} {1,-12} {2,-12} {3,-10} {4}" -f "arquivo","rva_inicio","rva_fim","tamanho","resolucao"))

$count = 0
foreach ($s in $starts) {
    # achar o IEND mais proximo depois deste inicio
    $e = $ends | Where-Object { $_ -gt $s } | Select-Object -First 1
    if ($null -eq $e) { "AVISO: PNG @ 0x{0:X6} sem IEND" -f $s; continue }
    $fim = $e + $IEND.Length   # posicao logo apos o ultimo byte do IEND

    # extrair bytes
    $len = $fim - $s
    $png_bytes = $bin[$s..($fim-1)]

    # ler dimensoes do IHDR (bytes 16..23 do PNG: width 4 bytes, height 4 bytes, big-endian)
    $w = 0; $h = 0
    if ($png_bytes.Length -ge 24) {
        $w = ([int]$png_bytes[16] -shl 24) -bor ([int]$png_bytes[17] -shl 16) -bor ([int]$png_bytes[18] -shl 8) -bor [int]$png_bytes[19]
        $h = ([int]$png_bytes[20] -shl 24) -bor ([int]$png_bytes[21] -shl 16) -bor ([int]$png_bytes[22] -shl 8) -bor [int]$png_bytes[23]
    }

    $count++
    $nome = "PNG_{0:D3}.png" -f $count
    $caminho = Join-Path $out_dir $nome
    [IO.File]::WriteAllBytes($caminho, $png_bytes)

    $linha = ("{0,-12} rva=0x{1:X6}  rva=0x{2:X6}  {3,8} bytes  {4}x{5}" -f $nome, $s, ($fim-1), $len, $w, $h)
    $log_lines.Add($linha)
    if ($count % 10 -eq 0) { "  extraidos: $count..." }
}

$log_path = Join-Path $out_dir 'indice.txt'
$log_lines | Set-Content $log_path -Encoding UTF8

"`nTotal extraido: $count PNGs -> $out_dir"
"Indice com resolucoes: $log_path"
