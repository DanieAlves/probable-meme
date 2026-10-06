/* ============================================================================
 *  ReetFPS.exe  -  RECONSTRUCAO LEGIVEL (anotada) das partes principais
 * ----------------------------------------------------------------------------
 *  O QUE E ISTO
 *    ReetFPS e um "otimizador de FPS" para o jogo Point Blank, escrito em
 *    Delphi (RAD Studio / VCL). Este arquivo NAO e o codigo-fonte original
 *    (que foi perdido na compilacao) - e uma RECONSTRUCAO em C das rotinas que
 *    realmente importam, feita a partir da decompilacao do binario no Ghidra.
 *
 *    O objetivo e ser LIDO por um humano para entender o que o programa faz.
 *    Nao compila e nao pretende compilar: os nomes/comentarios foram inferidos
 *    da analise; os enderecos originais (ex.: 0x1371598c) ficam nos comentarios
 *    para voce poder abrir a mesma funcao no Ghidra e conferir.
 *
 *  RESUMO DO QUE O PROGRAMA FAZ
 *    OTIMIZADOR: aplica um catalogo grande de ajustes no Windows executando
 *    comandos de shell (reg add, sc, bcdedit, netsh, powercfg, remocao de
 *    appx, etc.). O catalogo completo esta em "catalogo_comandos.md" e as
 *    estruturas de dados estao em "catalogo_comandos.c".
 *
 *    NOTA: o fluxo de LOGIN/autenticacao remota e as validacoes de plano/
 *    licenca foram RETIRADOS desta reconstrucao (projeto em homologacao,
 *    caminho para open source). Nao ha mais tela de login nem checagem de
 *    credenciais/licenca neste codigo.
 *
 *  COMPILADOR DETECTADO : Borland/Embarcadero Delphi (x86, 32-bit)
 *  IMAGE BASE           : 0x13140000
 *  FUNCOES TOTAIS       : 18.086   | SIMBOLOS: 56.972
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 *  TIPOS AUXILIARES (modelo simplificado do runtime Delphi)
 * ------------------------------------------------------------------------- */

/* No Delphi, uma "string" (UnicodeString) e um ponteiro para um buffer de
 * WideChar com cabecalho de tamanho/refcount antes do ponteiro. Aqui tratamos
 * como ponteiro opaco; "vazia" == ponteiro nulo. */
typedef void *DelphiStr;

/* Instancia de formulario/VCL. Acessamos campos por offset, como no binario. */
typedef uint8_t TForm;


/* ===========================================================================
 *  1) RESOLVEDOR DO CAMINHO DO POWERSHELL
 * ===========================================================================
 *
 *  Original: FUN_135401d0 @ 0x135401d0
 *
 *  Como o ReetFPS roda ajustes via PowerShell e e um processo 32-bit, ele
 *  precisa escolher o caminho certo para nao cair no redirecionamento WOW64:
 *    - "Sysnative\..."  alcanca o PowerShell de 64 bits a partir de um app
 *      de 32 bits (quando o SO e 64-bit e o atalho existe);
 *    - senao usa "System32\..." (PowerShell nativo da arquitetura do SO).
 *
 *  Retorna, em *destino, o caminho ABSOLUTO escolhido: o diretorio do
 *  Windows (GetWindowsDirectoryW, com barra final) concatenado ao sufixo.
 */

/* Prototipos dos auxiliares (mapeamento 1:1 com o binario). */
/* Verdadeiro quando o processo roda sob WOW64: obtem um export do kernel32
 * por FUN_1315b19c e o chama com GetCurrentProcess()
 * (INFERIDO: IsWow64Process) (FUN_135400c4). */
extern bool so_e_64bits(void);
/* GetWindowsDirectoryW + barra final (FUN_13540150 -> FUN_131776e8). */
extern void obter_windir(DelphiStr *dst);
/* dst = dst + sufixo (FUN_1314c7d0). */
extern void str_append(DelphiStr *dst, const wchar_t *sufixo);
/* dst = a + b (FUN_1314c828). */
extern void str_concat(DelphiStr *dst, DelphiStr a, const wchar_t *b);
/* O arquivo existe? (FUN_1316db0c(path, 1)). */
extern bool arquivo_existe(DelphiStr caminho);

void resolver_caminho_powershell(DelphiStr *destino)
{
    DelphiStr teste  = NULL;  /* local_8  */
    DelphiStr windir = NULL;  /* local_c / local_10 */

    if (so_e_64bits())                                         /* FUN_135400c4 */
    {
        /* Testa <windir>\Sysnative\...\powershell.exe */
        obter_windir(&teste);                                  /* FUN_13540150 */
        str_append(&teste, L"Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe");

        if (arquivo_existe(teste))                             /* FUN_1316db0c */
        {
            obter_windir(&windir);
            str_concat(destino, windir,
                       L"Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe");
            return;
        }
    }

    /* Fallback: <windir>\System32\...\powershell.exe */
    obter_windir(&windir);
    str_concat(destino, windir, L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
}


/* ===========================================================================
 *  2) OTIMIZADOR (visao geral)
 * ===========================================================================
 *
 *  O nucleo do "boost" e um catalogo de ajustes do Windows. Cada ajuste e,
 *  na pratica, um comando de shell executado (em geral de forma elevada, via
 *  o PowerShell/cmd resolvido acima). Foram extraidos 559 comandos unicos do
 *  binario.
 *
 *  As categorias (contagem de comandos unicos por grupo):
 *      SERVICES ............ 98   desativa servicos (DiagTrack, SysMain,
 *                                 WSearch, Fax, diagnosticshub, ...)
 *      CPU_GPU_PRIORITY .... 39   SystemProfile/Games, GPU Priority,
 *                                 Win32PrioritySeparation, LargeSystemCache
 *      UI_RESPONSIVENESS ... 33   MenuShowDelay, animacoes DWM, efeitos visuais
 *      POWER ............... 28   powercfg (planos de energia, PERFBOOST, etc.)
 *      GAMEDVR_GAMEBAR ..... 22   desativa GameDVR/GameBar/captura
 *      NETWORK ............. 10   Tcpip params, TCPNoDelay, rss, netsh
 *      PRIVACY_TELEMETRY ... 10   AllowTelemetry, DataCollection
 *      APPX_REMOVE .......... 8   remove appx (Cortana, etc.)
 *      BOOT_BCDEDIT ......... 6   bcdedit (useplatformtick, hypervisor, ...)
 *      EVENTLOG ............. 3   limita tamanho dos logs de eventos
 *      OTHER .............. 302   mouse/teclado (Control Panel), menu de
 *                                 contexto do proprio ReetFPS, e demais reg add
 *
 *  OBSERVACAO IMPORTANTE: muitos comandos aparecem em PARES (aplicar / reverter),
 *  por exemplo MenuShowDelay com valores 0 (otimizado) e 400 (padrao do Windows).
 *  Isso indica que o ReetFPS tem funcoes de "aplicar perfil" e "restaurar".
 *
 *  A forma reconstruida do catalogo (struct Tweak + arrays por categoria) e o
 *  conteudo integral dos comandos estao nos arquivos companheiros:
 *      - catalogo_comandos.md  (lista completa, legivel, agrupada)
 *      - catalogo_comandos.c   (os mesmos comandos como arrays de dados em C)
 *
 *  Modelo de execucao (observado no despachante FUN_135d1fb8):
 *
 *    Cada bloco de ajustes monta um ARRAY de linhas de comando e chama
 *    FUN_135d1fb8(array, ultimo_indice). Ela repassa para FUN_135d1fdc,
 *    que:
 *      1. cria um objeto worker da classe cuja VMT esta em 0x135d1ec4
 *         (INFERIDO: descendente de TThread; construtor FUN_1321994c);
 *      2. cria uma lista de strings (FUN_13206ce8, guardada em +0x28);
 *      3. para cada linha do array aplica Trim (FUN_1316c638) e, se nao
 *         ficar vazia, adiciona a linha ORIGINAL a lista (vtbl+0x3c);
 *      4. inicia o worker (FUN_1321a87c).
 *    Ou seja: os comandos de um bloco rodam em segundo plano, numa thread,
 *    e nao na thread da interface.
 *
 *    COMO cada linha e executada fica no metodo do worker em 0x135d2564
 *    (entrada da VMT), que nao esta definido como funcao no Ghidra e nao foi
 *    decompilado. O binario tem as strings "cmd.exe" (0x135bd8d8),
 *    "Sysnative\cmd.exe" (0x135bd878), " /d /c " (0x135c3ff0), "runas"
 *    (0x135515dc) e importa ShellExecuteW/ShellExecuteExW, mas a ligacao
 *    dessas strings com este worker NAO foi comprovada. Varias mensagens
 *    pedem "Execute o ReetFPS como administrador" (ex.: 0x135c512c), o que
 *    indica que o programa depende de rodar elevado.
 */

typedef struct {
    const char *comando;    /* linha de shell executada */
    const char *categoria;  /* grupo logico (ver acima) */
} Tweak;

/* Despachante de blocos de comandos (FUN_135d1fb8 -> FUN_135d1fdc). */
extern void executar_bloco_comandos(const char *const *linhas, int ultimo_indice);

/* Aplica um ajuste isolado: um bloco de uma linha so.
 * INFERIDO: no binario os ajustes sao sempre enviados em blocos pelo
 * despachante acima; nao ha uma funcao "aplica um ajuste" separada. */
void aplicar_tweak(const Tweak *t)
{
    const char *linhas[1] = { t->comando };
    executar_bloco_comandos(linhas, 0);
}

/* Aplica um conjunto de ajustes como UM bloco, como o binario faz: todas as
 * linhas vao para o mesmo worker (FUN_135d1fb8).
 * Strings de UI relacionadas a perfis:
 *   "APLICAR PERFIL"                            @ 0x135950ac
 *   "PERFIL ATIVO"                              @ 0x13595084
 *   "Perfil Competitivo aplicado com sucesso."  @ 0x1359772c
 *   "Ativa o modo de jogo recomendado..."       @ 0x136ed430
 *   "Aplica o perfil de energia recomendado..." @ 0x136ec454
 * INFERIDO: o agrupamento por categoria (Tweak.categoria) e uma organizacao
 * desta reconstrucao; no binario cada bloco tem seu proprio array. */
void aplicar_categoria(const Tweak *lista, int n)
{
    const char *linhas[64];
    int k = 0;

    for (int i = 0; i < n && k < 64; i++)
        linhas[k++] = lista[i].comando;

    if (k > 0)
        executar_bloco_comandos(linhas, k - 1);
}

/* ============================================================================
 *  FIM. Para o catalogo completo de comandos, ver os arquivos companheiros.
 * ========================================================================== */
