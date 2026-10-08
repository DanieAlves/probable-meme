# Cria uma copia do .bin com headers PE corrigidos para analise no Ghidra.
#
# O .bin e um dump de memoria: as secoes estao nos offsets VirtualAddress,
# mas o PE header tem PointerToRawData dos offsets do arquivo em disco.
# O Ghidra leria as secoes nos lugares errados se carregasse direto.
#
# O que este script faz:
#   1. Le o .bin original
#   2. Copia o conteudo intacto
#   3. No PE header da copia: FileAlignment <- SectionAlignment (0x1000)
#   4. Para cada secao:   PointerToRawData <- VirtualAddress
#                         SizeOfRawData    <- VirtualSize arredondado para cima p/ 0x1000
#   5. Salva como bloco_*_GHIDRA.bin na mesma pasta
#
# ImageBase permanece 0x10000000 (original).
# Como a DLL foi capturada com relocacoes ja aplicadas (base real 0x201D0000),
# voce tem duas opcoes ao importar no Ghidra:
#   A) Importar com base 0x10000000 e usar Window > Rebase Program -> 0x201D0000
#   B) Deixar em 0x10000000 e ignorar os offsets absolutos (referencias relativas ok)
# Recomendado: importar em 0x10000000 para manter compatibilidade com strings e
# comentarios deste projeto; usa Rebase se quiser combinar com live debugging.

param([string]$bin_path = '')

$dir = $PSScriptRoot
if (-not $bin_path) {
    $bin_path = Get-ChildItem $dir 'bloco_PID*_0x*.bin' |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $bin_path -or -not (Test-Path $bin_path)) { "ERRO: nao achei .bin"; exit 1 }

$out_path = $bin_path -replace '\.bin$', '_GHIDRA.bin'
"Entrada: $([IO.Path]::GetFileName($bin_path))"
"Saida:   $([IO.Path]::GetFileName($out_path))"

$bin = [IO.File]::ReadAllBytes($bin_path)
$out = [byte[]]::new($bin.Length)
[Array]::Copy($bin, $out, $bin.Length)

function GetI32([byte[]]$b, [int]$off) { [BitConverter]::ToInt32($b, $off) }
function SetI32([byte[]]$b, [int]$off, [int]$v) {
    $bytes = [BitConverter]::GetBytes([int]$v)
    $b[$off]=$bytes[0]; $b[$off+1]=$bytes[1]; $b[$off+2]=$bytes[2]; $b[$off+3]=$bytes[3]
}

$e_lfanew  = GetI32 $out 0x3C
$opt_off   = $e_lfanew + 0x18
$sec_align = GetI32 $out ($opt_off + 0x20)   # SectionAlignment (mantido)
$num_secs  = [BitConverter]::ToInt16($out, $e_lfanew + 0x06)
$opt_size  = [BitConverter]::ToInt16($out, $e_lfanew + 0x14)
$sec_table = $e_lfanew + 0x18 + $opt_size

# 1. FileAlignment <- SectionAlignment
SetI32 $out ($opt_off + 0x24) $sec_align
"FileAlignment ajustado para 0x{0:X}" -f $sec_align

# 2. Corrigir cada secao
$enc = [System.Text.Encoding]::ASCII
for ($i = 0; $i -lt $num_secs; $i++) {
    $s      = $sec_table + $i * 40
    $name   = $enc.GetString($out[$s..($s+7)]).TrimEnd([char]0)
    $vsize  = GetI32 $out ($s + 8)
    $va     = GetI32 $out ($s + 12)

    # SizeOfRawData = VirtualSize arredondado para cima (multiplo de sec_align)
    $raw_sz = [int](([Math]::Ceiling($vsize / $sec_align)) * $sec_align)

    SetI32 $out ($s + 16) $raw_sz   # SizeOfRawData
    SetI32 $out ($s + 20) $va       # PointerToRawData

    "  {0,-10} PointerToRawData <- 0x{1:X6}  SizeOfRawData <- 0x{2:X6}" -f $name, $va, $raw_sz
}

[IO.File]::WriteAllBytes($out_path, $out)
"`nArquivo salvo: $out_path"
"`nImporte no Ghidra como 'PE (Portable Executable)', linguagem x86:LE:32:default:windows."
"Se o Ghidra perguntar sobre o image base, mantenha 0x10000000."
"Apos analise automatica: Window > Rebase Program -> 0x201D0000 se quiser"
"combinar os enderecos com o live debugging (opcional)."
