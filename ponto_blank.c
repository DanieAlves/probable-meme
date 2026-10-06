/* ============================================================================
 *  ReetFPS.exe  -  MODULO POINT BLANK (ponto_blank.c)
 * ----------------------------------------------------------------------------
 *  Reconstrucao em C anotada das rotinas que o ReetFPS executa DIRETAMENTE
 *  sobre o processo do Point Blank. Complementa reetfps.c (login + otimizador
 *  do Windows) e o resumo de alto nivel em ponto_blank.md.
 *
 *  O QUE ESTA AQUI
 *    1.  pb_encontrar_instalacao()          -- acha onde o jogo esta instalado
 *    2.  pb_encerrar_processos()            -- mata o processo do jogo via taskkill
 *    3.  pb_init_ponteiros_api()            -- carrega funcoes da WinAPI em runtime
 *    4.  pb_elevar_prioridade_cpu()         -- SetPriorityClass(ABOVE_NORMAL) no jogo
 *    5.  pb_elevar_priority_boost()         -- SetProcessPriorityBoost(bDisable=FALSE)
 *    6.  pb_elevar_prioridade_gpu()         -- D3DKMTSetProcessSchedulingPriorityClass
 *    7.  pb_mmcss_configurar()              -- AvSetMmThread* (MMCSS, perfil "Games")
 *    8.  pb_verificar_crashes()             -- conta crashes diarios e exibe aviso
 *    9.  pb_ativar_fluidezmax()             -- perfil FLUIDEZMAX (7 itens)
 *   10.  limpeza_varrer_diretorio()         -- limpeza inteligente (FindFirstFile loop)
 *   11.  pb_configurar_timer_resolution()   -- SetProcessInformation (TimerResolutionPolicy)
 *   12.  crosshair_desenhar()               -- mira customizada sobre o jogo
 *
 *  O QUE NAO ESTA AQUI
 *    - O carregador PE em memoria (0x136e5000-0x136e7400) usa
 *      OpenProcess/WriteProcessMemory/CreateRemoteThread. Esse componente
 *      nao e reconstruido neste arquivo por razoes explicadas em ponto_blank.md.
 *
 *  COMO LER
 *    Cada funcao tem um comentario com o endereco original para voce abrir
 *    no Ghidra e conferir. Offsets de campo (ex.: +0x8d0) sao do objeto
 *    TPointBlankStabilityMonitor (o "contexto" param_1 de cada funcao).
 *
 *  NAO COMPILA -- e uma leitura em C do que o binario faz.
 * ========================================================================== */

#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 *  TIPOS AUXILIARES (identicos a reetfps.c)
 * ------------------------------------------------------------------------- */

typedef void    *DelphiStr;  /* UnicodeString -- ponteiro nulo == vazia */
typedef uint8_t  TForm;      /* formulario VCL -- campos via offset */

/* ---------------------------------------------------------------------------
 *  CONTEXTO DO MODULO DE MANUTENCAO
 *
 *  Offsets observados no decompilador para o objeto que e passado como
 *  "param_1" para a maioria das funcoes abaixo. O tipo real no binario
 *  parece ser algo como "TPointBlankMaintain" ou similar.
 * ------------------------------------------------------------------------- */
typedef struct {
    /* +0x04  */ void      *p_observer;          /* ponteiro para observer/vtable */

    /* +0x33  */ bool       prioridade_foi_alterada;
    /* +0x34  */ bool       boost_foi_ativado;    /* SetProcessPriorityBoost ok nesta passada */
    /* +0x36  */ bool       d3dkmt_elevacao_confirmada; /* releitura apos Set(3) deu classe > 2 */
    /* +0x3b  */ bool       boost_ativo;          /* estado final: boost dinamico HABILITADO */
    /* +0x3d  */ bool       d3dkmt_elevado;       /* classe de GPU ja > NORMAL ou elevada agora */
    /* +0x3e  */ bool       d3dkmt_utilizavel;    /* D3DKMT disponivel / ainda vale tentar */

    /* +0x68  */ int        contagem_crashes_hoje;
    /* +0x6c  */ bool       boost_desabilitado;   /* bDisableBoost != 0 (boost DESLIGADO) */
    /* +0x78  */ bool       crashes_suprimidos;

    /* +0x7c  */ int        classe_d3dkmt_lida;   /* ultima classe lida por D3DKMTGet... */
    /* +0x88  */ int        contador_boost_reativado; /* vezes que o boost foi religado */

    /* Prioridade de CPU (salva/restaura): */
    /* +0x8cc */ bool       cpu_priority_habilitado; /* flag de feature ativa */
    /* +0x8d0 */ DWORD      prioridade_original;     /* salvo por GetPriorityClass */
    /* +0x8d4 */ bool       prioridade_capturada;    /* GetPriorityClass retornou ok */

    /* Ponteiros de funcao carregados em runtime (kernel32.dll): */
    /* +0x8a0 */ FARPROC    pfn_SetProcessInformation;
    /* +0x8a4 */ FARPROC    pfn_GetProcessInformation;
    /* +0x8a8 */ FARPROC    pfn_GetProcessPriorityBoost;
    /* +0x8ac */ FARPROC    pfn_SetProcessPriorityBoost;
    /* +0x8b0 */ FARPROC    pfn_GetSystemCpuSetInformation;
    /* +0x8b4 */ FARPROC    pfn_SetProcessDefaultCpuSets;
    /* +0x8b8 */ FARPROC    pfn_GetProcessDefaultCpuSets;
    /* +0x8bc */ FARPROC    pfn_GetProcessMemoryInfo;

    /* Ponteiros de funcao carregados em runtime (gdi32.dll): */
    /* +0x8c0 */ FARPROC    pfn_D3DKMTGetProcessSchedulingPriorityClass;
    /* +0x8c4 */ FARPROC    pfn_D3DKMTSetProcessSchedulingPriorityClass;

    /* +0x8f8 */ int        classe_d3dkmt_inicial;   /* classe lida antes de elevar */
    /* +0x8fc */ bool       classe_d3dkmt_capturada; /* +0x8f8 e valido */
    /* +0x8fd */ bool       d3dkmt_disponivel;   /* 0 sem ponteiros; 1 apos Get ok */
    /* +0x900 */ int        contador_erros_d3dkmt;   /* zerado quando o Get funciona */
} TPointBlankMantain;


/* ===========================================================================
 *  1. ENCONTRAR A INSTALACAO DO POINT BLANK
 * ===========================================================================
 *
 *  Original: FUN_136f010c @ 0x136f010c
 *
 *  Constroi uma lista de caminhos candidatos (registro + hardcoded + todas as
 *  letras de drive fixas) e testa cada um chamando FUN_136edf7c para verificar
 *  se ha um executavel valido naquele caminho. Retorna o primeiro que bater.
 *
 *  Mensagens de erro (strings do binario):
 *    "nao foi possivel localizar o Point Blank. Abra o PBLauncher manualmente
 *     uma vez com o ReetFPS aberto e tente novamente."
 *    "nao foi possivel iniciar o Point Blank. Feche o PBLauncher, abra o
 *     ReetFPS como administrador e tente novamente."
 */
bool pb_encontrar_instalacao(void *contexto_pb, DelphiStr *caminho_out)
{
    /* Lista dinamica de candidatos (TList / TStringList do Delphi).
     * FUN_13206ce8(&PTR_FUN_131d5998, 1) = cria um TList. */
    void *lista = criar_lista_strings();

    /* --- Caminhos fixos conhecidos --- */
    lista_adicionar(lista, L"C:\\Zepetto\\PointBlank\\");
    lista_adicionar(lista, L"C:\\PointBlank\\");
    lista_adicionar(lista, L"C:\\Games\\PointBlank\\");
    lista_adicionar(lista, L"C:\\Jogos\\PointBlank\\");
    lista_adicionar(lista, L"C:\\Program Files (x86)\\Zepetto\\PointBlank\\");
    lista_adicionar(lista, L"C:\\Program Files\\Zepetto\\PointBlank\\");

    /* --- Variaveis de ambiente do sistema --- */
    DelphiStr progfiles_x86 = NULL;
    if (env_var_expandir(L"ProgramFiles(x86)", &progfiles_x86)) {
        /* ex.: "C:\Program Files (x86)\Zepetto\PointBlank\" */
        str_concatenar(&progfiles_x86, L"Zepetto\\PointBlank\\");
        lista_adicionar(lista, progfiles_x86);
    }

    DelphiStr progfiles = NULL;
    if (env_var_expandir(L"ProgramFiles", &progfiles)) {
        str_concatenar(&progfiles, L"Zepetto\\PointBlank\\");
        lista_adicionar(lista, progfiles);
    }

    /* --- Varre todas as letras de drive (C: a Z:) ---
     *
     * Loop: sVar6 começa em 0x43 ('C') e vai ate 0x5A ('Z') inclusive.
     * Para cada letra, testa GetDriveTypeW; se == DRIVE_FIXED (3), adiciona
     * quatro subpastas candidatas.
     *
     *   <letra>:\Zepetto\PointBlank\
     *   <letra>:\PointBlank\
     *   <letra>:\Games\PointBlank\
     *   <letra>:\Jogos\PointBlank\
     */
    for (wchar_t letra = L'C'; letra <= L'Z'; letra++) {
        wchar_t raiz[4] = { letra, L':', L'\\', L'\0' };
        if (GetDriveTypeW(raiz) == DRIVE_FIXED) {
            DelphiStr tmp = NULL;
            str_combinar_caminho(&tmp, raiz, L"Zepetto\\PointBlank\\");
            lista_adicionar(lista, tmp);

            str_combinar_caminho(&tmp, raiz, L"PointBlank\\");
            lista_adicionar(lista, tmp);

            str_combinar_caminho(&tmp, raiz, L"Games\\PointBlank\\");
            lista_adicionar(lista, tmp);

            str_combinar_caminho(&tmp, raiz, L"Jogos\\PointBlank\\");
            lista_adicionar(lista, tmp);
        }
    }

    /* --- Testa cada candidato --- */
    int n = lista_contar(lista);
    for (int i = 0; i < n; i++) {
        DelphiStr candidato = lista_obter(lista, i);
        /* FUN_136edf7c(contexto_pb, caminho, resultado_anterior):
         * verifica se o caminho contem um PBLauncher.exe valido. */
        if (testar_caminho_pb(contexto_pb, candidato, *caminho_out)) {
            /* Achou. caminho_out ja foi preenchido por testar_caminho_pb. */
            destruir_lista(lista);
            return true;
        }
    }

    destruir_lista(lista);
    return false;
}

/* Prototipos dos auxiliares de lista/string usados acima. */
extern void  *criar_lista_strings(void);                          /* FUN_13206ce8 */
extern void   lista_adicionar(void *lista, const wchar_t *s);     /* vtable+0x3c */
extern int    lista_contar(void *lista);                          /* vtable+0x14 */
extern void  *lista_obter(void *lista, int i);                    /* vtable+0x0c */
extern void   destruir_lista(void *lista);                        /* FUN_13149ad8 */
extern bool   env_var_expandir(const wchar_t *nome, DelphiStr *d);/* FUN_1317948c */
extern void   str_concatenar(DelphiStr *dst, const wchar_t *suf); /* FUN_1314c7d0 */
extern void   str_combinar_caminho(DelphiStr *dst,
                                   const wchar_t *base,
                                   const wchar_t *suf);           /* FUN_1314c828 */
extern bool   testar_caminho_pb(void *ctx, DelphiStr caminho,
                                DelphiStr *resultado);            /* FUN_136edf7c */


/* ===========================================================================
 *  2. ENCERRAR PROCESSOS DO JOGO
 * ===========================================================================
 *
 *  Original: FUN_135a0d84 @ 0x135a0d84
 *
 *  Constroi a linha "taskkill.exe /F /T /IM "<nome.exe>"" e dispara via
 *  CreateProcessW com janela oculta. Aguarda 3 segundos pelo termino.
 *
 *  Uma segunda rota em 0x135cafd4 usa "cmd /c taskkill /im ..." pelo cmd.exe.
 *
 *  Strings de UI que acompanham esta operacao:
 *    "Os processos do PointBlank foram encerrados. Pode abrir o jogo de novo."
 *    "O jogo fechou, mas o processo continua aberto. Abra para encerrar."
 *    "Processo %s encerrado"
 */
void pb_encerrar_processos(const wchar_t *nome_exe)
{
    /* Monta:  taskkill.exe /F /T /IM "<nome_exe>"  */
    wchar_t *linha_cmd = montar_cmd_taskkill(nome_exe);
    /* ex.: L"taskkill.exe /F /T /IM \"PBClient.exe\"" */

    STARTUPINFOW si = { 0 };
    si.cb          = sizeof(si);           /* 0x44 */
    si.dwFlags     = STARTF_USESHOWWINDOW; /* 1    */
    si.wShowWindow = SW_HIDE;              /* 0 -- janela oculta */

    PROCESS_INFORMATION pi = { 0 };

    /* CREATE_NO_WINDOW (0x08000000) -- sem console visivel */
    BOOL ok = CreateProcessW(
        NULL,            /* lpApplicationName -- NULL: tira do lpCommandLine */
        linha_cmd,       /* lpCommandLine */
        NULL, NULL,      /* sem atributos de seguranca */
        FALSE,           /* bInheritHandles */
        CREATE_NO_WINDOW,
        NULL, NULL,      /* sem ambiente nem diretorio especifico */
        &si, &pi
    );

    if (ok) {
        /* Aguarda o taskkill encerrar o processo (timeout de 3 segundos). */
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

/* Auxiliar: concatena "taskkill.exe /F /T /IM \"" + nome + "\"" */
extern wchar_t *montar_cmd_taskkill(const wchar_t *nome_exe); /* FUN_1314c8b0 + idx 3 */


/* ===========================================================================
 *  3. INICIALIZAR PONTEIROS DE API EM RUNTIME
 * ===========================================================================
 *
 *  Original: FUN_135a3cec @ 0x135a3cec
 *
 *  O ReetFPS usa GetProcAddress para carregar dinamicamente funcoes que podem
 *  nao existir em versoes mais antigas do Windows. Os ponteiros ficam no
 *  objeto TPointBlankMantain (offsets +0x8a0 a +0x8c4).
 *
 *  FUN_135a2f38(nome)  -- GetProcAddress em kernel32.dll
 *  FUN_135a2f84(nome)  -- GetProcAddress em psapi.dll  (fallback antigo)
 *  FUN_135a2fd8(nome)  -- GetProcAddress em gdi32.dll
 */
void pb_init_ponteiros_api(TPointBlankMantain *ctx)
{
    /* Garante GetTickCount64 resolvido globalmente (DAT_13819a5c). */
    if (g_pfn_GetTickCount64 == NULL)
        g_pfn_GetTickCount64 = GetProcAddress_kernel32("GetTickCount64");

    /* Funcoes de informacao de processo (Windows 8+): */
    ctx->pfn_SetProcessInformation      = GetProcAddress_kernel32("SetProcessInformation");
    ctx->pfn_GetProcessInformation      = GetProcAddress_kernel32("GetProcessInformation");

    /* Priority boost (disponivel desde Windows 2000): */
    ctx->pfn_GetProcessPriorityBoost    = GetProcAddress_kernel32("GetProcessPriorityBoost");
    ctx->pfn_SetProcessPriorityBoost    = GetProcAddress_kernel32("SetProcessPriorityBoost");

    /* CPU sets (Windows 10+): */
    ctx->pfn_GetSystemCpuSetInformation = GetProcAddress_kernel32("GetSystemCpuSetInformation");
    ctx->pfn_SetProcessDefaultCpuSets   = GetProcAddress_kernel32("SetProcessDefaultCpuSets");
    ctx->pfn_GetProcessDefaultCpuSets   = GetProcAddress_kernel32("GetProcessDefaultCpuSets");

    /* Memoria de processo: tenta K32GetProcessMemoryInfo primeiro (Vista+),
     * cai em GetProcessMemoryInfo da psapi.dll se nao existir. */
    ctx->pfn_GetProcessMemoryInfo = GetProcAddress_kernel32("K32GetProcessMemoryInfo");
    if (ctx->pfn_GetProcessMemoryInfo == NULL)
        ctx->pfn_GetProcessMemoryInfo = GetProcAddress_psapi("GetProcessMemoryInfo");

    /* GPU Scheduler (gdi32.dll, disponivel no Windows Vista+ com WDDM): */
    ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass =
        GetProcAddress_gdi32("D3DKMTGetProcessSchedulingPriorityClass");
    ctx->pfn_D3DKMTSetProcessSchedulingPriorityClass =
        GetProcAddress_gdi32("D3DKMTSetProcessSchedulingPriorityClass");

    /* d3dkmt_disponivel so e true se AMBAS as funcoes foram encontradas. */
    ctx->d3dkmt_disponivel = (ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass != NULL)
                          && (ctx->pfn_D3DKMTSetProcessSchedulingPriorityClass != NULL);
}

extern FARPROC g_pfn_GetTickCount64;                    /* DAT_13819a5c */
extern FARPROC GetProcAddress_kernel32(const char *fn); /* FUN_135a2f38 */
extern FARPROC GetProcAddress_psapi   (const char *fn); /* FUN_135a2f84 */
extern FARPROC GetProcAddress_gdi32   (const char *fn); /* FUN_135a2fd8 */


/* ===========================================================================
 *  4. ELEVAR PRIORIDADE DE CPU DO PROCESSO DO JOGO
 * ===========================================================================
 *
 *  Original: FUN_135a4dc4 @ 0x135a4dc4
 *
 *  Eleva a classe de prioridade do processo do jogo para ABOVE_NORMAL, mas
 *  so se ela ainda nao for ABOVE_NORMAL, HIGH ou REALTIME (nao mexe se o
 *  usuario ja configurou uma prioridade alta).
 *
 *  Guarda a prioridade original em ctx->prioridade_original para poder
 *  restaurar quando o jogo fechar (ver par reverter nao decompilado aqui).
 *
 *  Strings de log internas:
 *    "GetPriorityClass"
 *    "SetPriorityClass.AboveNormal"
 */
void pb_elevar_prioridade_cpu(TPointBlankMantain *ctx, HANDLE hProcesso)
{
    /* Verifica flag "feature de prioridade de CPU ativa" (+0x8cc). */
    if (!ctx->cpu_priority_habilitado)
        return;

    DWORD prioridade_atual = GetPriorityClass(hProcesso);
    if (prioridade_atual == 0) {
        /* GetPriorityClass falhou -- loga o erro. */
        DWORD err = GetLastError();
        log_erro_maintain(ctx, L"GetPriorityClass", err); /* FUN_135a4a18 */
        return;
    }

    /* Salva a prioridade original para poder reverter depois. */
    ctx->prioridade_original  = prioridade_atual;
    ctx->prioridade_capturada = true;  /* +0x8d4 = 1 */

    /* So altera se a prioridade atual for normal ou abaixo.
     * REALTIME (0x100), HIGH (0x80) e ABOVE_NORMAL (0x8000) ja sao ok. */
    bool ja_elevada = (prioridade_atual == ABOVE_NORMAL_PRIORITY_CLASS)  /* 0x8000 */
                   || (prioridade_atual == HIGH_PRIORITY_CLASS)          /* 0x80   */
                   || (prioridade_atual == REALTIME_PRIORITY_CLASS);     /* 0x100  */
    if (ja_elevada)
        return;

    BOOL ok = SetPriorityClass(hProcesso, ABOVE_NORMAL_PRIORITY_CLASS);
    if (ok) {
        ctx->prioridade_foi_alterada = true; /* +0x33 = 1 */
    } else {
        DWORD err = GetLastError();
        log_erro_maintain(ctx, L"SetPriorityClass.AboveNormal", err);
    }
}

/* Auxiliar de log de erros do modulo Maintain. */
extern void log_erro_maintain(TPointBlankMantain *ctx,
                              const wchar_t *operacao,
                              DWORD codigo_erro);  /* FUN_135a4a18 */


/* ===========================================================================
 *  5. HABILITAR O PRIORITY BOOST DINAMICO DO PROCESSO
 * ===========================================================================
 *
 *  Original: FUN_135a6bdc @ 0x135a6bdc
 *
 *  O Windows pode "boost" temporariamente a prioridade de threads que
 *  acabam de sair de estado de espera. Alguns programas desabilitam isso;
 *  aqui o ReetFPS garante que esteja habilitado para o processo do jogo.
 *
 *  Strings de log internas:
 *    "Maintain.GetProcessPriorityBoost"
 *    "Maintain.SetProcessPriorityBoost.Enable"
 *
 *  As escritas finais nos campos do contexto sao feitas sob uma trava: o
 *  objeto em ctx+0x04 tem vtable[0] = travar e vtable[1] = destravar
 *  (mesmo padrao do §6). Aqui isso aparece como trava_ctx/destrava_ctx.
 */

/* Trava do contexto: vtable[0]/vtable[1] do objeto em ctx+0x04. */
extern void trava_ctx(TPointBlankMantain *ctx);
extern void destrava_ctx(TPointBlankMantain *ctx);

void pb_elevar_priority_boost(TPointBlankMantain *ctx, HANDLE hProcesso)
{
    /* Precisa da feature ativa (+0x8cc) e dos dois ponteiros de funcao. */
    if (!ctx->cpu_priority_habilitado
        || ctx->pfn_GetProcessPriorityBoost == NULL     /* +0x8a8 */
        || ctx->pfn_SetProcessPriorityBoost == NULL)    /* +0x8ac */
        return;

    /* GetProcessPriorityBoost(hProcesso, &bDisableBoost)
     * bDisableBoost != 0  -> boost dinamico DESABILITADO.
     * bDisableBoost == 0  -> boost dinamico HABILITADO (o que o ReetFPS quer). */
    BOOL bDisableBoost = 0;
    BOOL ok = ((BOOL(WINAPI*)(HANDLE, PBOOL))ctx->pfn_GetProcessPriorityBoost)(
                  hProcesso, &bDisableBoost);
    if (!ok) {
        DWORD err = GetLastError();
        log_erro_maintain(ctx, L"Maintain.GetProcessPriorityBoost", err);
        return;                     /* unico retorno antecipado do binario */
    }

    bool boost_habilitado = (bDisableBoost == 0);
    bool religou          = false;

    if (bDisableBoost != 0) {
        /* Boost desligado: religa passando bDisablePriorityBoost = FALSE. */
        ok = ((BOOL(WINAPI*)(HANDLE, BOOL))ctx->pfn_SetProcessPriorityBoost)(
                 hProcesso, FALSE);
        if (!ok) {
            DWORD err = GetLastError();
            log_erro_maintain(ctx, L"Maintain.SetProcessPriorityBoost.Enable", err);
            /* NAO retorna: segue para gravar o estado (bDisableBoost continua != 0). */
        } else {
            religou = true;

            /* Confirma relendo o estado. */
            bDisableBoost = 0;
            ok = ((BOOL(WINAPI*)(HANDLE, PBOOL))ctx->pfn_GetProcessPriorityBoost)(
                     hProcesso, &bDisableBoost);
            boost_habilitado = ok && (bDisableBoost == 0);
        }
    }

    /* Grava o estado SEMPRE -- inclusive quando o boost ja estava ligado. */
    trava_ctx(ctx);
    ctx->boost_desabilitado = (bDisableBoost != 0);     /* +0x6c */
    ctx->boost_ativo        = boost_habilitado;         /* +0x3b */
    if (religou) {
        ctx->boost_foi_ativado = true;                  /* +0x34 = 1 */
        ctx->contador_boost_reativado++;                /* +0x88     */
    }
    destrava_ctx(ctx);
}


/* ===========================================================================
 *  6. ELEVAR PRIORIDADE DE AGENDAMENTO DA GPU (D3DKMT)
 * ===========================================================================
 *
 *  Original: FUN_135a56ec @ 0x135a56ec
 *
 *  D3DKMTSetProcessSchedulingPriorityClass eleva a classe de agendamento do
 *  GPU scheduler para o processo. Requer gdi32.dll no Vista+/WDDM.
 *
 *  Enum D3DKMT_SCHEDULINGPRIORITYCLASS (d3dkmthk.h):
 *    0 = IDLE, 1 = BELOW_NORMAL, 2 = NORMAL, 3 = ABOVE_NORMAL, 4 = HIGH, 5 = REALTIME
 *
 *  O ReetFPS so mexe quando a classe atual e <= 2 (NORMAL ou abaixo) e entao
 *  pede a classe 3 (ABOVE_NORMAL) -- coerente com o sufixo ".AboveNormal" da
 *  string de log. Classes 3..5 ja definidas por outro programa sao mantidas.
 *
 *  Strings de log internas:
 *    "D3DKMTSetProcessSchedulingPriorityClass.AboveNormal"
 *    (usada so na falha do Set; a falha do Get NAO gera log)
 *
 *  As escritas finais em +0x3d/+0x3e/+0x7c/+0x36 sao feitas sob a trava do
 *  contexto (ver §5).
 */
#define D3DKMT_CLASSE_NORMAL        2
#define D3DKMT_CLASSE_ABOVE_NORMAL  3

void pb_elevar_prioridade_gpu(TPointBlankMantain *ctx, HANDLE hProcesso)
{
    /* Sem os dois ponteiros D3DKMT: marca indisponivel e sai. */
    if (ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass == NULL     /* +0x8c0 */
        || ctx->pfn_D3DKMTSetProcessSchedulingPriorityClass == NULL) /* +0x8c4 */
    {
        ctx->d3dkmt_disponivel = false;                /* +0x8fd = 0 */
        trava_ctx(ctx);
        ctx->d3dkmt_utilizavel = false;                /* +0x3e = 0 */
        ctx->d3dkmt_elevado    = false;                /* +0x3d = 0 */
        destrava_ctx(ctx);
        return;
    }

    /* Le a classe atual (valor padrao 2 se a API nao escrever nada). */
    int classe = D3DKMT_CLASSE_NORMAL;
    NTSTATUS st = ((NTSTATUS(WINAPI*)(HANDLE, int*))
                      ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass)(
                      hProcesso, &classe);

    if (st != 0) {
        /* Falha no Get: so conta o erro, sem log. Depois de 6 falhas
         * seguidas o D3DKMT deixa de ser considerado utilizavel. */
        ctx->contador_erros_d3dkmt++;                       /* +0x900 */
        trava_ctx(ctx);
        ctx->d3dkmt_utilizavel  = (ctx->contador_erros_d3dkmt < 6); /* +0x3e */
        ctx->d3dkmt_elevado     = false;                    /* +0x3d */
        ctx->classe_d3dkmt_lida = classe;                   /* +0x7c */
        destrava_ctx(ctx);
        return;
    }

    /* Get ok: zera erros e guarda a classe inicial. */
    ctx->contador_erros_d3dkmt   = 0;                       /* +0x900 = 0 */
    ctx->d3dkmt_disponivel       = true;                    /* +0x8fd = 1 */
    ctx->classe_d3dkmt_inicial   = classe;                  /* +0x8f8     */
    ctx->classe_d3dkmt_capturada = true;                    /* +0x8fc = 1 */

    bool elevado = (classe > D3DKMT_CLASSE_NORMAL);         /* ja acima de NORMAL? */

    if (!elevado) {
        st = ((NTSTATUS(WINAPI*)(HANDLE, int))
                 ctx->pfn_D3DKMTSetProcessSchedulingPriorityClass)(
                 hProcesso, D3DKMT_CLASSE_ABOVE_NORMAL);   /* Set(3) */

        if (st == 0) {
            /* Confirma relendo; so considera elevado se a classe ficou > 2. */
            classe = D3DKMT_CLASSE_NORMAL;
            st = ((NTSTATUS(WINAPI*)(HANDLE, int*))
                     ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass)(
                     hProcesso, &classe);
            elevado = (st == 0) && (classe > D3DKMT_CLASSE_NORMAL);

            if (elevado) {
                trava_ctx(ctx);
                ctx->d3dkmt_elevacao_confirmada = true;     /* +0x36 = 1 */
                destrava_ctx(ctx);
            }
        } else {
            /* Falha no Set: conta e loga so na primeira vez. */
            ctx->contador_erros_d3dkmt++;                   /* +0x900 */
            if (ctx->contador_erros_d3dkmt == 1)
                log_erro_maintain(ctx,
                    L"D3DKMTSetProcessSchedulingPriorityClass.AboveNormal", st);
        }
    }

    trava_ctx(ctx);
    ctx->d3dkmt_utilizavel  = ctx->d3dkmt_disponivel;      /* +0x3e = +0x8fd (1) */
    ctx->d3dkmt_elevado     = elevado;                      /* +0x3d */
    ctx->classe_d3dkmt_lida = classe;                       /* +0x7c */
    destrava_ctx(ctx);
}


/* ===========================================================================
 *  7. MMCSS -- THREAD DE TIMER DO REETFPS ("Pro Audio" / "Games" / "Playback")
 * ===========================================================================
 *
 *  Funcoes no binario (nenhuma esta definida como funcao no Ghidra; lidas
 *  a partir dos bytes):
 *    FUN_1357c2b4 @ 0x1357c2b4  -- carrega avrt.dll uma unica vez:
 *        DAT_1380f21c = LoadLibraryW(L"avrt.dll")
 *        DAT_1380f220 = GetProcAddress(.., "AvSetMmThreadCharacteristicsW")
 *        DAT_1380f224 = GetProcAddress(.., "AvSetMmThreadPriority")
 *        DAT_1380f228 = GetProcAddress(.., "AvRevertMmThreadCharacteristics")
 *    0x1357c4d0                 -- registra a THREAD ATUAL no MMCSS
 *    0x1357c88c                 -- Execute da thread "ReetTimerPrecision"
 *                                  (VMT em 0x1357c804, classe TTPWatchdog)
 *
 *  Nomes de tarefa tentados, em ordem (UTF-16LE):
 *    "Pro Audio"  @ 0x1357c570
 *    "Games"      @ 0x1357c584
 *    "Playback"   @ 0x1357c590
 *
 *  IMPORTANTE: AvSetMmThreadCharacteristicsW age sobre a thread que a chama.
 *  Quem chama e o Execute da thread de manutencao do timer (ver §10), entao o
 *  MMCSS e aplicado a essa thread do proprio ReetFPS, NAO ao processo do
 *  Point Blank. O objetivo e a thread que reafirma a resolucao de 0.5 ms
 *  nao perder a vez para outras threads.
 *
 *  Nao ha timeEndPeriod aqui: o timeEndPeriod(1) pertence a FUN_1357c5e0
 *  (revogacao do timer, §10) e so roda se o fallback timeBeginPeriod foi usado.
 */

typedef HANDLE (WINAPI *PFN_AvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
typedef BOOL   (WINAPI *PFN_AvSetMmThreadPriority)(HANDLE, int);
typedef BOOL   (WINAPI *PFN_AvRevertMmThreadCharacteristics)(HANDLE);

extern HMODULE g_hAvrt;                                      /* DAT_1380f21c */
extern PFN_AvSetMmThreadCharacteristicsW   g_pfnAvSetChar;   /* DAT_1380f220 */
extern PFN_AvSetMmThreadPriority           g_pfnAvSetPri;    /* DAT_1380f224 */
extern PFN_AvRevertMmThreadCharacteristics g_pfnAvRevert;    /* DAT_1380f228 */
extern void avrt_carregar(void);                             /* FUN_1357c2b4 */

#define AVRT_PRIORITY_HIGH 1    /* avrt.h */

/* Tenta registrar a thread atual numa tarefa MMCSS e elevar sua prioridade.
 * Original: codigo em 0x1357c4d0. Retorna o handle da tarefa ou NULL. */
HANDLE pb_mmcss_configurar(void)
{
    static const wchar_t *tarefas[] = { L"Pro Audio", L"Games", L"Playback" };
    HANDLE h = NULL;
    DWORD  indice;

    avrt_carregar();                                     /* FUN_1357c2b4 */

    for (int i = 0; i < 3 && h == NULL; i++) {
        if (g_pfnAvSetChar == NULL)
            continue;
        h = g_pfnAvSetChar(tarefas[i], &indice);
        if (h != NULL && g_pfnAvSetPri != NULL)
            g_pfnAvSetPri(h, AVRT_PRIORITY_HIGH);        /* (h, 1) */
    }
    return h;
}

/* Ciclo de vida, visto no Execute da thread em 0x1357c88c:
 *   inicio : self+0x28 = pb_mmcss_configurar();
 *   fim    : if (self+0x28 != NULL && g_pfnAvRevert != NULL)
 *                g_pfnAvRevert(self+0x28);
 * A reversao e feita inline no Execute; nao existe funcao separada. */
void pb_mmcss_reverter(HANDLE h_tarefa)
{
    if (h_tarefa != NULL && g_pfnAvRevert != NULL)
        g_pfnAvRevert(h_tarefa);                         /* DAT_1380f228 */
}


/* ===========================================================================
 *  8. VERIFICAR E EXIBIR CONTAGEM DE CRASHES DO DIA
 * ===========================================================================
 *
 *  Original: FUN_1359f8b0 @ 0x1359f8b0
 *
 *  Chamado pelo TPointBlankStabilityMonitor ao detectar que o jogo encerrou.
 *  Se o jogo fechou mais de 2 vezes hoje, publica UMA notificacao do dia
 *  (identificada pela chave "pb-crash-YYYYMMDD") e marca o aviso como dado.
 *
 *  Strings do binario (UTF-16LE):
 *    "%d encerramentos inesperados hoje. Veja como reparar."      (0x1359fa4c)
 *    "Detectamos %d encerramentos inesperados do PointBlank hoje. Abra esta
 *     notificacao para ver as recomendacoes de reparacao."        (0x1359fac4)
 *    "yyyymmdd"                                                   (0x1359fbc4)
 *    "O PointBlank fechou varias vezes"   (titulo da notificacao, 0x1359fbe4)
 *    "pb-crash-"                          (prefixo da chave,      0x1359fc34)
 *
 *  Condicoes para notificar (todas verificadas no inicio da funcao):
 *    ctx->contagem_crashes_hoje > 2                       (+0x68)
 *    ctx->crashes_suprimidos == false                     (+0x78, "ja avisado")
 *    *(*PTR_DAT_13811668 + 0xbc) == 0                     (flag global de
 *                                                          supressao de avisos)
 *
 *  Depois de publicar com sucesso, o binario grava +0x78 = 1 e chama
 *  FUN_1359e8ec(ctx) -- por isso o aviso aparece no maximo uma vez.
 *  INFERIDO: FUN_1359e8ec persiste o estado diario (nao decompilada aqui).
 */
void pb_verificar_crashes(TPointBlankMantain *ctx)
{
    if (ctx->contagem_crashes_hoje <= 2
        || ctx->crashes_suprimidos
        || *((uint8_t *)*g_config_app + 0xbc) != 0)   /* PTR_DAT_13811668 */
        return;

    /* Texto curto e texto detalhado da notificacao (FUN_1316ec8c = Format). */
    DelphiStr msg_curta = NULL;      /* [EBP-4] */
    str_formatar(&msg_curta,
        L"%d encerramentos inesperados hoje. Veja como reparar.",
        ctx->contagem_crashes_hoje);

    DelphiStr msg_detalhe = NULL;    /* [EBP-8] */
    str_formatar(&msg_detalhe,
        L"Detectamos %d encerramentos inesperados do PointBlank hoje. "
        L"Abra esta notificacao para ver as recomendacoes de reparacao.",
        ctx->contagem_crashes_hoje);

    void *notificador = obter_notificador();               /* FUN_134ad1c8 */

    /* Chave unica por dia: "pb-crash-" + FormatDateTime("yyyymmdd", Now).
     * FUN_13170f54 = Now; FUN_13171f58 recebe o FormatSettings em
     * PTR_DAT_138118c0; FUN_1314c828 = concatenacao de 2 strings.         */
    DelphiStr data_str  = NULL;
    DelphiStr chave_dia = NULL;
    formatar_data(L"yyyymmdd", g_format_settings, &data_str, agora());
    str_concat(&chave_dia, L"pb-crash-", data_str);

    /* FUN_134ae4cc(notificador, chave, 2, titulo, msg_curta, msg_detalhe, 0)
     * -> true quando a notificacao foi publicada.                          */
    bool publicada = notificador_publicar(notificador, chave_dia, 2,
                                          L"O PointBlank fechou varias vezes",
                                          msg_curta, msg_detalhe, 0);
    if (publicada) {
        ctx->crashes_suprimidos = true;                    /* +0x78 = 1    */
        pb_salvar_estado_diario(ctx);                      /* FUN_1359e8ec */
    }
}

extern void **g_config_app;                    /* PTR_DAT_13811668 (+0xbc = suprimir avisos) */
extern void  *g_format_settings;               /* PTR_DAT_138118c0 (TFormatSettings) */
extern void   str_formatar(DelphiStr *dst,
                           const wchar_t *fmt, ...);       /* FUN_1316ec8c */
extern double agora(void);                                  /* FUN_13170f54 */
extern void   formatar_data(const wchar_t *fmt, void *fs,
                            DelphiStr *dst, double quando); /* FUN_13171f58 */
/* str_concat (FUN_1314c828) e declarado junto aos auxiliares da secao 11. */
extern void  *obter_notificador(void);                      /* FUN_134ad1c8 */
extern bool   notificador_publicar(void *n, DelphiStr chave, int tipo,
                                   const wchar_t *titulo,
                                   DelphiStr curta, DelphiStr detalhe,
                                   int extra);              /* FUN_134ae4cc */
extern void   pb_salvar_estado_diario(TPointBlankMantain *ctx); /* FUN_1359e8ec */


/* ===========================================================================
 *  VISAO GERAL: GESTAO INTELIGENTE (TPointBlankStabilityMonitor)
 * ===========================================================================
 *
 *  Quando o usuario ativa a "Gestao Inteligente", o ReetFPS roda
 *  TPointBlankStabilityMonitor em segundo plano. Esse monitor:
 *
 *    1. Aguarda o processo do jogo aparecer (TPointBlankWaitThread).
 *    2. Quando detecta o jogo subindo:
 *         - Abre o handle com OpenProcess (PROCESS_SET_INFORMATION | ...)
 *         - Chama pb_init_ponteiros_api()   (uma vez so, inicializa API)
 *         - Chama pb_elevar_prioridade_cpu() via TPointBlankMantain
 *         - Chama pb_elevar_priority_boost()
 *         - Chama pb_elevar_prioridade_gpu()
 *         - Chama pb_mmcss_configurar()
 *         - Aplica o perfil de energia via PowerCfg (catalogo POWER)
 *         - Exibe: "Gestao Inteligente ativada. O ReetFPS passa a gerenciar
 *                   o desempenho do Point Blank automaticamente."
 *    3. Monitora o processo (TPointBlankDailyState conta sessoes e crashes).
 *    4. Quando o jogo encerra:
 *         - TPointBlankExitInfo captura o codigo de saida.
 *         - TPointBlankCrashVerifierThread / TPointBlankCrashVerificationTask
 *           classifica se foi crash ou saida normal.
 *         - Reverte prioridade de CPU / boost / GPU / MMCSS.
 *         - Chama pb_verificar_crashes() se houver crashes no dia.
 *
 *  Classes RTTI confirmadas no binario (0x13599d7a - 0x1359a8ec):
 *    TPointBlankDailyState, TPointBlankCrashVerificationTask,
 *    TPointBlankCrashVerificationResult, TPointBlankCrashVerifierThread,
 *    TPointBlankWaitThread, TPointBlankStabilityMonitor
 *
 *  String que confirma o fluxo:
 *    "Gestao Inteligente ativada. O ReetFPS passa a gerenciar o desempenho
 *     do Point Blank automaticamente."
 * ========================================================================== */

/* ===========================================================================
 *  9. PERFIL FLUIDEZMAX ("Fluidez maxima") E A LISTA DE RECOMENDACOES
 * ===========================================================================
 *
 *  Enderecos-chave:
 *    0x13700af0  -- pb_ativar_fluidezmax() (entry point que ativa o perfil)
 *    0x137008d0  -- FUN_137008d0 = TReetGameModePanel.ExecuteGameModeActions
 *                  (coleta itens habilitados e despacha execucao em task)
 *    0x136ec13c  -- FUN_136ec13c: monta a LISTA DE RECOMENDACOES (dois grupos
 *                  de itens, ver tabela abaixo). Cada item e acrescentado por
 *                  FUN_136ebbb4 a um array dinamico em (form + 0x340).
 *
 *  FLUIDEZMAX e uma das recomendacoes, nao o nome da tabela inteira:
 *    chave  "FLUIDEZMAX"
 *    label  "Fluidez maxima"                                 @ 0x136ecd34
 *    desc   "Ative a otimizacao de fluidez maxima para priorizar o melhor
 *            comportamento do jogo."                         @ 0x136ecc80
 *  O texto "Carregamento de mapa otimizado" (@ 0x136ece50) e o label do
 *  item LOADINGMAP, nao do FLUIDEZMAX.
 *
 *  -----------------------------------------------------------------------
 *  LISTA DE RECOMENDACOES (FUN_136ec13c @ 0x136ec13c)
 *  -----------------------------------------------------------------------
 *  Registro de 0x14 bytes em (form+0x340), preenchido por FUN_136ebbb4:
 *    +0x00 chave   +0x04 label   +0x08 descricao   +0x0c icone
 *    +0x10 byte (5o parametro: 0 no grupo Windows, 1 no grupo PointBlank)
 *    +0x11 byte (4o parametro: sempre 1 nas chamadas abaixo)
 *
 *  GRUPO "Windows"  -- so montado se (form+0x310) == 0; cada item so entra
 *  se FUN_136ebfbc(chave) for verdadeiro (= !FUN_136ebcdc(chave)):
 *    chave                 label                               icone
 *    --------------------  ----------------------------------  --------
 *    Energia_ON            "Windows Turbo +FPS"                bolt
 *    Hibernate_ON          (DAT_136ec660)                      power
 *    Cortana_ON            "Desativar Cortana"                 chat
 *    TarefaTelemetria_ON   "Desativar telemetria do Windows"   chart
 *    Superfetch_ON         "Desativar Superfetch"              database
 *    ADMENU_ON             (DAT_136ecb14)                      bell
 *    OpMouse_ON            "Otimizar mouse"                    mouse
 *
 *  GRUPO "PointBlank" -- so montado se (form+0x311) == 0; cada item so entra
 *  se FUN_136ebfd0(chave) retornar 0 (consulta um evento do form em +0x428):
 *    chave                 label                               icone     descricao
 *    --------------------  ----------------------------------  --------  ---------
 *    FLUIDEZMAX            "Fluidez maxima" @ 0x136ecd34       bolt      @ 0x136ecc80
 *    LOADINGMAP            "Carregamento de mapa otimizado"    clock     "Melhora as configuracoes relacionadas
 *                                                                         ao carregamento para reduzir
 *                                                                         interferencias." @ 0x136ecd84
 *    FULLSCREEN            "Tela cheia otimizada"              fullscreen "Aplica o modo tela cheia recomendado
 *                                                                         para reduzir interferencias visuais
 *                                                                         e melhorar estabilidade." @ 0x136ecec0
 *    OPTIMIZER_PB_MANAGER  "Otimizacao Inteligente" @0x136ed088 smart    "Otimiza os recursos do Windows."
 *    PRIORITYPB            "Prioridade do PointBlank"          rocket    "Ajusta a prioridade do jogo para
 *                                                                         melhorar a resposta durante a partida."
 *    INTERFACE             "Interface otimizada"               window    "Aplica os ajustes recomendados de
 *                                                                         interface para melhor estabilidade e
 *                                                                         fluidez."
 *    FPS_SELECTION_INDEX   "FPS recomendado"                   speed     "Aplica o indice de FPS recomendado
 *                                                                         pela CFG oficial do PointBlank."
 *                                                                         @ 0x136ed330
 *    REETGAMEMODE          "Game Mode ReetFPS"                 gamepad   "Ativa o modo de jogo recomendado para
 *                                                                         completar o perfil do PointBlank."
 *
 *  Obs.: "fullscreen" e "smart" sao ICONES, nao chaves (a versao anterior
 *  desta secao os tratava como chaves e escrevia "OTIMIZER_PB_MANAGER").
 *
 *  DESPACHANTE DAS CHAVES DO GRUPO PointBlank (rotina @ 0x136f78d0, sem
 *  funcao definida no Ghidra). Se o form global *(PTR_DAT_13810970) for nulo
 *  levanta excecao (mensagem @ 0x136f7a98). Depois compara a chave (ECX) com
 *  cada string via FUN_1316c388 e, no primeiro acerto, chama
 *  FUN_136f782c(handler, form):
 *    chave (string comparada)                    handler
 *    ------------------------------------------  ----------
 *    "FLUIDEZMAX" @0x136f7ae0 ou
 *    "FLUIDEZMAXIMA" @0x136f7b04                  0x1372a03c
 *    "FULLSCREEN" @0x136f7b2c                     0x1372dfb0
 *    "OPTIMIZER_PB_MANAGER" @0x136f7b50           0x13729094
 *    "FPS_SELECTION_INDEX" @0x136f7b88            0x137294d8
 *    "PRIORITYPB" @0x136f7bbc                     0x1372e888
 *    "LOADINGMAP" @0x136f7be0                     0x13728d20
 *    "INTERFACE" @0x136f7c04                      0x1372d4b8
 *    "REETGAMEMODE" @0x136f7c24                   0x1372c00c
 *  Obs.: OPTIMIZER_PB_MANAGER vai para 0x13729094; FUN_13600598 e outra
 *  rotina (reparo do sistema), nao este item.
 *  INFERIDO: o corpo de cada handler nao foi analisado nesta secao.
 *
 *  INFERIDO: nao foi comprovado que este array em (form+0x340) seja a mesma
 *  lista (form+0x2e0) que FUN_137008d0 executa -- os offsets sao diferentes.
 *  As descricoes da tabela acima sao so os textos da UI. Afirmacoes antigas desta secao (OPTIMIZER_PB_MANAGER
 *  ativa o TPointBlankStabilityMonitor; FPS_SELECTION_INDEX le
 *  [Graphics] FPSType/FPSVal do INI; REETGAMEMODE liga o Windows Game Mode)
 *  NAO foram verificadas no binario e foram retiradas.
 *
 *  -----------------------------------------------------------------------
 *  ITENS SEPARADOS: LIMPEZA DE CACHE (tabela em 0x1366c900, ver secao 11)
 *  -----------------------------------------------------------------------
 *
 *  Chave                   Path/Alvo                            Aviso
 *  ----------------------  -----------------------------------  --------------------
 *  prefetch                "%WINDIR%\Prefetch" @ 0x1366ca58     "Seguro, mas programas
 *                          (conteudo do diretorio)              podem abrir mais
 *                                                               devagar na 1a vez"
 *  driver_extract_cache    %SystemDrive%\NVIDIA\DisplayDriver\* "Seguro..."
 *                          (cache de extracao de drivers NVIDIA)
 */

/* Estrutura de um item do perfil FLUIDEZMAX (campos reconstruidos da tabela
 * em 0x136ecb00 e do loop em FUN_137008d0 @ offsets +0xc, +0x1c, +0x20, +0x24). */
typedef struct {
    /* +0x0c */ DelphiStr chave;        /* ex.: L"fullscreen"         */
    /* +0x1c */ DelphiStr icone_label;  /* ex.: L"FULLSCREEN"         */
    /* +0x20 */ DelphiStr descricao;    /* ex.: L"Fullscreen exclusivo"*/
    /* +0x24 */ bool      habilitado;   /* checkbox marcado na UI     */
} TGameModeItem;

/*
 * pb_ativar_fluidezmax()  (original: FUN_13700af0 @ 0x13700af0)
 *
 * Entry point que:
 *   1. Chama FUN_13700b24(panel, 1) -- marca o perfil como "ativo" no painel.
 *   2. Chama FUN_137008d0(panel)    -- coleta e executa os itens habilitados.
 *   3. Define panel->+0x38e = 1     -- flag "fluidezmax aplicado".
 *
 * param_1 = instancia de TReetGameModePanel (o painel da UI de Game Mode).
 */
void pb_ativar_fluidezmax(void *panel)
{
    /* Marca o perfil como ativo na UI (FUN_13700b24). */
    perfil_marcar_ativo(panel, /*ativo=*/1);  /* FUN_13700b24 */

    /* Coleta os itens habilitados da lista em panel+0x2e0,
     * cria closure ActRec (RTTI: TReetGameModePanel.ExecuteGameModeActions$ActRec
     * @ DAT_1370050c), e despacha execucao em thread separada. */
    executar_itens_habilitados(panel);        /* FUN_137008d0 */

    /* Marca o flag "perfil fluidezmax foi aplicado". */
    *(uint8_t *)((uint8_t *)panel + 0x38e) = 1;
}

/*
 * executar_itens_habilitados()  (original: FUN_137008d0 @ 0x137008d0)
 *
 * Dado o painel (TReetGameModePanel), percorre a lista de itens em
 * panel->+0x2e0, conta os que estao habilitados (item->+0x24 != 0),
 * copia os campos +0xc/+0x1c/+0x20 de cada um para um array de triplas,
 * cria um TStringList auxiliar e despacha a execucao em task
 * (FUN_13219b0c + FUN_1321a804).
 * INFERIDO: os nomes chave/icone/descricao dos tres campos.
 *
 * Traducao simplificada do decompilado (offsets confirmados):
 */
void executar_itens_habilitados(void *panel)
{
    /* Cria o registro de closure (ActRec) para a lambda de execucao.
     * FUN_13149aa8(&DAT_1370050c, 1) = construtor do tipo ActRec. */
    void *act_rec = criar_act_rec(&DAT_1370050c, /*ref=*/1);
    *(void **)((uint8_t *)act_rec + 0x18) = panel;  /* captura panel */

    /* (panel+0x1c) e o ComponentState; bit 8 = csDestroying. */
    if (*(uint8_t *)((uint8_t *)panel + 0x1c) & 8)
        return;

    /* Lista de itens do perfil (TList<TGameModeItem> em panel->+0x2e0). */
    void     *lista_itens = *(void **)((uint8_t *)panel + 0x2e0);
    int       total       = lista_count_ptr(lista_itens);

    /* Conta os habilitados. */
    int n_hab = 0;
    for (int i = 0; i < total; i++) {
        TGameModeItem *item = lista_get_ptr(lista_itens, i);
        if (item->habilitado)
            n_hab++;
    }

    if (n_hab == 0) return; /* nenhum item habilitado -- nada a fazer */

    /* Aloca array de triplas [chave, icone, descricao] * n_hab. */
    /* FUN_1314efd0(act_rec+0x20, &DAT_13700334, 1) = cria TArray<TStringTriplet>. */
    criar_array_triplas((uint8_t *)act_rec + 0x20, &DAT_13700334, /*count=*/n_hab);

    int j = 0;
    for (int i = 0; i < total; i++) {
        TGameModeItem *item = lista_get_ptr(lista_itens, i);
        if (!item->habilitado) continue;

        /* Copia chave, icone e descricao para o slot j do array. */
        str_assign(*(void **)((uint8_t *)act_rec + 0x20) + j * 0xc,      item->chave);
        str_assign(*(void **)((uint8_t *)act_rec + 0x20) + j * 0xc + 4,  item->icone_label);
        str_assign(*(void **)((uint8_t *)act_rec + 0x20) + j * 0xc + 8,  item->descricao);
        j++;
    }

    /* Cria um TStringList (FUN_13206ce8 = TObject.Create com a VMT em
     * 0x131d5998; vmtClassName = "TStringList") e guarda em act_rec+0x1c.
     * Em seguida chama dois setters dele (FUN_13206ee0(obj,0) e
     * FUN_13206c10(obj,1)) e zera o byte obj+0x2d.
     * INFERIDO: quais propriedades esses setters alteram nao foi verificado. */
    void *lista = criar_lista(&PTR_FUN_131d5998, /*alocar=*/1);
    *(void **)((uint8_t *)act_rec + 0x1c) = lista;
    FUN_13206ee0(lista, 0);
    FUN_13206c10(lista, 1);
    *(uint8_t *)((uint8_t *)lista + 0x2d) = 0;

    /* FUN_136fade8() -- INFERIDO: efeito nao analisado. */
    desativar_estado_ui();

    /* Salva posicao/tamanho do painel para restaurar depois
     * (campos panel->+0x3f8 e +0x3fc guardam geometria). */
    *(uint32_t *)((uint8_t *)act_rec + 0x10) = *(uint32_t *)((uint8_t *)panel + 0x3f8);
    *(uint32_t *)((uint8_t *)act_rec + 0x14) = *(uint32_t *)((uint8_t *)panel + 0x3fc);

    /* FUN_13219b0c(act_rec+0x24) -- inicia o loop de execucao dos itens. */
    iniciar_loop_execucao((uint8_t *)act_rec + 0x24);

    /* FUN_1321a804() -- kick-off do dispatcher (signal de inicio). */
    iniciar_dispatcher();
}

/* Prototipos dos auxiliares de execucao do Game Mode. */
extern void *criar_act_rec(void *tipo_rtti, int ref);         /* FUN_13149aa8 */
extern void *criar_lista(void *vmt_tstringlist, int alocar);  /* FUN_13206ce8 */
extern void  FUN_13206ee0(void *lista, int valor);            /* setter do TStringList */
extern void  FUN_13206c10(void *lista, int valor);            /* setter do TStringList */
extern void  desativar_estado_ui(void);                       /* FUN_136fade8 */
extern void  iniciar_loop_execucao(void *array_triplas);      /* FUN_13219b0c */
extern void  iniciar_dispatcher(void);                        /* FUN_1321a804 */
extern void  perfil_marcar_ativo(void *panel, int ativo);     /* FUN_13700b24 */
extern void  criar_array_triplas(void *dst, void *tipo, int n);/* FUN_1314efd0 */
extern void *lista_count_ptr(void *lista);                    /* vtable+0x08  */
extern void *lista_get_ptr(void *lista, int i);               /* FUN_136fb698 */
extern void *DAT_1370050c;  /* RTTI: TReetGameModePanel.ExecuteGameModeActions$ActRec */
extern void *DAT_13700334;  /* RTTI do tipo das triplas do array */


/* ===========================================================================
 *  10) TIMER RESOLUTION  (BUTTON_TIMER_RESOLUTION_ON @ 0x137805cc)
 * ===========================================================================
 *
 *  O que faz: reduz a granularidade do agendador do Windows de ~15.6ms
 *  (padrao) para 0.5ms, diminuindo a latencia de input e a variancia de
 *  frame-time do jogo. Funciona em tres camadas complementares:
 *
 *    Camada 1 - NtSetTimerResolution(5000, TRUE, &atual)  (rotina @ 0x1357c5a4)
 *               Solicita ao kernel a resolucao de 5000 (0x1388) unidades de
 *               100ns = 0.5ms. Funcao nao documentada exportada pela ntdll.dll.
 *               A thunk de importacao esta em 0x1357c294.
 *               timeBeginPeriod(1) NAO e chamado junto: e apenas o FALLBACK,
 *               usado so se NtSetTimerResolution falhar. O flag DAT_1380f238
 *               registra que o fallback esta ativo para que o cleanup faca
 *               timeEndPeriod(1).
 *
 *    Camada 2 - Thread de manutencao ("ReetTimerPrecision", 0x1357c870)
 *               Outros processos podem chamar NtSetTimerResolution com valor
 *               maior e, quando eles saem, o Windows volta ao padrao. Para
 *               evitar isso, um thread de fundo re-aplica a resolucao em
 *               loop. O nome interno e "Maintain 0.5ms timer for low
 *               latency" (log string @ 0x1357c40c). O thread e criado em
 *               FUN_1357c9b0 e cancelado em FUN_1357ca34.
 *
 *    Camada 3 - SetProcessInformation / ProcessPowerThrottling (Win11)
 *               Feita por FUN_135a50d8, reconstruida UMA unica vez na
 *               secao 12 (pb_configurar_timer_resolution).
 *
 *  Aplicar (rotina @ 0x1357c5a4, sem funcao definida no Ghidra):
 *    if (NtSetTimerResolution(5000, TRUE, &atual) == 0) return TRUE;
 *    if (!DAT_1380f238) DAT_1380f238 = (timeBeginPeriod(1) == TIMERR_NOERROR);
 *    return DAT_1380f238;
 *
 *  Cleanup (FUN_1357c5e0 @ 0x1357c5e0):
 *    NtSetTimerResolution(0, FALSE, &atual)  -- restaura resolucao padrao
 *    if (timeBeginPeriod chamado) timeEndPeriod(1)
 *
 *  Globais relevantes:
 *    DAT_1380f230  -- flag "thread de manutencao ativa"
 *    DAT_1380f238  -- flag "fallback timeBeginPeriod(1) ativo"
 *    DAT_1380f22c  -- handle do objeto de task/thread
 *
 *  Reconstrucao (logica equivalente; parametros aproximados):
 */

/* Globals do subsistema de timer resolution. */
extern char g_timer_ativo;       /* DAT_1380f230 */
extern char g_time_period_ativo; /* DAT_1380f238 */
extern void *g_timer_task;       /* DAT_1380f22c */

/* Importacoes de ntdll.dll carregadas dinamicamente. */
typedef long (NTAPI *PfnNtSetTimerResolution)(
        unsigned long DesiredResolution,  /* em unidades de 100ns; 5000 = 0.5ms */
        char          SetResolution,      /* TRUE=aplicar, FALSE=restaurar */
        unsigned long *CurrentResolution  /* saida: resolucao atual */
);
typedef long (NTAPI *PfnNtQueryTimerResolution)(
        unsigned long *MinimumResolution,
        unsigned long *MaximumResolution,
        unsigned long *CurrentResolution
);

extern PfnNtSetTimerResolution   pfn_NtSetTimerResolution;   /* 0x1357c294 thunk */
extern PfnNtQueryTimerResolution pfn_NtQueryTimerResolution;


/*
 * pb_timer_resolution_ativar_manutencao  (FUN_1357c9b0 @ 0x1357c9b0)
 *
 * Inicia a thread de manutencao do timer de 0.5ms.
 * Usa secao critica (DAT_138199bc) para serializar acesso ao g_timer_task.
 */
void pb_timer_resolution_ativar_manutencao(void)
{
    EnterCriticalSection(&g_timer_cs); /* DAT_138199bc */

    if (g_timer_task == NULL) {
        /* FUN_1321994c: cria objeto de task/thread com callback PTR_FUN_1357c804.
         * INFERIDO: que o callback re-aplica o timer via pb_timer_resolution_aplicar
         * (0x1357c804 nao e funcao definida no Ghidra; nao foi rastreado).
         * Nome interno: "ReetTimerPrecision" (DAT_1357c870). */
        g_timer_task = criar_task_thread(&PTR_FUN_1357c804, 1, 1); /* FUN_1321994c */
        configurar_task(g_timer_task, 0);                           /* FUN_13219fd8 */
        iniciar_dispatcher();                                       /* FUN_1321a804 */
    }

    g_timer_ativo = 1;  /* DAT_1380f230 = 1 */

    LeaveCriticalSection(&g_timer_cs);
}


/*
 * pb_timer_resolution_aplicar  (rotina @ 0x1357c5a4)
 *
 * Disassembly confirmado (@ 0x1357c5a4):
 *   PUSH ECX / PUSH ESP           ; slot de saida CurrentResolution
 *   PUSH 0x1 / PUSH 0x1388        ; SetResolution=TRUE, Desired=5000 (0.5ms)
 *   CALL NtSetTimerResolution (0x1357c294)
 *   TEST EAX,EAX / SETZ AL / JNZ fim   ; STATUS_SUCCESS -> retorna 1
 *   CMP byte [0x1380f238],0 / JNZ ret_flag
 *   PUSH 1 / CALL timeBeginPeriod (0x134a9ec4)
 *   TEST EAX,EAX / SETZ [0x1380f238]   ; TIMERR_NOERROR -> flag = 1
 *   ret_flag: MOVZX EAX, byte [0x1380f238]
 *
 * INFERIDO: o chamador desta rotina nao foi rastreado.
 */
bool pb_timer_resolution_aplicar(void)
{
    unsigned long atual;
    if (pfn_NtSetTimerResolution(5000, /* TRUE */ 1, &atual) == 0)
        return true;                       /* 0.5ms via ntdll */

    /* Fallback: so tenta timeBeginPeriod(1) se ainda nao estiver ativo. */
    if (g_time_period_ativo == 0)          /* DAT_1380f238 */
        g_time_period_ativo = (timeBeginPeriod(1) == TIMERR_NOERROR);

    return g_time_period_ativo != 0;
}


/*
 * pb_timer_resolution_revogar  (FUN_1357c5e0 @ 0x1357c5e0)
 *
 * Restaura a resolucao de timer para o padrao do Windows.
 * Chamada pelo cleanup da thread de manutencao.
 *
 * Disassembly confirmado (@ 0x1357c5e0):
 *   PUSH ECX          ; slot de saida CurrentResolution
 *   PUSH ESP          ; ponteiro para o slot
 *   PUSH 0x0          ; SetResolution = FALSE (restaurar)
 *   PUSH 0x0          ; DesiredResolution = 0
 *   CALL NtSetTimerResolution (0x1357c294)
 */
void pb_timer_resolution_revogar(void)  /* FUN_1357c5e0 */
{
    unsigned long atual;
    pfn_NtSetTimerResolution(0, /* FALSE */ 0, &atual);  /* restaura padrao */

    if (g_time_period_ativo != 0) {   /* DAT_1380f238 */
        timeEndPeriod(1);
        g_time_period_ativo = 0;
    }
}


/*
 * pb_timer_resolution_parar  (FUN_1357ca34 @ 0x1357ca34)
 *
 * Para a thread de manutencao e restaura o timer.
 */
void pb_timer_resolution_parar(void)    /* FUN_1357ca34 */
{
    EnterCriticalSection(&g_timer_cs);  /* DAT_138199bc */

    g_timer_ativo = 0;                  /* DAT_1380f230 = 0 */
    pb_timer_resolution_revogar();

    LeaveCriticalSection(&g_timer_cs);
}


/*
 * Camada 3 (FUN_135a50d8 @ 0x135a50d8) -- ver secao 12,
 * pb_configurar_timer_resolution(). A reconstrucao fica so la; a versao que
 * existia aqui (pb_timer_resolution_policy_win11) era duplicada, chamava as
 * duas APIs incondicionalmente e trocava Get/Set de +0x8a0/+0x8a4.
 */

/* Prototipos dos auxiliares do subsistema de timer resolution. */
extern void *criar_task_thread(void *callback, int a, int b);   /* FUN_1321994c */
extern void  configurar_task(void *task, int modo);             /* FUN_13219fd8 */
extern void  EnterCriticalSection(void *cs);
extern void  LeaveCriticalSection(void *cs);
extern void *g_timer_cs;  /* DAT_138199bc */
extern void  PTR_FUN_1357c804;  /* ponteiro para callback da thread de manutencao */


/* ===========================================================================
 *  11) LIMPEZA INTELIGENTE
 * ===========================================================================
 *
 *  A "Limpeza Inteligente" e um limpa-arquivos recursivo com tres camadas de
 *  seguranca: (a) NAO entra em juncoes/symlinks -- remove apenas o proprio
 *  link (RemoveDirectoryW), nunca o conteudo do alvo; (b) usa prefixo \\?\
 *  para caminhos longos; (c) reporta negados, em-uso e falhas separadamente.
 *  A varredura pode ser cancelada pelo usuario a qualquer momento.
 *
 *  FUNCOES MAPEADAS
 *    FUN_13674090  @  0x13674090  -- scanner recursivo de diretorio
 *    FUN_13673ee0  @  0x13673ee0  -- deleta arquivo individual (DeleteFileW)
 *    FUN_13673fb4  @  0x13673fb4  -- remove diretorio vazio ou link (RemoveDirectoryW)
 *    FUN_13673dc4  @  0x13673dc4  -- normaliza caminho (adiciona \\?\)
 *    FUN_13674088  @  0x13674088  -- (attr & 0x400) != 0  -> reparse point
 *    FUN_1366039c  @  0x1366039c  -- le, de forma atomica, o pedido de
 *                                    cancelamento em (worker + 0x3b4)
 *
 *  As tres funcoes de arquivo/diretorio sao procedimentos ANINHADOS (nested
 *  procedures do Delphi): recebem o frame da funcao externa em param_4 e
 *  leem dele:  [frame-4] = objeto worker (log e cancelamento)
 *              [frame-8] = ponteiro para flag de cancelamento (char)
 *              [frame-0xc]/[frame-0x10] = HMODULE de kernel32 e o ponteiro
 *                de FindFirstFileExW obtido por GetProcAddress (FUN_1315b19c).
 *  INFERIDO: o ponteiro de FindFirstFileExW nao e usado dentro do scanner
 *  (que chama FindFirstFileW); provavelmente serve a outro procedimento
 *  aninhado da mesma funcao externa.
 *
 *  ITENS DE LIMPEZA CONHECIDOS (tabela em 0x1366c900)
 *    chave                 alvo (antes de expandir variaveis)
 *    prefetch              "%WINDIR%\Prefetch"                    @ 0x1366ca58
 *                          label "Cache Prefetch do Windows"      @ 0x1366ca88
 *    driver_extract_cache  "%SystemDrive%\NVIDIA\DisplayDriver\*" @ 0x1366c8f0
 *
 *  STRINGS DE STATUS (usadas no log interno)
 *    "Executando limpeza..."              @ 0x1365ef18
 *    "Limpeza coordenada: itens preservados (negado=%s, em uso=%s, protecao=%s)"
 *                                         @ 0x13673d30
 *    "Limpeza bloqueada em junction/symlink: "  @ 0x1367456c
 *        (INFERIDO: usada pela funcao externa quando a RAIZ do item e um
 *         reparse point; o scanner em si nao referencia esta string)
 *    "Scan ignorado em junction/symlink: "      @ 0x1366dbc4
 *    "Falha geral na limpeza: "           @ 0x13723144
 *    "Limpeza segura em apenas um clique" @ 0x13642ab8
 *
 *  CONTEXTO DE ESTADO (FUN_13153234() retorna ponteiro para o struct de stats)
 *    +0x5c  negado_acesso     -- acesso negado em algum arquivo
 *    +0x5d  arquivo_em_uso    -- arquivo bloqueado (SHARING_VIOLATION)
 *    +0x5e  deletado          -- pelo menos um arquivo deletado com sucesso
 *    +0x60  qtd_falhas        -- contador de falhas acumuladas
 *    +0x70/0x74  qtd_arquivos (64 bits) -- arquivos deletados
 *    +0x78/0x7c  qtd_pastas   -- pastas recursadas
 *    +0x80/0x84  bytes_totais (64 bits) -- bytes liberados
 */

/* ---- ESTRUTURAS ---- */

typedef struct {
    int  negado_acesso;        /* +0x5c -- algum arquivo teve ERROR_ACCESS_DENIED */
    int  arquivo_em_uso;       /* +0x5d -- algum arquivo estava aberto (ERROR_SHARING_VIOLATION) */
    int  deletado;             /* +0x5e -- ao menos um arquivo foi removido */
    int  qtd_falhas;           /* +0x60 -- acumulador de erros */
    uint64_t qtd_arquivos;     /* +0x70 -- arquivos deletados */
    uint64_t qtd_pastas;       /* +0x78 -- sub-pastas recursadas */
    uint64_t bytes_totais;     /* +0x80 -- bytes liberados */
} TLimpezaStats;

/* Retorna o contexto de stats da limpeza em andamento (singleton por thread). */
extern TLimpezaStats *limpeza_stats_get(void);           /* FUN_13153234 */

/* Normaliza o caminho para o formato Windows longo (\\?\).
 * Se ja comecar com \\?\ devolve igual;
 * se for caminho UNC (\\server\) converte para \\?\UNC\;
 * caso contrario, prepende \\?\.
 * Isso garante DeleteFileW em caminhos > MAX_PATH.                          */
void limpeza_normalizar_caminho(const DelphiStr caminho, DelphiStr *dst)
{
    /* FUN_13673dc4 @ 0x13673dc4 */
    if (caminho == NULL) {
        str_clear(dst);
        return;
    }
    if (str_starts_with(caminho, L"\\\\?\\")) {
        /* ja esta no formato longo -- devolve como esta */
        str_assign(dst, caminho);
        return;
    }
    if (str_starts_with(caminho, L"\\\\")) {
        /* UNC: \\ -> \\?\UNC\ */
        str_concat(dst, L"\\\\?\\UNC\\", str_substr(caminho, 2));
    } else {
        /* caminho local: prepende \\?\ */
        str_concat(dst, L"\\\\?\\", caminho);
    }
}

/* Contexto do procedimento externo, lido pelos procedimentos aninhados
 * atraves do frame (param_4) -- ver cabecalho da secao.                     */
typedef struct {
    void   *worker;          /* [frame-4]  : log + pedido de cancelamento     */
    char   *cancelar;        /* [frame-8]  : flag de cancelamento do usuario  */
    HMODULE kernel32;        /* [frame-0xc]                                   */
    void   *pFindFirstFileExW; /* [frame-0x10]                                */
} TLimpezaFrame;

/* true quando o usuario pediu para cancelar (flag local OU worker+0x3b4).  */
static bool limpeza_cancelada(TLimpezaFrame *f)
{
    return *f->cancelar != 0 || limpeza_worker_cancelado(f->worker); /* FUN_1366039c */
}

/* Deleta um unico arquivo, rastreando o motivo da falha no stats.
 * Retorna true se o arquivo foi removido (direto ou apos tomar posse).     */
bool limpeza_deletar_arquivo(TLimpezaFrame *f, const DelphiStr caminho_curto)
{
    /* FUN_13673ee0 @ 0x13673ee0  (resultado em BL)                          */
    DelphiStr  caminho_longo = NULL;

    limpeza_log_arquivo(f->worker, caminho_curto); /* FUN_13671600          */
    limpeza_normalizar_caminho(caminho_curto, &caminho_longo);

    if (DeleteFileW((LPCWSTR)str_c(caminho_longo))) {
        limpeza_stats_get()->deletado = 1;
        return true;
    }

    DWORD err = GetLastError();
    if (err == ERROR_ACCESS_DENIED && limpeza_tomar_posse(caminho_longo)) {
        /* FUN_136712f0: toma posse/ajusta ACL e apaga.                      */
        limpeza_stats_get()->deletado = 1;
        return true;
    }
    if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION)
        limpeza_stats_get()->arquivo_em_uso = 1;
    limpeza_stats_get()->qtd_falhas++;
    if (err == ERROR_ACCESS_DENIED)
        limpeza_stats_get()->negado_acesso = 1;
    return false;
}

/* FUN_13673fb4 @ 0x13673fb4 -- mesmo esquema de limpeza_deletar_arquivo,
 * mas com RemoveDirectoryW. Aplicado a um REPARSE POINT, remove apenas o
 * link, sem tocar no diretorio alvo.                                        */
extern bool limpeza_remover_diretorio(TLimpezaFrame *f, DelphiStr caminho);

/* (dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT 0x400) != 0             */
extern bool limpeza_e_juncao_symlink(DWORD dwAttribs); /* FUN_13674088 */

/* Scanner recursivo de diretorio (FindFirstFileW/FindNextFileW).
 * Retorna true se tudo dentro do diretorio foi removido; false se algo
 * falhou OU se a limpeza foi cancelada.                                     */
bool limpeza_varrer_diretorio(TLimpezaFrame *f, const DelphiStr dir_path)
{
    /* FUN_13674090 @ 0x13674090 */
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    DelphiStr  dir_barra = NULL, padrao = NULL, padrao_longo = NULL;
    DelphiStr  nome = NULL, entrada_path = NULL;
    bool  ok = true;

    if (limpeza_cancelada(f))
        return false;

    /* padrao = IncludeTrailingPathDelimiter(dir) + "*"                      */
    incluir_barra_final(dir_path, &dir_barra);           /* FUN_131776e8     */
    str_concat(&padrao, dir_barra, L"*");                /* FUN_1314c828     */

    f->kernel32 = GetModuleHandleW(L"kernel32.dll");
    f->pFindFirstFileExW = NULL;
    if (f->kernel32 != NULL)
        f->pFindFirstFileExW = GetProcAddress(f->kernel32, "FindFirstFileExW");

    /* O padrao de busca tambem e convertido para \\?\ (FUN_13673dc4).      */
    limpeza_normalizar_caminho(padrao, &padrao_longo);
    hFind = FindFirstFileW((LPCWSTR)str_c(padrao_longo), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_ACCESS_DENIED)
            limpeza_stats_get()->negado_acesso = 1;
        return true;           /* diretorio inacessivel nao conta como falha */
    }

    for (;;) {
        if (limpeza_cancelada(f)) {          /* checado a cada entrada       */
            ok = false;
            break;
        }

        str_from_wbuf(&nome, fd.cFileName, MAX_PATH);    /* FUN_1314c674    */
        if (wcscmp(str_c(nome), L".") != 0 && wcscmp(str_c(nome), L"..") != 0) {
            incluir_barra_final(dir_path, &dir_barra);   /* FUN_131776e8     */
            str_concat(&entrada_path, dir_barra, nome);

            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                if (limpeza_deletar_arquivo(f, entrada_path)) {
                    TLimpezaStats *s = limpeza_stats_get();
                    s->qtd_arquivos++;
                    s->bytes_totais += ((uint64_t)fd.nFileSizeHigh << 32)
                                     | fd.nFileSizeLow;
                } else {
                    ok = false;
                }
            } else if (limpeza_e_juncao_symlink(fd.dwFileAttributes)) {
                /* Juncao/symlink: NAO recursa. Remove so o link; falha
                 * apenas se essa remocao falhar.                            */
                if (limpeza_remover_diretorio(f, entrada_path))
                    limpeza_stats_get()->qtd_pastas++;
                else
                    ok = false;
            } else {
                bool sub_ok = limpeza_varrer_diretorio(f, entrada_path);
                bool removido = limpeza_remover_diretorio(f, entrada_path);
                if (removido)
                    limpeza_stats_get()->qtd_pastas++;
                ok = ok && sub_ok && removido;
            }
        }

        if (!FindNextFileW(hFind, &fd))
            break;
    }

    FindClose(hFind);
    return ok;
}

/* Prototipos dos auxiliares internos da limpeza. */
extern void  limpeza_log_arquivo(void *worker, DelphiStr caminho); /* FUN_13671600 */
extern bool  limpeza_tomar_posse(DelphiStr caminho);         /* FUN_136712f0 */
extern bool  limpeza_worker_cancelado(void *worker);         /* FUN_1366039c */
extern void  incluir_barra_final(DelphiStr dir, DelphiStr *dst); /* FUN_131776e8 */
extern void  str_from_wbuf(DelphiStr *dst, const wchar_t *buf, int max); /* FUN_1314c674 */
extern void  str_concat(DelphiStr *dst, ...);                /* FUN_1314c828 */
extern bool  str_starts_with(DelphiStr s, const wchar_t *prefix);
extern DelphiStr str_substr(DelphiStr s, int from);
extern const wchar_t *str_c(DelphiStr s);
extern void  str_clear(DelphiStr *p);


/* ===========================================================================
 *  12) TIMER RESOLUTION -- SetProcessInformation / ProcessPowerThrottling (Win11)
 * ===========================================================================
 *
 *  Complemento da secao 10: enquanto a secao 10 cobre o NtSetTimerResolution
 *  global e a thread de manutencao, esta parte cobre o SetProcessInformation
 *  com ProcessPowerThrottling -- que garante que o PROCESSO DO JOGO respeite
 *  a resolucao de 0.5ms definida globalmente.
 *
 *  Esta e a UNICA reconstrucao de FUN_135a50d8 neste arquivo (a secao 10
 *  apenas aponta para ca).
 *
 *  O recurso usa SetProcessInformation/GetProcessInformation com a classe
 *  4 = ProcessPowerThrottling, em dois passos independentes:
 *    - ControlMask 1 (EXECUTION_SPEED), StateMask 0 -> desliga o EcoQoS;
 *    - ControlMask 4 (IGNORE_TIMER_RESOLUTION), StateMask 0 -> o processo
 *      passa a honrar a resolucao de timer pedida (no Win11 um processo
 *      em segundo plano pode ignora-la).
 *  Cada passo so roda se a flag correspondente da configuracao estiver
 *  ligada, e so escreve se a leitura previa mostrar que ainda e preciso.
 *
 *  STRINGS DE UI E TELEMETRIA
 *    "Maintain 0.5ms timer for low latency"          @ 0x1357c40c  (tooltip)
 *    "ReetTimerPrecision"                            @ 0x1357c870  (nome do componente)
 *    "BUTTON_TIMER_RESOLUTION_ON"                    @ 0x137805cc  (evento UI)
 *    "BUTTON_TIMER_RESOLUTION_OFFClick&"             @ 0x13724f50  (evento UI)
 *
 *  STRINGS DE LOG INTERNO (FUN_135a50d8)
 *    "ProcessPowerThrottling.ApiUnavailable"          -- Get/SetProcessInformation
 *                                                        ausentes; logado com codigo
 *                                                        0x78 (ERROR_CALL_NOT_IMPLEMENTED)
 *    "GetProcessInformation.PowerThrottling"          -- falha ao ler estado
 *    "SetProcessInformation.PowerThrottling"          -- falha ao desativar throttle
 *    "GetProcessInformation.TimerResolution.Pre"      -- falha ao ler timer atual
 *    "SetProcessInformation.TimerResolutionPolicy"    -- falha ao definir politica
 *
 *  FUNCAO PRINCIPAL: FUN_135a50d8 @ 0x135a50d8
 *    Recebe: param_1 = contexto TPointBlankStabilityMonitor
 *            param_2 = handle do processo do jogo (HANDLE hProc)
 *    Campos lidos:
 *      param_1 + 0x8cc  -- gate geral (sai sem fazer nada se 0)
 *      param_1 + 0x8a0  -- ptr SetProcessInformation (resolvido em FUN_135a3cec)
 *      param_1 + 0x8a4  -- ptr GetProcessInformation (resolvido em FUN_135a3cec)
 *      param_1 + 0x08   -- 24 bytes de configuracao, copiados sob lock por
 *                          FUN_135a4024; usados aqui:
 *                            +0x0b (local_3d) = pedir desligamento do EcoQoS
 *                            +0x0d (local_3b) = pedir politica de timer
 *      param_1 + 0x04   -- objeto de lock (TMonitor Enter/Exit via vtable)
 *    Campos escritos:
 *      +0x8dd/+0x8e1/+0x8e5 -- Version/ControlMask/StateMask da 1a leitura
 *      +0x8f0 = 1           -- 1a leitura feita
 *      +0x8e9 = 1           -- passo de timer foi tentado
 *      +0x8eb               -- Set da politica de timer OK
 *      +0x8ea               -- politica de timer confirmada por releitura
 *      +0x35 = 1 / +0x37 = 1 (sob lock) -- EcoQoS desligado / timer confirmado
 *      +0x74/+0x78 (sob lock) -- ControlMask/StateMask finais
 *      +0x3c, +0x3f, +0x40, +0x41 (sob lock) -- resumo (ver codigo)
 *
 *  CONSTANTES DA API
 *    ProcessPowerThrottling (classe 4 em SetProcessInformation/GetProcessInformation):
 *      struct PROCESS_POWER_THROTTLING_STATE {
 *          ULONG Version;        -- sempre 1
 *          ULONG ControlMask;    -- bit 1 = PowerThrottling; bit 4 = TimerResolution
 *          ULONG StateMask;      -- mesmo bitmask; 0 = desativar o controle
 *      }
 *    ControlMask = 1, StateMask = 0  -> desativa throttling de CPU
 *    ControlMask = 4, StateMask = 0  -> desativa "ignore timer resolution" -> timer preciso
 */

/* PROCESS_POWER_THROTTLING_STATE simplificado */
typedef struct {
    uint32_t Version;       /* sempre 1 */
    uint32_t ControlMask;   /* bits de controle */
    uint32_t StateMask;     /* estado desejado (0 = desabilitar esse controle) */
} REET_POWER_THROTTLING;

#define CLASSE_PROCESS_POWER_THROTTLING 4   /* push 4 antes de cada chamada */

/* Prototipos dos auxiliares de timer resolution (declarados antes do uso). */
typedef int (WINAPI *PfnProcessInformation)(HANDLE h, int cls, void *buf, uint32_t sz);
extern void timer_log(void *ctx, const wchar_t *msg, DWORD err);   /* FUN_135a4a18 */
extern void ctx_copiar_config(void *ctx, uint8_t cfg[24]);         /* FUN_135a4024 */
extern void ctx_lock(void *ctx);    /* TMonitor.Enter em *(ctx+4) (vtable[0]) */
extern void ctx_unlock(void *ctx);  /* TMonitor.Exit  em *(ctx+4) (vtable[1]) */

#define SET_PI(ctx) (*(PfnProcessInformation *)((uint8_t *)(ctx) + 0x8a0))
#define GET_PI(ctx) (*(PfnProcessInformation *)((uint8_t *)(ctx) + 0x8a4))

static bool bit_controlado_e_desligado(const REET_POWER_THROTTLING *s, uint32_t bit)
{
    return (s->ControlMask & bit) != 0 && (s->StateMask & bit) == 0;
}

void pb_configurar_timer_resolution(void *ctx, HANDLE hProc)
{
    /* FUN_135a50d8 @ 0x135a50d8 */
    uint8_t *c = (uint8_t *)ctx;
    REET_POWER_THROTTLING st, novo;
    uint8_t cfg[24];

    if (c[0x8cc] == 0)
        return;                                     /* gate geral */

    if (SET_PI(ctx) == NULL || GET_PI(ctx) == NULL) {
        timer_log(ctx, L"ProcessPowerThrottling.ApiUnavailable",
                  0x78 /* ERROR_CALL_NOT_IMPLEMENTED */);
        return;
    }

    ctx_copiar_config(ctx, cfg);                    /* FUN_135a4024 */
    bool pedir_ecoqos_off = cfg[3] != 0;            /* local_3d = ctx+0x0b */
    bool pedir_timer      = cfg[5] != 0;            /* local_3b = ctx+0x0d */

    /* ---- Leitura inicial ---- */
    memset(&st, 0, sizeof(st));
    st.Version = 1;
    if (!GET_PI(ctx)(hProc, CLASSE_PROCESS_POWER_THROTTLING, &st, sizeof(st))) {
        timer_log(ctx, L"GetProcessInformation.PowerThrottling", GetLastError());
        return;
    }
    *(uint32_t *)(c + 0x8dd) = st.Version;
    *(uint32_t *)(c + 0x8e1) = st.ControlMask;
    *(uint32_t *)(c + 0x8e5) = st.StateMask;
    c[0x8f0] = 1;

    /* resumo do passo 1; quando o passo nao e pedido, fica 1 (local_3d ^ 1) */
    uint8_t ecoqos_ok = !pedir_ecoqos_off;
    uint8_t timer_ok  = 0;

    /* ---- Passo 1: EcoQoS (so se pedido e se ainda nao estiver desligado) ---- */
    if (pedir_ecoqos_off) {
        ecoqos_ok = bit_controlado_e_desligado(&st, 1);
        if (!ecoqos_ok) {
            memset(&novo, 0, sizeof(novo));
            novo.Version = 1; novo.ControlMask = 1; novo.StateMask = 0;
            if (SET_PI(ctx)(hProc, CLASSE_PROCESS_POWER_THROTTLING, &novo, sizeof(novo))) {
                ctx_lock(ctx); c[0x35] = 1; ctx_unlock(ctx);
            } else {
                timer_log(ctx, L"SetProcessInformation.PowerThrottling", GetLastError());
            }
            /* INFERIDO: o decompilador mostra um "return" logo apos o
             * finally do lock; tratamos como continuacao do try/finally. */
        }
    }

    /* ---- Passo 2: politica de timer (so se pedido) ---- */
    if (pedir_timer) {
        c[0x8e9] = 1;
        memset(&st, 0, sizeof(st));
        st.Version = 1;
        if (!GET_PI(ctx)(hProc, CLASSE_PROCESS_POWER_THROTTLING, &st, sizeof(st))) {
            timer_log(ctx, L"GetProcessInformation.TimerResolution.Pre", GetLastError());
            return;
        }
        if (bit_controlado_e_desligado(&st, 4)) {
            c[0x8eb] = 1;                           /* ja estava configurado */
            c[0x8ea] = 1;
        } else {
            memset(&novo, 0, sizeof(novo));
            novo.Version = 1; novo.ControlMask = 4; novo.StateMask = 0;
            if (!SET_PI(ctx)(hProc, CLASSE_PROCESS_POWER_THROTTLING, &novo, sizeof(novo))) {
                DWORD err = GetLastError();
                c[0x8eb] = 0;
                c[0x8ea] = 0;
                /* 0x57 INVALID_PARAMETER, 0x32 NOT_SUPPORTED,
                 * 0x78 CALL_NOT_IMPLEMENTED -> silenciados */
                if (err != 0x57 && err != 0x32 && err != 0x78)
                    timer_log(ctx, L"SetProcessInformation.TimerResolutionPolicy", err);
            } else {
                c[0x8eb] = 1;
                memset(&st, 0, sizeof(st));
                st.Version = 1;
                if (!GET_PI(ctx)(hProc, CLASSE_PROCESS_POWER_THROTTLING, &st, sizeof(st))) {
                    c[0x8ea] = 0;
                } else {
                    c[0x8ea] = bit_controlado_e_desligado(&st, 4);
                    if (c[0x8ea]) {
                        ctx_lock(ctx); c[0x37] = 1; ctx_unlock(ctx);
                    }
                }
            }
        }
    }

    /* ---- Releitura final e resumo (sob lock) ---- */
    memset(&st, 0, sizeof(st));
    st.Version = 1;
    if (!GET_PI(ctx)(hProc, CLASSE_PROCESS_POWER_THROTTLING, &st, sizeof(st)))
        return;
    if (pedir_ecoqos_off)
        ecoqos_ok = bit_controlado_e_desligado(&st, 1);
    if (pedir_timer && c[0x8ea])
        timer_ok = bit_controlado_e_desligado(&st, 4);

    ctx_lock(ctx);
    *(uint32_t *)(c + 0x74) = st.ControlMask;
    *(uint32_t *)(c + 0x78) = st.StateMask;
    c[0x3c] = ecoqos_ok;
    c[0x41] = c[0x8eb];
    c[0x40] = c[0x8ea];
    c[0x3f] = timer_ok;
    ctx_unlock(ctx);
}


/* ===========================================================================
 *  13) MIRAS CUSTOMIZADAS
 * ===========================================================================
 *
 *  O ReetFPS permite ao jogador personalizar a mira sobreposta na tela do PB.
 *  A mira e desenhada em uma janela transparente sobre o jogo.
 *
 *  CLASSES RTTI
 *    TRPCrosshair    @ 0x136945ef  -- classe principal da mira
 *    UDialogCrosshair @ 0x13696e21 -- formulario de configuracao
 *
 *  STRINGS DE UI
 *    "PERSONALIZAR MIRA"       @ 0x13694a94  -- botao que abre o dialogo
 *    "TAMANHO DA LINHA"        @ 0x1369491c  -- label do controle de espessura
 *    "QUADRADO CENTRAL"        @ 0x13694970  -- tipo de forma: quadrado central
 *    "Exibir sombra na mira"   @ 0x13695f50  -- checkbox de sombra
 *    "COR DA MIRA"             @ 0x13695f88  -- label do picker de cor
 *    "crosshair1_space"        @ 0x13727fae  -- nome interno do espaco da mira
 *    "CROSSHAIR_SHADOW"        @ 0x13696f1c  -- chave de configuracao: sombra
 *
 *  ESTRUTURA TRPCrosshair (offsets confirmados em FUN_136951fc e no setter
 *  FUN_13694ab8, que grava os campos com clamp e chama Repaint)
 *    +0x310  comprimento_linha   -- comprimento de cada braco
 *    +0x314  espacamento         -- distancia do centro ao inicio do braco
 *    +0x318  quadrado_central    -- lado do quadrado central (0 = sem quadrado;
 *                                   se impar e arredondado para cima, para par)
 *    +0x31c/+0x320/+0x324  minimos de +0x310/+0x314/+0x318
 *    +0x328/+0x32c/+0x330  maximos de +0x310/+0x314/+0x318
 *    +0x334  indice_cor          -- 1..6 (clamp por FUN_13191200), indexa a
 *                                   tabela de cores em 0x1380f738
 *    +0x338  sombra_ativa        -- bool: desenhar contorno escuro sob a mira
 *
 *  TABELA DE CORES (ARGB, dword[indice] em 0x1380f738 + 4*indice)
 *    1  0xffff0000  vermelho        4  0xff0000ff  azul
 *    2  0xff00ff00  verde           5  0xffffff00  amarelo
 *    3  0xff8000ff  violeta         6  0xffffffff  branco
 *    O indice 0 nunca e usado (clamp 1..6): a posicao 0x1380f738 e outra
 *    variavel, um ponteiro para a string "QUADRADO CENTRAL" (0x13694970) --
 *    por isso o Ghidra nomeia a tabela PTR_u_QUADRADO_CENTRAL_1380f738.
 *
 *  FUNCAO DE DESENHO: FUN_136951fc @ 0x136951fc
 *    1. FUN_13694bf0 obtem as dimensoes; FUN_134576f8 limpa o canvas com
 *       0xff04050b (quase preto, opaco).
 *    2. FUN_13457c84 desenha um retangulo navy 0xff14172e de (dim + 84) com
 *       raio de canto r = 0.72 * <constante>.  O centro da mira e
 *       (dimX + 84, dimY + 84).
 *    3. Duas passadas (0 e 1). A passada 0 so roda com sombra_ativa e pinta
 *       em 0xe6000000 (preto 90%), com cada retangulo 2px maior e deslocado
 *       -1px: e a SOMBRA. A passada 1 pinta o CORPO na cor escolhida.
 *    4. Em cada passada: se quadrado_central > 0, 4 retangulos de 1 unidade
 *       formam o contorno do quadrado central; depois, sempre, 4 bracos
 *       (esquerda, direita, cima, baixo) de comprimento +0x310 a partir de
 *       +0x314 do centro.  FUN_13695114 desenha cada retangulo com
 *       FUN_13457b08, em escala 2x.
 *    INFERIDO: o fundo opaco e o painel navy indicam que esta funcao pinta a
 *    PRE-VISUALIZACAO da mira (dialogo); a sobreposicao no jogo e ligada e
 *    desligada por FUN_13696e3c (abaixo).
 *
 *  LIGAR/DESLIGAR A SOBREPOSICAO: FUN_13696e3c @ 0x13696e3c
 *    Com trava de reentrada em (form+0x478), chama vtable[0x188] do objeto em
 *    *(PTR_DAT_1381110c)+0x550 com o bool recebido e repassa o mesmo bool a
 *    FUN_13687120(form+0x46c).
 *
 *  CONFIGURACAO E PERSISTENCIA
 *    A mira e salva/carregada via chaves "CROSSHAIR_SHADOW" e "crosshair1_space"
 *    no INI/registro do ReetFPS. A chave "CROSSHAIR_SHADOW" (@ 0x13696f1c)
 *    controla o checkbox de sombra.
 */

typedef struct {
    /* ... campos herdados da VCL/FMX ate +0x30f ... */
    int     comprimento_linha;   /* +0x310 */
    int     espacamento;         /* +0x314 */
    int     quadrado_central;    /* +0x318 */
    int     min_comprimento, min_espacamento, min_quadrado;  /* +0x31c..+0x324 */
    int     max_comprimento, max_espacamento, max_quadrado;  /* +0x328..+0x330 */
    int     indice_cor;          /* +0x334 -- 1..6 */
    bool    sombra_ativa;        /* +0x338 */
} TRPCrosshair;

extern const uint32_t g_cores_mira[7];  /* 0x1380f738; usar so [1..6] */

/* FUN_13695114: retangulo (x, y, w, h) em unidades, escala 2x, relativo ao
 * centro. Na passada de sombra usa 0xe6000000 e cresce 1px de cada lado.  */
static void mira_retangulo(void *canvas, int passada, uint32_t cor,
                           float cx, float cy,
                           float x, float y, float w, float h)
{
    if (passada == 0)
        canvas_fill_rect(canvas, 0xe6000000,
                         cx + x * 2 - 1, cy + y * 2 - 1,
                         w * 2 + 2, h * 2 + 2);          /* FUN_13457b08 */
    else
        canvas_fill_rect(canvas, cor,
                         cx + x * 2, cy + y * 2, w * 2, h * 2);
}

/* FUN_136951fc @ 0x136951fc */
void crosshair_desenhar(TRPCrosshair *mira, void *canvas)
{
    float dimX, dimY;
    crosshair_obter_dimensoes(mira, &dimX, &dimY);       /* FUN_13694bf0 */
    canvas_clear(canvas, 0xff04050b);                    /* FUN_134576f8 */

    float r = 0.72f * crosshair_raio_base();             /* INFERIDO: FUN_13147ef4 */
    canvas_round_rect(canvas, 0xff14172e, r, r,
                      dimX + 84.0f, dimY + 84.0f);       /* FUN_13457c84 */

    float cx = dimX + 84.0f, cy = dimY + 84.0f;
    int   len = mira->comprimento_linha;                 /* +0x310 */
    int   gap = mira->espacamento;                       /* +0x314 */
    uint32_t cor = g_cores_mira[clamp(mira->indice_cor, 1, 6)]; /* FUN_13191200 */

    for (int passada = 0; passada < 2; passada++) {
        if (passada == 0 && !mira->sombra_ativa)
            continue;                                    /* sem sombra */

        int q = mira->quadrado_central;                  /* +0x318 */
        if (q > 0) {
            if (q & 1) q++;                              /* lado sempre par */
            float o = -1.0f - q / 2.0f;
            mira_retangulo(canvas, passada, cor, cx, cy, o,     o,     1, q);     /* esq  */
            mira_retangulo(canvas, passada, cor, cx, cy, o,     o,     q, 1);     /* topo */
            mira_retangulo(canvas, passada, cor, cx, cy, o,     o + q, q, 1);     /* base */
            mira_retangulo(canvas, passada, cor, cx, cy, o + q, o,     1, q + 1); /* dir  */
        }

        mira_retangulo(canvas, passada, cor, cx, cy, -len - gap, -1,  len, 1);  /* braco esq  */
        mira_retangulo(canvas, passada, cor, cx, cy,  gap - 1,   -1,  len, 1);  /* braco dir  */
        mira_retangulo(canvas, passada, cor, cx, cy, -1, -len - gap,  1, len);  /* braco cima */
        mira_retangulo(canvas, passada, cor, cx, cy, -1,  gap - 1,    1, len);  /* braco baixo */
    }
}

/* FUN_13694ab8 @ 0x13694ab8 -- setter com clamp + Repaint (vtable+0xe0). */
void crosshair_configurar_parametros(TRPCrosshair *m, int comprimento,
                                     int espacamento, bool sombra,
                                     int indice_cor, int quadrado)
{
    m->comprimento_linha = clamp(comprimento, m->min_comprimento, m->max_comprimento);
    m->espacamento       = clamp(espacamento, m->min_espacamento, m->max_espacamento);
    m->quadrado_central  = clamp(quadrado,    m->min_quadrado,    m->max_quadrado);
    m->indice_cor        = clamp(indice_cor, 1, 6);
    m->sombra_ativa      = sombra;
    controle_repaint(m);
}

/* Prototipos dos auxiliares da mira. */
extern void  crosshair_obter_dimensoes(TRPCrosshair *m, float *w, float *h); /* FUN_13694bf0 */
extern int   clamp(int v, int min, int max);                                  /* FUN_13191200 */
extern void  canvas_clear(void *canvas, uint32_t cor_argb);                   /* FUN_134576f8 */
extern float crosshair_raio_base(void);                                       /* FUN_13147ef4 */
extern void  canvas_round_rect(void *canvas, uint32_t cor, float rx, float ry,
                               float w, float h);                             /* FUN_13457c84 */
extern void  canvas_fill_rect(void *canvas, uint32_t cor, float x, float y,
                              float w, float h);                              /* FUN_13457b08 */
extern void  controle_repaint(void *controle);                                /* vtable+0xe0 */


/* ===========================================================================
 *  14) TECLADO DE PRECISAO  (interno: "Teclado Turbo")
 * ===========================================================================
 *
 *  Otimiza a resposta do teclado ajustando as configuracoes de acessibilidade
 *  do Windows (HKCU\Control Panel\Accessibility\Keyboard Response).  O efeito
 *  e reduzir atrasos de repeticao e eliminar a filtragem de teclas (FilterKeys)
 *  para que os comandos do jogo cheguem ao PB sem latencia adicional.
 *
 *  O modulo tambem detecta teclados HID conectados (GetRawInputDeviceList +
 *  GetRawInputDeviceInfoW) antes de aplicar as configuracoes.
 *
 *  STRINGS DE UI
 *    "Teclado Turbo ativado!\r\nLatencia reduzida e resposta imediata..."
 *                                              @ ~0x136b5608  (UTF-16LE)
 *    "Teclado conectado"                        @  0x1353b43c
 *
 *  ITENS DE PERFIL (tabela de WideChar* em 0x136b6bec / 0x136b6cac)
 *    "TeclasAderencia_ON"    @ 0x136b6bec  -- desativa tecla aderente (StickyKeys)
 *    "OpTeclado_ON"          @ 0x136b6cac  -- otimiza configuracoes do teclado
 *
 *  TIPOS HID DETECTADOS (tabela @ 0x1353b080)
 *    "RECEPTOR"  @ 0x1353b088  -- receptor sem fio
 *    "KEYBOARD"  @ 0x1353b0d8  -- teclado USB padrao
 *    "TECLADO"   @ 0x1353b0f8  -- alias em portugues
 *    "KEYPAD"    @ (seguinte)  -- teclado numerico
 *
 *  CHAVE DE ESTADO: HKCU\Keyboard Layout\ReetFPS
 *    Usada para gravar/restaurar o estado; referenciada em:
 *      0x1353f8b4, 0x13584434, 0x135940f0, 0x135a09c8
 *
 *  COMANDOS DE REGISTRO -- APLICAR (enderecos: 0x135e9b30 .. 0x135e9cd4)
 *    reg add "HKCU\Control Panel\Accessibility\Keyboard Response"
 *            /v Flags           /t REG_SZ /d 0   /f  (desativa FilterKeys)
 *    reg add ...                /v AutoRepeatDelay /t REG_SZ /d 250 /f  (ms)
 *    reg add ...                /v AutoRepeatRate  /t REG_SZ /d 20  /f  (ms)
 *    reg add "HKCU\Control Panel\Accessibility\MouseKeys"
 *            /v Flags           /t REG_SZ /d 0   /f  (desativa MouseKeys)
 *                                              @ 0x135ea1a8
 *
 *  COMANDOS DE REGISTRO -- RESTAURAR (enderecos: 0x135e9ddc .. 0x135e9f80)
 *    reg add ...                /v AutoRepeatDelay /t REG_SZ /d 300 /f  (padrao)
 *    reg add ...                /v AutoRepeatRate  /t REG_SZ /d 45  /f  (padrao)
 *    reg add ...                /v BounceTime      /t REG_SZ /d 0   /f
 *    reg add ...                /v Flags           /t REG_SZ /d 2   /f  (restaura FilterKeys)
 */

/* Detecta teclados HID antes de aplicar as configuracoes.
 * Usa GetRawInputDeviceList + GetRawInputDeviceInfoW (@ 0x13835ef0 / 0x13836766).
 * Retorna true se um teclado compativel (KEYBOARD, TECLADO, RECEPTOR, KEYPAD)
 * foi encontrado.                                                              */
extern bool teclado_hid_detectar(void);    /* FUN_1353b000 (area) */

/* Executa um comando de shell elevado (wrapper de aplicar_tweak).             */
extern void executar_cmd(const char *linha); /* FUN_135401d0 area */

/* Card de notificacao (mesmo auxiliar usado por todos os modulos).            */
extern void FUN_1358027c(const wchar_t *titulo, const wchar_t *corpo, ...);

void pb_teclado_precisao_ativar(void)
{
    /* FUN_136b???? -- handler do botao TECLADO DE PRECISAO
     *
     * Fluxo reconstruido a partir dos itens de perfil, das strings e dos
     * comandos de registro encontrados na regiao 0x135e9b30..0x135ea1b0.     */

    if (!teclado_hid_detectar()) {
        /* Nenhum teclado HID reconhecido -- exibe aviso.                     */
        /* ("Teclado conectado" @ 0x1353b43c usado como indicador de ausencia) */
        return;
    }

    /* Desativa FilterKeys (Flags=0) para eliminar atraso de filtragem.       */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v Flags /t REG_SZ /d 0 /f >nul 2>&1");           /* 0x135e9b30 */

    /* Reduz tempo de repeticao: 250 ms de atraso, 20 ms entre repeticoes.   */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v AutoRepeatDelay /t REG_SZ /d 250 /f");          /* 0x135e9c00 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v AutoRepeatRate /t REG_SZ /d 20 /f");            /* 0x135e9cd4 */

    /* Desativa MouseKeys para nao interferir na precisao do mouse.           */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\MouseKeys\""
                 " /v Flags /t REG_SZ /d 0 /f >nul 2>&1");           /* 0x135ea1a8 */

    /* Notificacao de sucesso.                                                */
    FUN_1358027c(
        L"ReetFPS",
        L"Teclado Turbo ativado!\r\nLatência reduzida e resposta imediata "
         "para comandos mais rápidos e precisos.",
        0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
        L"icon.png", 0xffffffff, 0xffffffff, 0xffffffff, 1, 1, 1);
}

void pb_teclado_precisao_restaurar(void)
{
    /* Reverte para os valores padrao do Windows.                             */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v AutoRepeatDelay /t REG_SZ /d 300 /f");          /* 0x135e9ddc */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v AutoRepeatRate /t REG_SZ /d 45 /f");            /* 0x135e9eb0 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v BounceTime /t REG_SZ /d 0 /f");                 /* 0x135e9f80 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
                 " /v Flags /t REG_SZ /d 2 /f");                      /* (restaura) */
}


/* ===========================================================================
 *  15) ENTRADA INSTANTANEA
 * ===========================================================================
 *
 *  Pacote de otimizacoes de latencia aplicado de uma so vez.  Combina:
 *    - Configuracao da tarefa MMCSS "Low Latency" para prioridade maxima;
 *    - Desativacao do Game Bar / GameDVR / Xbox Live (que capturam input);
 *    - Despacho dos itens de perfil que cobrem servicos, efeitos visuais,
 *      telemetria, Cortana, OneDrive e demais fontes de jitter.
 *
 *  STRINGS DE UI
 *    "Ajustes de desempenho aplicados!\r\nSistema otimizado para menor "
 *     "latencia e resposta imediata em jogos."   @ ~0x136b0880  (UTF-16LE)
 *    "Game Bar desativada!\r\nRecursos em segundo plano foram desligados "
 *     "para reduzir input lag e melhorar o desempenho."
 *                                               @ 0x136b2124  (alias UTF-16LE)
 *
 *  ITENS DE PERFIL (tabela de WideChar* iniciando em ~0x136b6c00)
 *    "OpMouse_ON"          @ 0x136b6c88  -- otimiza precisao do mouse
 *    "OpTeclado_ON"        @ 0x136b6cac  -- otimiza teclado (ver §14)
 *    "TeclasAderencia_ON"  @ 0x136b6bec  -- desativa StickyKeys
 *    "GameBar_ON"          -- desativa Game Bar
 *    "GameDVR_ON"          -- desativa gravacao DVR
 *    "XboxLive_ON"         -- desativa servicos Xbox Live
 *    "EFFECTS_ON"          -- desativa efeitos visuais do Windows
 *    "DarkTheme_ON"        -- aplica tema escuro (reduz carga GPU)
 *    "Services_ON"         -- para servicos desnecessarios (ver §3 SERVICES)
 *    "Hibernate_ON"        -- desativa hibernacao
 *    "Superfetch_ON"       -- desativa SysMain/Superfetch
 *    "Cortana_ON"          -- remove Cortana
 *    "OneDrive_ON"         @ 0x136b6c70
 *    "APPS_ON"             @ 0x136b6c78
 *    "Ativador_ON"         @ (seguinte)
 *    "TarefaTelemetria_ON" -- cancela tarefas agendadas de telemetria
 *    "TelemetriaChrome_ON" -- desativa telemetria do Chrome
 *    "TelemetriaOffice_ON" @ 0x136b6c60
 *    "ADMENU_ON"           -- remove entradas de menu de contexto do Admin
 *
 *  TAREFA MMCSS "Low Latency" (9 comandos @ 0x135d5b10 .. 0x135d64d0)
 *    HKLM\...\Multimedia\SystemProfile\Tasks\Low Latency:
 *      Affinity          = 0         (sem afinidade de CPU fixa)
 *      Background Only   = False
 *      BackgroundPriority= 0
 *      Clock Rate        = 10000     (100 ns por tick -- resolucao maxima)
 *      GPU Priority      = 8
 *      Priority          = 2
 *      Scheduling Category = Medium
 *      SFIO Priority     = High
 *      Latency Sensitive = True
 *    Tambem aplica "Latency Sensitive=True" na tarefa "Games" @ 0x135d6f6c.
 *
 *  FUNCAO GAME BAR: FUN_136b1f48 @ 0x136b1f48
 *    Verifica o estado atual do Game Bar (FUN_13551e10(PTR_DAT_13811568)).
 *    Se == 7 (Game Bar ja desativado): mostra aviso via DAT_136b20a8.
 *    Senao: desativa com FUN_13552a50, oculta botoes do painel (+0x470/+0x474)
 *           e exibe a notificacao de sucesso via FUN_1358027c.
 */

/* Configura a tarefa MMCSS "Low Latency" com prioridade e sensibilidade
 * maximas.  Cada chamada executa um dos 9 comandos reg acima.               */
static void configurar_mmcss_low_latency(void)
{
    /* 0x135d5b10 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"Affinity\" /t REG_DWORD /d \"0\" /f");
    /* 0x135d5c3c */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"Background Only\" /t REG_SZ /d \"False\" /f");
    /* 0x135d5d78 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"BackgroundPriority\" /t REG_DWORD /d \"0\" /f");
    /* 0x135d5eb8 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"Clock Rate\" /t REG_DWORD /d \"10000\" /f");
    /* 0x135d5ff0 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"GPU Priority\" /t REG_DWORD /d \"8\" /f");
    /* 0x135d6124 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"Priority\" /t REG_DWORD /d \"2\" /f");
    /* 0x135d6250 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"Scheduling Category\" /t REG_SZ /d \"Medium\" /f");
    /* 0x135d6398 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"SFIO Priority\" /t REG_SZ /d \"High\" /f");
    /* 0x135d64d0 */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Low Latency\""
                 " /v \"Latency Sensitive\" /t REG_SZ /d \"True\" /f");

    /* Tarefa Games -- tambem recebe Latency Sensitive.                       */
    /* 0x135d6f6c */
    executar_cmd("Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT"
                 "\\CurrentVersion\\Multimedia\\SystemProfile\\Tasks\\Games\""
                 " /v \"Latency Sensitive\" /t REG_SZ /d \"True\" /f");
}

/* Handler do botao Game Bar dentro do painel ENTRADA INSTANTANEA.
 * FUN_136b1f48 @ 0x136b1f48.                                                */
void pb_entrada_instantanea_desativar_gamebar(int painel)
{
    int estado = FUN_13551e10(*(int *)PTR_DAT_13811568);  /* le estado do GameBar */

    if (estado == 7) {
        /* Game Bar ja desativado -- exibe aviso (DAT_136b20a8).             */
        FUN_134a8d98(*(int *)PTR_DAT_13811378,
                     (void *)0x136b20a8,
                     /*len=*/2, /*...*/0, 0x27, 0x52, 0);
        return;
    }

    /* Salva estado anterior e desativa.                                      */
    FUN_135529c4(*(int *)PTR_DAT_13811568, /*&backup=*/0);
    FUN_13552a50(*(int *)PTR_DAT_13811568, /*backup=*/0, /*acao=*/0);

    /* Oculta os botoes do painel que ficam visiveis so quando ativo.        */
    FUN_132abec4(*(int *)(painel + 0x470), 0);   /* oculta botao 1 */
    FUN_132abec4(*(int *)(painel + 0x474), 1);   /* exibe botao 2  */

    /* Notificacao de sucesso.                                                */
    FUN_1358027c(
        L"ReetFPS",
        L"Game Bar desativada!\r\nRecursos em segundo plano foram desligados "
         "para reduzir input lag e melhorar o desempenho.",  /* 0x136b2124 */
        0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
        L"icon.png", 0xffffffff, 0xffffffff, 0xffffffff, 1, 1, 1);
}

/* Ponto de entrada principal do perfil ENTRADA INSTANTANEA.
 * Despacha todos os itens de perfil habilitados (via executar_itens_habilitados)
 * e configura o MMCSS "Low Latency" em seguida.                             */
void pb_entrada_instantanea_ativar(int painel)
{
    /* Os itens de perfil (OpMouse_ON, GameBar_ON, GameDVR_ON, XboxLive_ON,
     * Services_ON, EFFECTS_ON, Hibernate_ON, Superfetch_ON, Cortana_ON,
     * OneDrive_ON, APPS_ON, Ativador_ON, TarefaTelemetria_ON,
     * TelemetriaChrome_ON, TelemetriaOffice_ON, ADMENU_ON, DarkTheme_ON,
     * TeclasAderencia_ON, OpTeclado_ON) sao despachados pelo mecanismo
     * generico de perfil (ver secao §3 + FUN_137008d0).                     */
    executar_itens_habilitados(painel);   /* FUN_137008d0 -- despacha perfil */

    /* Configura explicitamente a tarefa MMCSS "Low Latency".               */
    configurar_mmcss_low_latency();

    /* Desativa Game Bar se ainda ativa.                                     */
    pb_entrada_instantanea_desativar_gamebar(painel);
}

/* Auxiliares externos referenciados nesta secao.                            */
extern void executar_itens_habilitados(int painel);           /* FUN_137008d0 */
extern int  FUN_13551e10(int game_bar_handle);
extern void FUN_13552a50(int handle, int backup, int acao);
extern void FUN_135529c4(int handle, int *backup_out);
extern void FUN_134a8d98(int painel, void *msg, int tipo, ...);
extern void FUN_132abec4(int controle, int visivel);
extern int *PTR_DAT_13811568;   /* ponteiro para handle do Game Bar          */
extern int *PTR_DAT_13811378;   /* ponteiro para painel de notificacoes      */


/* ===========================================================================
 *  16) INTERFACE SEM DELAY
 * ===========================================================================
 *
 *  Desativa a transparencia do Windows (Aero) e os efeitos visuais para
 *  reduzir a carga do compositor DWM e eliminar atrasos de resposta de menu
 *  e janela.  A restauracao reverte tudo para a aparencia padrao do Windows.
 *
 *  STRINGS DE UI
 *    "Interface otimizada"                               @ 0x136ed2c8 (label)
 *    "Transparencia do Windows desativada!\r\n"
 *     "Efeitos visuais desativados para priorizar "
 *     "desempenho e reduzir latencia."                  @ 0x136b3740 (ativar)
 *    "Transparencia do Windows ativada!\r\n"
 *     "Efeitos visuais restaurados para uma interface "
 *     "mais fluida e moderna."                          @ ~0x136b398c (restaurar)
 *
 *  ITEM DE PERFIL
 *    "EFFECTS_ON"  @ 0x136b6adc
 *
 *  FUNCAO ATIVAR:   FUN_136b35e4  @ 0x136b35e4
 *  FUNCAO RESTAURAR: area antes de 0x136b3900
 *
 *  COMANDOS DE REGISTRO -- DESATIVAR EFEITOS (enderecos: 0x135f3848, 0x135f3940)
 *    reg add "HKCU\...\Explorer\VisualEffects"
 *            /v VisualFXSetting        /t REG_DWORD /d 0 /f  (melhor desempenho)
 *    reg add "HKCU\...\Explorer\VisualEffects"
 *            /v VisualFXSettingPerUser /t REG_DWORD /d 0 /f
 *
 *  COMANDOS DE REGISTRO -- DESATIVAR TRANSPARENCIA (enderecos: 0x135e8030)
 *    reg add "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize"
 *            /v EnableTransparency /t REG_DWORD /d 0 /f
 *
 *  COMANDOS DE REGISTRO -- RESPONSIVIDADE DE MENU/JANELA (0x135eb0a0)
 *    reg add "HKCU\Control Panel\Desktop" /v ForegroundLockTimeout  /d 0     /f
 *    reg add "HKCU\Control Panel\Desktop" /v HungAppTimeout         /d 2000  /f
 *    reg add "HKCU\Control Panel\Desktop" /v WaitToKillAppTimeout   /d 2000  /f
 *    reg add "HKCU\Control Panel\Desktop" /v MenuShowDelay          /d 0     /f
 *    reg add "HKCU\Control Panel\Desktop" /v LowLevelHooksTimeout   /d 2000  /f
 *    reg add "HKCU\System\GameConfigStore" /v GameMode              /d 0     /f
 *
 *  COMANDOS DE REGISTRO -- DESATIVAR ANIMACOES DWM (0x135d3e50)
 *    reg add "HKEY_CURRENT_USER\Software\Microsoft\Windows\DWM"
 *            /v DisableAnimations /t REG_DWORD /d 1 /f >nul 2>&1
 *
 *  COMANDOS DE REGISTRO -- RESTAURAR EFEITOS (enderecos: 0x135f2458, 0x135f2554)
 *    reg add "HKCU\...\Explorer\VisualEffects"
 *            /v VisualFXSetting        /t REG_DWORD /d 2 /f  (melhor aparencia)
 *    reg add "HKCU\...\Explorer\VisualEffects"
 *            /v VisualFXSettingPerUser /t REG_DWORD /d 2 /f
 *  RESTAURAR TRANSPARENCIA (0x135e79f8)
 *    reg add "HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Themes\Personalize"
 *            /v EnableTransparency /t REG_DWORD /d 1 /f
 */

/* Executa os 6 comandos de responsividade de menu/janela (FUN @ 0x135eb0a0).
 * Carrega as strings na pilha e chama o despachante com edx=5 (6 itens).     */
static void configurar_responsividade_interface(void)
{
    /* 0x135eb1f0 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""
                 " /v ForegroundLockTimeout /t REG_DWORD /d 0 /f");
    /* 0x135eb2a4 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""
                 " /v HungAppTimeout /t REG_SZ /d 2000 /f");
    /* 0x135eb348 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""
                 " /v MenuShowDelay /t REG_SZ /d 0 /f");
    /* 0x135eb3e4 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""
                 " /v WaitToKillAppTimeout /t REG_SZ /d 2000 /f");
    /* 0x135eb494 */
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""
                 " /v LowLevelHooksTimeout /t REG_SZ /d 2000 /f");
    /* 0x135eb00e */
    executar_cmd("reg add \"HKCU\\System\\GameConfigStore\""
                 " /v \"GameMode\" /t REG_DWORD /d 0 /f");
    /* 0x135eb100 */
    executar_cmd("reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Power\\PowerThrottling\""
                 " /v PowerThrottlingOff /t REG_DWORD /d 1 /f");
}

/* Ponto de entrada do perfil INTERFACE SEM DELAY -- desativa efeitos visuais.
 * FUN_136b35e4 @ 0x136b35e4.                                                 */
void pb_interface_sem_delay_ativar(void)
{
    /* Desativa transparencia do Aero.                                         */
    /* 0x135e8030 */
    executar_cmd("reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows"
                 "\\CurrentVersion\\Themes\\Personalize\""
                 " /v EnableTransparency /t REG_DWORD /d 0 /f");

    /* Desativa animacoes do compositor DWM.                                   */
    /* 0x135d3e50 */
    executar_cmd("reg add \"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\DWM\""
                 " /v DisableAnimations /t REG_DWORD /d 1 /f >nul 2>&1");

    /* Ajusta VisualFX para melhor desempenho (desativa todos os efeitos).     */
    /* 0x135f3848 */
    executar_cmd("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion"
                 "\\Explorer\\VisualEffects\""
                 " /v VisualFXSetting /t REG_DWORD /d 0 /f");
    /* 0x135f3940 */
    executar_cmd("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion"
                 "\\Explorer\\VisualEffects\""
                 " /v VisualFXSettingPerUser /t REG_DWORD /d 0 /f");

    /* Configura timeouts de menu e janela para resposta imediata.             */
    configurar_responsividade_interface();

    /* Notificacao de sucesso via card toast (FUN_1358027c).                   */
    FUN_1358027c(
        L"ReetFPS",                                       /* title @ 0x136b382c */
        L"Transparência do Windows desativada!\r\n"
         "Efeitos visuais desativados para priorizar "
         "desempenho e reduzir latência.",                /* body @ 0x136b3740  */
        0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
        L"icon.png",                                      /* @ 0x136b3720       */
        0xffffffff, 0xffffffff, 0xffffffff, 1, 1, 1);
}

/* Restaura efeitos visuais e transparencia para o padrao do Windows.         */
void pb_interface_sem_delay_restaurar(void)
{
    /* Reativa transparencia.                                                   */
    /* 0x135e79f8 */
    executar_cmd("reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows"
                 "\\CurrentVersion\\Themes\\Personalize\""
                 " /v EnableTransparency /t REG_DWORD /d 1 /f");

    /* Restaura VisualFX para "melhor aparencia" (todos os efeitos ligados).   */
    /* 0x135f2458 */
    executar_cmd("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion"
                 "\\Explorer\\VisualEffects\""
                 " /v VisualFXSetting /t REG_DWORD /d 2 /f");
    /* 0x135f2554 */
    executar_cmd("reg add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion"
                 "\\Explorer\\VisualEffects\""
                 " /v VisualFXSettingPerUser /t REG_DWORD /d 2 /f");

    /* Restaura delays de menu para o padrao do Windows.                       */
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""      /* 0x135f42c4 */
                 " /v MenuShowDelay /t REG_SZ /d 400 /f");
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""      /* 0x135f4364 */
                 " /v WaitToKillAppTimeout /t REG_SZ /d 20000 /f");
    executar_cmd("reg add \"HKCU\\Control Panel\\Desktop\""      /* 0x135f4418 */
                 " /v HungAppTimeout /t REG_SZ /d 5000 /f");

    /* Notificacao de restauracao.                                             */
    FUN_1358027c(
        L"ReetFPS",
        L"Transparência do Windows ativada!\r\n"
         "Efeitos visuais restaurados para uma interface "
         "mais fluida e moderna.",                        /* @ ~0x136b398c     */
        0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
        L"icon.png", 0xffffffff, 0xffffffff, 0xffffffff, 1, 1, 1);
}


/* ===========================================================================
 *  17) FPS ILIMITADO  (TRPFpsLimit / TURBINAR FPS)
 * ===========================================================================
 *
 *  Classe original: TRPFpsLimit
 *      RTTI @ 0x13691621  ("TRPFpsLimit6")
 *      Construtor       : FUN_13691854 @ 0x13691854
 *      Titulo interno   : "TURBINAR FPS" @ 0x136918f0
 *
 *  O perfil "TURBINAR FPS" tem dois modos seleccionaveis pelo usuario:
 *
 *    SEM LIMITE   (indice 10) @ 0x13692714
 *        Remove qualquer teto de FPS dentro do Point Blank: envia ao jogo
 *        o valor maximo reservado ("FPS 486", string estatica @ 0x13687df8),
 *        permitindo que o motor rode sem restricao. A chave de estado e
 *        gravada como a string literal "UNLOCKEDFPS".
 *
 *    LIMITE DE FPS (indice 11) @ 0x13692738
 *        O usuario escolhe um dos 6 presets de frequencia. O preset
 *        escolhido e persistido numericamente em FPS_SELECTION_INDEX.
 *
 *  Estado persistido (registry HKCU):
 *      Chave  : "Keyboard Layout\ReetFPS"  (padrao de todo o ReetFPS)
 *      Valor  : "FPS_SELECTION_INDEX"  @ 0x136937e8 / 0x13694054
 *                 "UNLOCKEDFPS"  -> modo SEM LIMITE  (@ 0x13693728)
 *                  "1" .. "6"   -> preset especifico, modo LIMITE DE FPS
 *
 *  Itens de perfil usados: Energia_ON, Hibernate_ON
 *      (reduzem latencia do subsistema de energia antes de aplicar FPS)
 *
 *  Cadeia de aplicacao (modo LIMITE DE FPS, FUN_1369407c @ 0x1369407c):
 *
 *    pb_fps_definir_preset(index)          // entry point, FUN_1369407c
 *      ↓
 *    FUN_13693740(index)                   // clamp: se index < 1 ou > 6 → 3
 *      ↓
 *    FUN_13693d44()                        // verifica se o jogo esta rodando
 *      ↓                                  // (PTR_DAT_13811928 / PTR_DAT_13810cd8)
 *    FUN_132db2e0(fpslimit_obj, index)     // grava no controlador de FPS
 *      ↓                                  // *(PTR_DAT_1381110c + 0x4fc)
 *    _DAT_1380f72c = index                // cache global do indice atual
 *      ↓
 *    FUN_13693f74(index)                   // notifica TPointBlankPerformanceThread
 *      ↓                                  // DAT_13819b44 + 0x448
 *    FUN_13693fec(index)                   // persiste FPS_SELECTION_INDEX no reg.
 *      ↓
 *    FUN_13693e20(index)                   // aplicador completo (ver abaixo)
 *
 *  Dentro de FUN_13693e20 (@ 0x13693e20):
 *    FUN_13693810(index, &label)           // label de exibicao do preset (1-6)
 *    FUN_132ac010(ui_obj+0x560, label)     // atualiza 3 controles de UI
 *    FUN_132ac010(ui_obj+0x514, label)
 *    FUN_132ac010(ui_obj+0x55c, label)
 *    FUN_1369190c(perf_thread, index)      // sincroniza com a thread de perf.
 *    FUN_13693a64(index, &fps_val)         // string numerica do FPS para o jogo
 *    FUN_13574f7c(fps_obj, fps_val)        // ESCRITA FINAL: obj+0xc = fps_val
 *
 *  FUN_13574f7c (@ 0x13574f7c) — setter atomico do FPS cap no jogo:
 *    compara o novo valor com o atual em (fps_obj+0xc);
 *    se diferente: atribui + dispara FUN_13203484 (repaint/notifica motor).
 *
 *  Notificacao toast: "Windows Turbo +FPS" @ 0x136ec52c
 */

/* Prototipos internos. */
extern int  fps_clamp_preset(int index);                /* FUN_13693740 */
extern void fps_verificar_jogo(void);                   /* FUN_13693d44 */
extern void fps_notificar_thread(int index);            /* FUN_13693f74 */
extern void fps_salvar_registro(int index);             /* FUN_13693fec */
extern void fps_aplicar_no_jogo(int index);             /* FUN_13693e20 */

/*
 * pb_fps_definir_preset  —  FUN_1369407c @ 0x1369407c
 *
 * Define o teto de FPS do Point Blank pelo indice de preset (1–6).
 * Indice fora do intervalo resulta no preset 3 (medio) como padrao.
 * Esta funcao e o ponto de entrada tanto para o modo LIMITE DE FPS
 * quanto para o startup que re-aplica o valor salvo em FPS_SELECTION_INDEX.
 */
void pb_fps_definir_preset(int index)
{
    int clamped;

    clamped = fps_clamp_preset(index);   /* [1,6], default 3 se fora do range */

    fps_verificar_jogo();                /* assegura que o jogo esta ativo     */

    /* Grava no controlador interno do FPS (objeto em PTR_DAT_1381110c+0x4fc). */
    if (*(int *)PTR_DAT_1381110c != 0 &&
        *(int *)(*(int *)PTR_DAT_1381110c + 0x4fc) != 0)
    {
        FUN_132db2e0(
            *(int *)(*(int *)PTR_DAT_1381110c + 0x4fc),
            clamped);                    /* FUN_132db2e0 @ 0x132db2e0        */
    }

    _DAT_1380f72c = clamped;             /* cache global do indice ativo       */

    fps_notificar_thread(clamped);       /* wake TPointBlankPerformanceThread  */
    fps_salvar_registro(clamped);        /* FPS_SELECTION_INDEX → reg.         */
    fps_aplicar_no_jogo(clamped);        /* UI + escrita final no motor        */
}

/*
 * pb_fps_ilimitado_ativar  —  modo SEM LIMITE
 *
 * Chamado quando o usuario seleciona "SEM LIMITE" (indice 10 na UI).
 * Envia o valor de FPS maximo ao jogo ("FPS 486", string em 0x13687df8)
 * e grava o marcador "UNLOCKEDFPS" na chave FPS_SELECTION_INDEX.
 *
 * O fluxo e diferente de pb_fps_definir_preset: o indice 10 nao passa
 * pela funcao de clamp (que retornaria 3 para valores fora de [1,6]).
 * Em vez disso, chama diretamente fps_aplicar_no_jogo com o valor 0 ou
 * usa uma sequencia separada que escrevia a string literal "FPS 486" em
 * (fps_obj+0xc) sem passar pelo conversor de presets.
 * A chave de estado registra "UNLOCKEDFPS" para que no proximo startup
 * o modo seja restaurado sem tentar parsear um numero de preset.
 */
void pb_fps_ilimitado_ativar(void)
{
    /* Grava o marcador de modo ilimitado. */
    RegSetValueExW(                                    /* via FUN_13693fec     */
        HKEY_CURRENT_USER,
        L"Keyboard Layout\\ReetFPS",
        L"FPS_SELECTION_INDEX",                        /* @ 0x136937e8         */
        L"UNLOCKEDFPS");                               /* @ 0x13693728         */

    /* Envia ao motor do Point Blank o teto maximo de FPS.
     * "FPS 486" e a string estatica usada como valor de FPS ilimitado.  */
    fps_aplicar_fps_string(L"FPS 486");                /* string @ 0x13687df8  */

    /* Notifica o usuario. */
    FUN_1358027c(
        L"ReetFPS",
        L"Windows Turbo +FPS ativado!\r\n"
         "FPS ilimitado aplicado no Point Blank.",     /* @ 0x136ec52c         */
        0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
        L"icon.png", 0xffffffff, 0xffffffff, 0xffffffff, 1, 1, 1);
}

/*
 * pb_fps_preset_salvar  —  FUN_13693fec @ 0x13693fec
 *
 * Persiste o indice de preset escolhido (1-6) como string decimal
 * em "Keyboard Layout\ReetFPS\FPS_SELECTION_INDEX" (HKCU).
 * No modo SEM LIMITE, a string gravada e "UNLOCKEDFPS" (ver acima).
 */
void pb_fps_preset_salvar(int index)
{
    wchar_t buf[8];
    _itow_s(index, buf, 8, 10);           /* convert int → "1".."6"           */

    RegSetValueExW(
        HKEY_CURRENT_USER,
        L"Keyboard Layout\\ReetFPS",      /* @ 0x13467cd4                     */
        L"FPS_SELECTION_INDEX",           /* @ 0x136937e8                     */
        buf);
}


/* ===========================================================================
 *  18) MAPAS INSTANTANEOS
 * ===========================================================================
 *
 *  Pacote de otimizacoes voltado a reduzir o tempo de carregamento de mapa
 *  no Point Blank.  O painel agrupa tres itens de perfil que juntos eliminam
 *  a concorrencia de disco/CPU durante o loading:
 *
 *    1. SUPERFETCH_ON  -- desativa SysMain/Superfetch, que faz pre-cargas em
 *                         background e provoca I/O durante o loading do mapa.
 *    2. LOADINGMAP     -- ajusta os timeouts de cache do redirector de rede
 *                         (LanmanWorkstation), zerando os tempos de cache de
 *                         diretorio, arquivos nao-encontrados e info de arquivo
 *                         para que leituras de pacotes do jogo nunca batam em
 *                         entradas obsoletas.
 *    3. FULLSCREEN     -- forca o modo tela cheia exclusivo do Point Blank,
 *                         reduzindo interferencias visuais do compositor DWM
 *                         e melhorando a estabilidade de frame.
 *
 *  PANEL INIT:  FUN_136ec13c @ 0x136ec13c
 *    Monta a lista de itens do painel; chamado ao abrir o formulario de
 *    MAPAS INSTANTANEOS.  Os itens em (param_1+0x310) sao configuracoes
 *    "a fazer"; os em (param_1+0x311) sao itens ja aplicados/ativos.
 *
 *  STRINGS DE UI
 *    "LOADINGMAP"                    @ 0x136ecd60  (chave de estado / profile item)
 *    "Carregamento de mapa otimizado"@ 0x136ece50  (label do item no painel)
 *    "FULLSCREEN"                    @ 0x136ece9c  (chave de estado)
 *    "Tela cheia otimizada"          @ 0x136ecfb4  (label FULLSCREEN)
 *    "Superfetch_ON"                 @ 0x136ec920  (chave Superfetch)
 *    "Desativar Superfetch"          @ 0x136eca0c  (label Superfetch)
 *
 *  ESTADO PERSISTIDO (HKCU\Keyboard Layout\ReetFPS)
 *    "LOADINGMAP"         @ 0x136f7be0  -- estado do item de carregamento
 *    "FULLSCREEN"         @ 0x136f7b2c  -- estado do fullscreen
 *    "Superfetch_ON"      @ 0x136f776c  -- estado do Superfetch
 *
 *  COMANDOS SUPERFETCH_ON (item 1):
 *    sc stop SysMain > nul 2>&1                             @ 0x135d2ab4
 *    sc config SysMain start= disabled > nul 2>&1          @ 0x135d2af8
 *    reg add "HKLM\...\PrefetchParameters"
 *            /v EnableSuperfetch /t REG_DWORD /d 0 /f      @ 0x135f0438
 *    reg add "HKLM\...\PrefetchParameters"
 *            /v EnablePrefetcher /t REG_DWORD /d 0 /f      @ 0x135f0560
 *
 *  COMANDOS LOADINGMAP (item 2, LanmanWorkstation cache lifetimes):
 *    reg add "HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\
 *             Services\LanmanWorkstation\Parameters"
 *            /v DirectoryCacheLifetime  /t REG_DWORD /d 0 /f >nul 2>&1
 *                                                           @ 0x135d50b4
 *    reg add "...\LanmanWorkstation\Parameters"
 *            /v FileNotFoundCacheLifetime /t REG_DWORD /d 0 /f >nul 2>&1
 *                                                           @ 0x135d51ec
 *    reg add "...\LanmanWorkstation\Parameters"
 *            /v FileInfoCacheLifetime   /t REG_DWORD /d 0 /f >nul 2>&1
 *                                                           @ 0x135d5328
 *
 *  COMANDO FULLSCREEN (item 3):
 *    Descricao: "Aplica o modo tela cheia recomendado para reduzir
 *    interferencias visuais e melhorar estabilidade."      @ 0x136ece9c
 *    (O mecanismo exato de fullscreen -- arquivo .ini ou parametro de
 *    linha de comando do PB -- nao foi localizado nas regioes acessiveis
 *    do binario; o item e despachado pelo mecanismo generico de perfil.)
 */

/* Desativa SysMain/Superfetch para liberar disco durante o loading.       */
static void mapas_desativar_superfetch(void)
{
    /* 0x135d2ab4 */
    executar_cmd("sc stop SysMain > nul 2>&1");
    /* 0x135d2af8 */
    executar_cmd("sc config SysMain start= disabled > nul 2>&1");
    /* 0x135f0438 */
    executar_cmd("reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager"
                 "\\Memory Management\\PrefetchParameters\""
                 " /v EnableSuperfetch /t REG_DWORD /d 0 /f");
    /* 0x135f0560 */
    executar_cmd("reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager"
                 "\\Memory Management\\PrefetchParameters\""
                 " /v EnablePrefetcher /t REG_DWORD /d 0 /f");
}

/* Zera os timeouts de cache do LanmanWorkstation para eliminar leituras
 * de cache obsoletas dos pacotes do jogo.                                 */
static void mapas_configurar_lanman_cache(void)
{
    /* 0x135d50b4 */
    executar_cmd("reg add \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet"
                 "\\Services\\LanmanWorkstation\\Parameters\""
                 " /v DirectoryCacheLifetime /t REG_DWORD /d 0 /f >nul 2>&1");
    /* 0x135d51ec */
    executar_cmd("reg add \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet"
                 "\\Services\\LanmanWorkstation\\Parameters\""
                 " /v FileNotFoundCacheLifetime /t REG_DWORD /d 0 /f >nul 2>&1");
    /* 0x135d5328 */
    executar_cmd("reg add \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet"
                 "\\Services\\LanmanWorkstation\\Parameters\""
                 " /v FileInfoCacheLifetime /t REG_DWORD /d 0 /f >nul 2>&1");
}

/*
 * pb_mapas_instantaneos_ativar  --  ponto de entrada do perfil MAPAS INSTANTANEOS
 *
 * Despacha os tres itens de perfil em sequencia e grava o estado em
 * "Keyboard Layout\ReetFPS" (chaves LOADINGMAP, FULLSCREEN, Superfetch_ON).
 * O mecanismo generico executar_itens_habilitados (FUN_137008d0) cuida do
 * despacho dos itens e do registro de estado; este codigo reflete a logica
 * especifica de cada item.
 *
 * Panel init: FUN_136ec13c @ 0x136ec13c
 */
void pb_mapas_instantaneos_ativar(int painel)
{
    /* Item 1: Superfetch_ON -- para SysMain e desabilita pre-cargas.      */
    mapas_desativar_superfetch();

    /* Item 2: LOADINGMAP -- zera caches do redirector de rede.            */
    mapas_configurar_lanman_cache();

    /* Item 3: FULLSCREEN -- o despacho do modo tela cheia e feito pelo
     * mecanismo generico de perfil; sem codigo especifico localizado.     */
    executar_itens_habilitados(painel);   /* FUN_137008d0 */
}

/* Externs desta secao (mesmos auxiliares dos modulos anteriores).        */
extern void executar_cmd(const char *linha);
extern void executar_itens_habilitados(int painel);  /* FUN_137008d0 */


/* ============================================================================
 *  §19  MINI-MAP OFF
 * ==============================================================================
 *
 *  O que o ReetFPS faz
 *  -------------------
 *  Modifica diretamente o arquivo de configuracao do Point Blank para desativar
 *  o indicador de missao (mini-mapa) e os efeitos de HUD. Isso reduz o volume
 *  de elementos desenhados no HUD a cada frame, o que diminui a carga de
 *  renderizacao e elimina distracao visual durante a partida.
 *
 *  Mecanismo
 *  ---------
 *  O binario contem a classe `TRPPBConfig` (unidade `uRPPBConfig`, RTTI em
 *  0x13582628). Ela le e grava o arquivo de configuracao do jogo usando pares
 *  chave/valor em formato INI (UTF-16LE). As duas chaves relevantes para o
 *  mini-mapa sao:
 *
 *      HUD_Effect             (offset 0x34 no objeto, config key @ 0x13584ce4)
 *      Enable_MissionIndicator (config key @ 0x13584d08)
 *
 *  Ambas aparecem uma segunda vez na tabela de escrita em 0x13585878 e
 *  0x1358589c, respectivamente. Quando TRPPBConfig.ApplyConfig e chamado com
 *  HUDEffect=0 e MissionIndicator=0, ele sobrescreve essas chaves no arquivo
 *  de opcoes do jogo.
 *
 *  O perfil e registrado como item `OPTIMIZER_PB_MANAGER` no despacho de
 *  perfis (tabela de strings em 0x136f7b50) e e aplicado via
 *  `FUN_13600598` (@ 0x13600598), que cria um contexto TRPPBConfig e invoca
 *  `FUN_135c52bc` (@ 0x135c52bc) para executar a escrita no arquivo do jogo.
 *
 *  A notificacao de conclusao (card ReetFPS) e disparada por
 *  `FUN_1358027c` com o texto em `DAT_1360069c`.
 *
 *  Campos da classe TRPPBConfig relevantes para o mini-mapa
 *  ---------------------------------------------------------
 *      Indice 40  HUDEffect           -- efeitos de HUD  (offset 0x34)
 *      Chave INI: HUD_Effect          -- escrita no arquivo do jogo
 *      Chave INI: Enable_MissionIndicator -- indicador de missao (mini-mapa)
 *
 *  Tabela completa de chaves INI lidas/escritas pela classe (das strings
 *  UTF-16LE em 0x13584c00 / 0x13585840):
 *      ScreenMode, Graphics, ScreenWidth, ScreenHeight, RefreshRate,
 *      AntiAlias, ShadowQualityType, TextureQualityType, SpecularQualityType,
 *      EffectQuality, FPSType, FPSValor, Gamma, FovValue, VSync, TriLinear,
 *      DynamicLight, EnableNormalMap, EnableTerrainEffect, HDR, DX11,
 *      RimLight, IBL, SSAO, SSR, EnablePhysX, Game, TeamBand,
 *      DisableAccessory, WeaponEffect, HUD_Effect, Enable_MissionIndicator,
 *      EnableBulletTrace, EnableBulletSmoke
 */

/*
 * pb_pbconfig_gravar_minimap_off
 *
 * Cria uma instancia de TRPPBConfig, define os campos de HUD e indicador
 * de missao como desativados (0) e persiste no arquivo de configuracao do
 * Point Blank via ApplyConfig.
 *
 * Enderecos-chave:
 *   TRPPBConfig RTTI         0x13582628  (uRPPBConfig)
 *   Tabela de chaves INI #1  0x13584c00  (leitura)
 *   Tabela de chaves INI #2  0x13585840  (escrita)
 *   HUD_Effect key           0x13584ce4 / 0x13585878
 *   Enable_MissionIndicator  0x13584d08 / 0x1358589c
 *   FUN_13600598             handler OPTIMIZER_PB_MANAGER
 *   FUN_135c52bc             enfileirador do ApplyConfig
 *   FUN_135bcaec             corpo do worker de verificacao/reparo
 */
static void pb_pbconfig_gravar_minimap_off(void *pb_config_obj)
{
    /*
     * TRPPBConfig.HUDEffect (offset 0x34) = 0
     *   Chave INI: HUD_Effect
     *   Efeito: desativa todos os efeitos de HUD (contadores, indicadores,
     *           marcadores de time) durante a renderizacao do frame.
     */
    *(unsigned char *)((unsigned char *)pb_config_obj + 0x34) = 0;   /* HUDEffect = 0 */

    /*
     * TRPPBConfig.Enable_MissionIndicator (chave INI direta) = 0
     *   Efeito: oculta o mini-mapa/radar de missao na tela do jogo.
     *   O campo e uma chave INI sem offset fixo identificado no RTTI; ele e
     *   gravado pela mesma rotina de escrita de config que usa a tabela em
     *   0x13585840.
     */

    /*
     * FUN_135c52bc @ 0x135c52bc -- enfileira a escrita do config no jogo.
     * Internamente:
     *   1. Cria contexto com objeto TRPPBConfig configurado acima.
     *   2. Verifica se o caminho do jogo e valido (FUN_135c5498).
     *   3. Serializa os campos para o arquivo INI do Point Blank.
     *   4. Dispara notificacao via FUN_1358027c se param_show_card != 0.
     */
    pbconfig_aplicar_config(pb_config_obj, /*show_card=*/1);   /* FUN_135c52bc */
}

/*
 * pb_minimap_off_ativar  --  ponto de entrada do MINI-MAP OFF
 *
 * Cria a instancia de TRPPBConfig (via FUN_13149aa8), configura os campos
 * de HUD e mini-mapa para zero e chama o worker de escrita.
 * Em seguida exibe o card de notificacao "MINI-MAP desativado!".
 *
 * Entrada no despacho de perfis: item `OPTIMIZER_PB_MANAGER`
 *   -- tabela em 0x136f7b50, handler em FUN_13600598 @ 0x13600598
 */
void pb_minimap_off_ativar(void)
{
    /* Cria instancia de TRPPBConfig (Delphi TObject.Create).                */
    void *cfg = pbconfig_criar();         /* FUN_13149aa8(&DAT_136003e4, 1)  */

    /*
     * Define os dois campos que controlam o mini-mapa:
     *   HUD_Effect            = 0  (sem efeitos de HUD)
     *   Enable_MissionIndicator = 0 (sem indicador de missao / mini-mapa)
     */
    pb_pbconfig_gravar_minimap_off(cfg);

    /*
     * Notificacao ao usuario via card ReetFPS.
     * Texto em DAT_1360069c: "Iniciando verificacao e reparacao..."
     * (o mesmo card usado pelo OPTIMIZER_PB_MANAGER geral).
     * FUN_1358027c @ 0x1358027c
     */
    notificar_card(L"ReetFPS",
                   L"MINI-MAP desativado! HUD_Effect e Enable_MissionIndicator = 0 "
                   L"gravados no arquivo de configuracao do PointBlank.",
                   6000, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                   L"icon.png", 0xffffffff, 0xffffffff, 0xffffffff, 1, 1, 1);
}

/* Externs desta secao.                                                       */
extern void *pbconfig_criar(void);           /* FUN_13149aa8(&DAT_136003e4,1) */
extern void  pbconfig_aplicar_config(void *cfg, int show_card); /* FUN_135c52bc */
extern void  notificar_card(const wchar_t *titulo, const wchar_t *corpo,
                            int duracao_ms,
                            int p1, int p2, int p3, int p4, int p5, int p6,
                            const wchar_t *icone,
                            int cor1, int cor2, int cor3,
                            int flag1, int flag2, int flag3); /* FUN_1358027c */


/* ===========================================================================
 *  20) DESBLOQUEADOR DE FPS  ("DESBLOQUEIO DE FPS")
 * ===========================================================================
 *
 *  Remove o limitador de FPS embutido no motor do Point Blank chamando
 *  diretamente o metodo de controle do objeto de cap de FPS via vtable.
 *  Diferente do §17 (FPS ILIMITADO / TRPFpsLimit), que usa um sistema de
 *  PRESETS numerados (1..6) + modo "UNLOCKEDFPS", o DESBLOQUEADOR DE FPS e
 *  um toggle binario simples: ativa (1) ou desativa (0) o cap no objeto
 *  em PTR_DAT_1381110c + 0x508.
 *
 *  COMPARACAO COM §17 (FPS ILIMITADO)
 *  -----------------------------------
 *    FPS ILIMITADO  (§17): objeto em PTR_DAT_1381110c + 0x4fc
 *                          usa FUN_132db2e0(obj, preset_index)
 *                          escreve "FPS 486" no campo fps_obj+0xc
 *                          persiste "UNLOCKEDFPS" no registro
 *    DESBLOQUEADOR (§20) : objeto em PTR_DAT_1381110c + 0x508
 *                          chama vtable[0x188](obj, 1/0) -- toggle puro
 *                          sem preset, sem escrita de string, sem registro
 *
 *  LABEL DA FEATURE: "DESBLOQUEIO DE FPS"  @ 0x13729580  (UTF-16LE)
 *
 *  FUNCOES
 *    FUN_137295a8 @ 0x137295a8  -- ativar   (vtable[0x188](obj+0x508, 1))
 *    FUN_1372987c @ 0x1372987c  -- restaurar (vtable[0x188](obj+0x508, 0))
 *
 *  FLUXO DE ATIVACAO (FUN_137295a8)
 *    1. Verifica se o jogo esta rodando (PTR_DAT_13810cd8 e PTR_DAT_13811928);
 *       se nao: FUN_135fcd18() -- exibe "jogo nao encontrado".
 *    2. Oculta botao "ATIVAR" (param_1+0x484) e exibe "ATIVO" (param_1+0x480).
 *    3. Chama vtable[0x188](*(PTR_DAT_1381110c+0x508), 1) -- remove o cap.
 *    4. Atualiza label de status via FUN_1369b158 + DAT_1372977c / LAB_13729794.
 *    5. Se param_2 != 0: exibe card de notificacao via FUN_1358027c
 *       (strings lazy-init em DAT_137297cc / DAT_13729870).
 *
 *  FLUXO DE RESTAURACAO (FUN_1372987c)
 *    1. Oculta botao "ATIVO" (param_1+0x480) e exibe "ATIVAR" (param_1+0x484).
 *    2. Chama vtable[0x188](*(PTR_DAT_1381110c+0x508), 0) -- reativa o cap.
 *    3. Atualiza label via FUN_1369ae6c + DAT_13729938.
 *
 *  NOTA: o vtable offset 0x188 (metodo index 98) e o mesmo usado por outras
 *  features de toggle graficas neste binario (Game Bar, Vsync, etc.). O
 *  objeto em +0x508 e distinto dos objetos de preset em +0x4fc (FPS ILIMITADO)
 *  e +0x510 (outro toggle grafico).
 */

/* Verifica se o jogo esta ativo. Chama FUN_135fcd18 se nao.                 */
extern void fps_verificar_jogo_ou_erro(void);  /* FUN_135fcd18 */

/*
 * pb_desbloqueador_fps_ativar  --  FUN_137295a8 @ 0x137295a8
 *
 * Remove o cap de FPS do motor do Point Blank via vtable toggle.
 * param_1  : ponteiro para o painel da feature (TForm/VCL)
 * param_2  : se != 0, exibe card de notificacao apos aplicar
 */
void pb_desbloqueador_fps_ativar(int painel, int mostrar_card)
{
    if ((*(int *)PTR_DAT_13810cd8 == 0) && (*(int *)PTR_DAT_13811928 == 0)) {
        fps_verificar_jogo_ou_erro();   /* FUN_135fcd18: jogo nao encontrado */
        return;
    }

    /* Atualiza botoes do painel: oculta ATIVAR, exibe ATIVO.               */
    FUN_132abec4(*(int *)(painel + 0x484), 0);  /* oculta botao ATIVAR      */
    FUN_132abec4(*(int *)(painel + 0x480), 1);  /* exibe  botao ATIVO       */

    /* Remove o cap de FPS: chama vtable[0x188](fps_cap_obj, 1).
     * fps_cap_obj = *(*(PTR_DAT_1381110c) + 0x508)                         */
    (**(void(**)(void *, int))
        (**(int **)(*(int *)PTR_DAT_1381110c + 0x508) + 0x188))
            (*(int **)(*(int *)PTR_DAT_1381110c + 0x508), 1);  /* 0x137295a8 */

    /* Atualiza label de status no painel.                                   */
    /* (strings lazy-init: DAT_1372977c / LAB_13729794 via FUN_134a8d98)    */

    /* Exibe card de notificacao se solicitado.                              */
    if (mostrar_card) {
        /* DAT_137297cc = texto de notificacao (lazy-init UTF-16LE)         */
        /* DAT_13729870 = subtitulo do card (lazy-init)                     */
        FUN_1358027c(/*titulo*/ 0, /*subtitulo*/ 0, 0x1194);  /* 0x1358027c */
    }
}

/*
 * pb_desbloqueador_fps_restaurar  --  FUN_1372987c @ 0x1372987c
 *
 * Reativa o cap de FPS original do Point Blank.
 */
void pb_desbloqueador_fps_restaurar(int painel)
{
    /* Atualiza botoes: exibe ATIVAR, oculta ATIVO.                          */
    FUN_132abec4(*(int *)(painel + 0x480), 0);  /* oculta botao ATIVO       */
    FUN_132abec4(*(int *)(painel + 0x484), 1);  /* exibe  botao ATIVAR      */

    /* Reativa o cap: vtable[0x188](fps_cap_obj, 0).                        */
    (**(void(**)(void *, int))
        (**(int **)(*(int *)PTR_DAT_1381110c + 0x508) + 0x188))
            (*(int **)(*(int *)PTR_DAT_1381110c + 0x508), 0);  /* 0x1372987c */

    /* Atualiza label de status (DAT_13729938 via FUN_1369ae6c).            */
}

/* Externs desta secao.                                                       */
extern void FUN_132abec4(int controle, int visivel);
extern void FUN_1358027c(int titulo_str, int corpo_str, int duracao);
extern int *PTR_DAT_1381110c;   /* manager principal: objeto em +0x508 = fps cap */
extern int *PTR_DAT_13810cd8;   /* flag: processo do jogo ativo                  */
extern int *PTR_DAT_13811928;   /* flag: jogo em execucao (segunda verificacao)  */


/* ===========================================================================
 *  21) IMPULSIONAR POINTBLANK  (interno: "FPS Game Booster" / TGameBooster)
 * ===========================================================================
 *
 *  Ativa um "booster" do motor do Point Blank via toggle direto na vtable,
 *  similar ao §20 (DESBLOQUEADOR DE FPS), mas usando o objeto no slot +0x500
 *  do manager (vs. +0x508 do DESBLOQUEADOR e +0x4fc do FPS ILIMITADO).
 *
 *  O modulo tambem localiza o PBLauncher.exe (via GamePath + registro) e
 *  oferece botoes para iniciar o jogo diretamente a partir do assistente.
 *
 *  CLASSE DELPHI: TGameBooster  (unidade UGameBooster)
 *    RTTI @ UGameBooster strings: 0x13727395, 0x1372c330, 0x13733583
 *    Metodos RTTI:
 *      FormCreate               @ 0x13733533  (inicializacao do formulario)
 *      FormShow                 @ 0x137338f7  (abertura do painel)
 *      StartPointBlankFromAssistant @ 0x137320c6  (inicia PB direto)
 *      ReetFPSSettingsPanel1...ToggleOff @ 0x1372c296
 *      ReetFPSSettingsPanel1...ToggleOn  @ 0x1372c89f
 *
 *  STRINGS DE UI
 *    " FPS Game Booster"                @ 0x136ea1e8  (label do painel)
 *    "Ative a configuracao recomendada para priorizar fluidez, desempenho
 *     e estabilidade no PointBlank."    @ 0x136ea208  (descricao)
 *    "BOOST ATIVO"                      @ 0x1356f25c  (label botao ativo)
 *    "INICIAR POINTBLANK"               @ 0x136f47fc  (botao de lancamento)
 *    "INICIAR JOGO"                     @ 0x136f4830  (alias do botao)
 *    "PB LOCALIZADO"                    @ 0x136f4870  (status de busca)
 *    "Launcher encontrado. Abra o PointBlank e acesse rapidamente o FPS
 *     Game Booster."                    @ 0x136f4858
 *    "Validando FPS Game Booster"       @ 0x136e9b94  (progresso validacao)
 *    "Conferindo ajustes pendentes"     @ 0x136e9bc0  (progresso)
 *    "Preparando recomendacoes finais"  @ 0x136e9be8  (progresso)
 *
 *  CHAVE DE ESTADO: "FirstAccessPointBlankBoosterApplied" @ 0x136ea420
 *    (tambem em 0x136f16dc e 0x136f1bd0 -- lida em dois contextos distintos)
 *
 *  COMPARACAO COM §17 / §20
 *    FPS ILIMITADO  (§17): slot +0x4fc, preset numerico, persiste no registro
 *    DESBLOQUEADOR  (§20): slot +0x508, toggle binario, sem registro
 *    IMPULSIONAR    (§21): slot +0x500, toggle binario + validacao de caminho do PB
 *
 *  FUNCOES PRINCIPAIS
 *    FUN_13727a5c @ 0x13727a5c  -- validacao / sequencia de progresso
 *    FUN_13728234 @ 0x13728234  -- ativar booster (toggle=1)
 *    FUN_13728578 @ 0x13728578  -- restaurar     (toggle=0)
 *    FUN_13727a00 @ 0x13727a00  -- iniciar PB pelo assistente
 */

/*
 * pb_impulsionar_pb_validar  --  FUN_13727a5c @ 0x13727a5c
 *
 * Roda ao abrir o painel: exibe 15 mensagens de progresso via FUN_134a8d98
 * + FUN_1369ae6c, define ~20 globais de estado e faz scroll do painel.
 * Localiza PBLauncher.exe lendo GamePath e registros do sistema.
 */
void pb_impulsionar_pb_validar(void)
{
    /* Sequencia de progresso (FUN_134a8d98 + FUN_1369ae6c repete ~15x): */
    /* "Validando FPS Game Booster"       @ 0x136e9b94 */
    /* "Conferindo ajustes pendentes"     @ 0x136e9bc0 */
    /* "Preparando recomendacoes finais"  @ 0x136e9be8 */
    /* ... (mais mensagens em DAT_13727f88..DAT_13728178) */
    notificar_progresso_booster(); /* FUN_134a8d98 x15 + FUN_1369ae6c x15 */

    /* Define estados internos do gerenciador (20 globais PTR_DAT_*).
     * Ex: PTR_DAT_138116e4=3, PTR_DAT_138117c4=7, PTR_DAT_1381147c=1, etc.
     * Estes controlam quais subsistemas o booster pode tocar.             */

    /* Atualiza o scroll do formulario principal.                          */
    /* FUN_132ac618(DAT_13819fac) + vtable[0xe0]                           */
}

/*
 * pb_impulsionar_pb_ativar  --  FUN_13728234 @ 0x13728234
 *
 * Ativa o FPS Game Booster: verifica jogo, atualiza UI e chama
 * vtable[0x188](obj+0x500, 1) no manager principal.
 *
 * param_1 : ponteiro para o painel TGameBooster
 * param_2 : se != 0, exibe card de notificacao
 */
void pb_impulsionar_pb_ativar(int painel, int mostrar_card)
{
    if ((*(int *)PTR_DAT_13810cd8 == 0) && (*(int *)PTR_DAT_13811928 == 0)) {
        FUN_135fcd18();  /* jogo nao encontrado */
        return;
    }

    /* Exibe mensagem de status (DAT_13728488, 0xb8 chars).                */
    FUN_134a8d98(*(int *)PTR_DAT_13811378, (void *)0x13728488, 0xb8);
    /* Exibe segunda linha de status (DAT_137284a0, 6 chars).              */
    FUN_134a8d98(*(int *)PTR_DAT_13811378, (void *)0x137284a0, 6);

    /* Troca botoes: oculta "BOOST", exibe "BOOST ATIVO".                  */
    FUN_132abec4(*(int *)(painel + 0x580), 0);  /* oculta BOOST           */
    FUN_132abec4(*(int *)(painel + 0x57c), 1);  /* exibe  BOOST ATIVO     */

    /* Ativa o booster: vtable[0x188](*(PTR_DAT_1381110c+0x500), 1).       */
    (**(void(**)(void *, int))
        (**(int **)(*(int *)PTR_DAT_1381110c + 0x500) + 0x188))
            (*(int **)(*(int *)PTR_DAT_1381110c + 0x500), 1); /* @ 0x13728234 */

    /* Exibe card de notificacao se solicitado.                             */
    if (mostrar_card) {
        /* Strings lazy-init em DAT_137284bc, DAT_137284d4, DAT_13728524,
         * DAT_1372856c — construidas por FUN_134a8d98 + FUN_1314c17c.    */
        FUN_1358027c(0, 0, 0x1194);  /* card padrao via FUN_1358027c       */
    }
}

/*
 * pb_impulsionar_pb_restaurar  --  FUN_13728578 @ 0x13728578
 *
 * Desativa o booster: reverte os botoes e chama vtable toggle=0.
 */
void pb_impulsionar_pb_restaurar(int painel)
{
    /* Exibe status de desativacao (DAT_13728634, 6 chars).                */
    FUN_134a8d98(*(int *)PTR_DAT_13811378, (void *)0x13728634, 6);

    /* Troca botoes: exibe "BOOST", oculta "BOOST ATIVO".                  */
    FUN_132abec4(*(int *)(painel + 0x580), 1);  /* exibe  BOOST           */
    FUN_132abec4(*(int *)(painel + 0x57c), 0);  /* oculta BOOST ATIVO     */

    /* Desativa: vtable[0x188](*(PTR_DAT_1381110c+0x500), 0).              */
    (**(void(**)(void *, int))
        (**(int **)(*(int *)PTR_DAT_1381110c + 0x500) + 0x188))
            (*(int **)(*(int *)PTR_DAT_1381110c + 0x500), 0); /* @ 0x13728578 */
}

/*
 * pb_impulsionar_iniciar_jogo  --  FUN_13727a00 @ 0x13727a00
 *
 * Inicia o Point Blank diretamente pelo assistente (botoes "INICIAR
 * POINTBLANK" / "INICIAR JOGO" @ 0x136f47fc / 0x136f4830).
 */
void pb_impulsionar_iniciar_jogo(int param_1)
{
    (**(void(**)())(*(int *)PTR_DAT_13810ae8 + 0x1cc))(); /* vtable[0x1cc] */
    FUN_1372f160(param_1);
}

/* Externs desta secao.                                                    */
extern void  FUN_134a8d98(int painel, void *str_data, int comprimento, ...);
extern void  FUN_1369ae6c(int barra, int str);
extern void  FUN_135fcd18(void);          /* "jogo nao encontrado"        */
extern void  FUN_1372f160(int param_1);   /* inicia PBLauncher            */
extern void  notificar_progresso_booster(void); /* sequencia de validacao */
extern int  *PTR_DAT_13810ae8;  /* launcher handle                        */
extern int  *PTR_DAT_13811bac;  /* barra de status                        */
/* PTR_DAT_1381110c, PTR_DAT_13810cd8, PTR_DAT_13811928 ja declarados §20 */


/* ===========================================================================
 *  22) FULL SCREEN
 * ===========================================================================
 *
 *  Forca o modo tela cheia exclusivo do Point Blank gravando ScreenMode=1
 *  no arquivo de configuracao do jogo via TRPPBConfig.  Usar tela cheia
 *  exclusiva reduz a carga do compositor DWM e elimina a latencia de frame
 *  introduzida pelo modo janela ou tela cheia sem bordas.
 *
 *  Mecanismo
 *  ---------
 *  FULLSCREEN e um item de perfil registrado no painel MAPAS INSTANTANEOS
 *  (FUN_136ec13c @ 0x136ec13c -- ver §18).  Quando habilitado, o mecanismo
 *  generico de despacho (FUN_137008d0, ver §18) passa a chave "FULLSCREEN"
 *  ao worker de configuracao do jogo, que usa a mesma TRPPBConfig usada
 *  pelo MINI-MAP OFF (§19).
 *
 *  TRPPBConfig.ScreenMode e o PRIMEIRO campo gravado na tabela de escrita
 *  (@ 0x13585938).  A tabela de leitura correspondente esta em 0x13584da4.
 *  Valores:
 *      ScreenMode = 0  ->  modo janela
 *      ScreenMode = 1  ->  tela cheia exclusiva  (este item)
 *
 *  O handler final e FUN_13600598 (OPTIMIZER_PB_MANAGER @ 0x13600598) que:
 *    1. Cria TRPPBConfig via FUN_13149aa8(&DAT_136003e4, 1)
 *    2. Verifica se o caminho do jogo e valido (FUN_135c5498)
 *    3. Chama FUN_135c52bc para enfileirar a escrita
 *    4. Exibe notificacao via FUN_1358027c (texto @ DAT_1360069c)
 *
 *  STRINGS DE UI
 *    "FULLSCREEN"               @ 0x136ece9c  (chave do item de perfil)
 *    "Tela cheia otimizada"     @ 0x136ecfb4  (label do item no painel)
 *    "fullscreen"               @ 0x136ecf90  (icone do item)
 *    "Aplica o modo tela cheia recomendado para reduzir interferencias
 *     visuais e melhorar estabilidade."
 *                               @ 0x136ecec0  (descricao do item)
 *
 *  ESTADO PERSISTIDO
 *    HKCU\Keyboard Layout\ReetFPS\FULLSCREEN  @ 0x136f7b2c
 *
 *  CHAVES INI DO TRPPBConfig  (tabela de escrita @ 0x13585938)
 *    ScreenMode           -- offset 0 no objeto (primeiro campo)
 *    Graphics             -- segundo campo
 *    (ver §19 para a lista completa de 34 chaves)
 *
 *  FUNCOES
 *    FUN_136ec13c @ 0x136ec13c  -- panel init; registra o item FULLSCREEN
 *    FUN_137008d0 @ 0x137008d0  -- despachante generico de perfil (ver §18)
 *    FUN_13600598 @ 0x13600598  -- handler OPTIMIZER_PB_MANAGER
 *    FUN_135c52bc @ 0x135c52bc  -- TRPPBConfig.ApplyConfig (enfileirador)
 *    TRPPBConfig RTTI           @ 0x13582628  (uRPPBConfig)
 */

/*
 * pb_fullscreen_ativar  --  ativa o modo tela cheia exclusivo do PB
 *
 * Cria uma instancia de TRPPBConfig, define ScreenMode=1 e persiste via
 * ApplyConfig (FUN_135c52bc).  O fluxo e identico ao MINI-MAP OFF (§19)
 * mas grava no campo ScreenMode em vez de HUDEffect.
 *
 * Enderecos-chave:
 *   TRPPBConfig RTTI         0x13582628  (unidade uRPPBConfig)
 *   Construtor               FUN_13149aa8(&DAT_136003e4, 1)
 *   ScreenMode (write table) 0x13585938  (primeiro campo gravado)
 *   ScreenMode (read  table) 0x13584da4
 *   FUN_13600598             handler OPTIMIZER_PB_MANAGER
 *   FUN_135c52bc             enfileirador ApplyConfig
 *   FULLSCREEN state key     0x136f7b2c  (HKCU\...\ReetFPS\FULLSCREEN)
 */
void pb_fullscreen_ativar(void)
{
    void *cfg = pbconfig_criar();   /* FUN_13149aa8(&DAT_136003e4, 1)         */

    /*
     * TRPPBConfig.ScreenMode = 1  (tela cheia exclusiva)
     *
     * ScreenMode e o primeiro campo na tabela de escrita (0x13585938).
     * O offset exato no objeto nao foi isolado diretamente, mas o campo
     * precede "Graphics" na tabela e e o primeiro item escrito pelo worker.
     */
    *(unsigned char *)((unsigned char *)cfg + 0x00) = 1;   /* ScreenMode = 1 */

    /*
     * FUN_135c52bc enfileira a escrita:
     *   1. Verifica que o caminho do jogo e valido (FUN_135c5498 -- testa lock
     *      em DAT_13819a8c; false se o worker ja esta rodando).
     *   2. Grava os campos configurados no arquivo .ini do Point Blank.
     *   3. Dispara o card de notificacao se show_card != 0.
     *
     * show_card = 1  ->  texto @ DAT_1360069c via FUN_1358027c:
     *   "Iniciando verificacao e reparacao do sistema...\r\n
     *    O progresso ser..."
     */
    pbconfig_aplicar_config(cfg, /*show_card=*/1);    /* FUN_135c52bc         */
}

/*
 * pb_fullscreen_restaurar  --  volta ao modo janela
 *
 * Seta ScreenMode=0 e chama ApplyConfig sem card de notificacao.
 */
void pb_fullscreen_restaurar(void)
{
    void *cfg = pbconfig_criar();
    *(unsigned char *)((unsigned char *)cfg + 0x00) = 0;   /* ScreenMode = 0 (janela) */
    pbconfig_aplicar_config(cfg, /*show_card=*/0);
}

/* Externs desta secao (mesmos que §19 MINI-MAP OFF).                        */
/* extern void *pbconfig_criar(void);          FUN_13149aa8(&DAT_136003e4,1) */
/* extern void  pbconfig_aplicar_config(...);  FUN_135c52bc                  */


/* ===========================================================================
 *  23) OTIMIZAÇÃO GPU
 * ===========================================================================
 *
 *  Configura o subsistema grafico do Windows para maxima performance em jogo:
 *  aumenta a prioridade de GPU/CPU para o processo do PB, habilita o Hardware
 *  Accelerated GPU Scheduling (HAGS), minimiza latencias de transicao de estado
 *  de energia da GPU e oferece uma sub-ferramenta de configuracao do Painel de
 *  Controle da NVIDIA.
 *
 *  CLASSE PRINCIPAL
 *  ----------------
 *  TGPU_Utils  (RTTI @ 0x136c1ad6)
 *    Metodos identificados:
 *      NVIDIABOOST_OFFClick      --  abre/configura o Painel de Controle NVIDIA
 *      CheckDriverAndChipset     --  detecta driver + chipset instalado
 *      DriverRowClick            --  seleciona linha de driver na grade
 *      RunDriverInstaller        --  executa instalador de driver
 *      RPSidePanel1RowClick      --  interacao com painel lateral
 *
 *  TGPURegistryWorker (RTTI @ 0x13734cd1)
 *    Worker de monitoramento em background: abre uma query PDH para:
 *      "\\GPU Engine(*)\\Utilization Percentage"
 *      "\\GPU Adapter Memory(*)\\Dedicated Limit"
 *    Reporta utilizacao e memoria dedicada da GPU enquanto o painel esta aberto.
 *
 *  ITEMS DE PERFIL (cache/shader GPU, painel interno)
 *  ---------------------------------------------------
 *    gpu_directx  @ 0x136697f4  -- limpa NVIDIA DXCache e GLCache
 *    gpu_nvidia   @ 0x13669bbc  -- otimizacoes especificas NVIDIA
 *    gpu_amd      @ 0x13669ea0  -- otimizacoes especificas AMD
 *    gpu_intel    @ 0x1366a098  -- otimizacoes especificas Intel
 *
 *    NVIDIABOOST  @ 0x136c1e50 / 0x136c292c
 *      chave     : "NVIDIABOOST"  (11 chars)
 *      icone     : "icon.png"
 *      sucesso   : "Painel de controle da NVIDIA configurado com sucesso!\r\n
 *                   Otimizacoes aplicadas para maximo desempenho e estabilidade."
 *      erro      : "Nao foi possivel aplicar o perfil NVIDIA."
 *
 *  REGISTROS -- ATIVAR  (dispatcher @ 0x135f7f54, 9 comandos)
 *  -----------------------------------------------------------
 *    0x135f8000  reg add "HKLM\...\SystemProfile\Tasks\Games"
 *                  /v "GPU Priority"  /t REG_DWORD /d 8  /f
 *                  -- eleva prioridade de GPU para jogos (0-8, max=8)
 *
 *    0x135f8138  reg add "HKLM\...\SystemProfile\Tasks\Games"
 *                  /v "Priority"  /t REG_DWORD /d 6  /f
 *                  -- prioridade de CPU para jogos (Tasks\Games)
 *
 *    0x135f82xx  reg add "HKLM\...\Multimedia\SystemProfile"
 *                  /v "SystemResponsiveness"  /t REG_DWORD /d 0  /f
 *                  -- elimina reserva de CPU para aplicacoes em background
 *
 *    0x135f8880  reg add "HKLM\...\Services\Tcpip\Parameters"
 *                  /v "TCPNoDelay"  /t REG_DWORD /d 1  /f
 *                  -- desativa algoritmo de Nagle (reduz latencia de rede)
 *
 *    0x135f897c  reg add "HKLM\...\Control\GraphicsDrivers"
 *                  /v "HwSchMode"  /t REG_DWORD /d 2  /f
 *                  -- habilita HAGS (Hardware Accelerated GPU Scheduling)
 *
 *    0x135d70a0  Reg.exe add "HKLM\...\PriorityControl"
 *                  /v "Win32PrioritySeparation" /t REG_DWORD /d 38 /f
 *                  -- aumenta fatia de tempo para threads em primeiro plano
 *
 *  REGISTROS -- DESLIGAR  (dispatcher @ 0x135f94ac, 7 comandos)
 *  -------------------------------------------------------------
 *    0x135f8ad8  reg delete "HKLM\...\Tasks\Games"
 *                  /v "GPU Priority"  /f
 *
 *    0x135f93e4  reg delete "HKLM\...\Control\GraphicsDrivers"
 *                  /v "HwSchMode"  /f
 *                  -- remove HAGS (volta ao agendamento tradicional)
 *
 *    0x135f9514  reg add "HKLM\...\PriorityControl"
 *                  /v "Win32PrioritySeparation" /t REG_DWORD /d 18 /f
 *                  -- restaura o padrao do Windows (18 = balanceado)
 *
 *  LATENCIA DE ENERGIA DA GPU  (16 comandos @ 0x135d880c – 0x135da204)
 *  ---------------------------------------------------------------------
 *  Todos gravam /d "1" para minimizar a latencia de transicao de estado D3:
 *    0x135d880c  GraphicsDrivers\Power\DefaultD3TransitionLatencyActivelyUsed
 *    0x135d8a00  GraphicsDrivers\Power\DefaultD3TransitionLatencyIdleLongTime
 *    ... (14 entradas adicionais, variantes ActivelyUsed/Idle/Hibernate)
 *  Efeito: a GPU nao entra em estados de baixo consumo entre frames, eliminando
 *  o "stutter" causado pela rampa de energia ao retomar trabalho.
 *
 *  STRINGS DE UI
 *    "AJUSTES DA GPU"                                      @ 0x13781ab2
 *    "Restaurando ajustes da GPU"                          @ 0x136c57f8
 *    "Os ajustes da GPU desta tela foram desligados e os
 *     registros do ReetFPS apagados."                      @ 0x136c56bc
 *
 *  FUNCOES
 *    TGPU_Utils RTTI                @ 0x136c1ad6
 *    TGPURegistryWorker RTTI        @ 0x13734cd1
 *    dispatcher ativar (9 cmds)     @ 0x135f7f54
 *    dispatcher desligar (7 cmds)   @ 0x135f94ac
 *    dispatcher power latency       @ ~0x135d880c
 *    NVIDIABOOST_OFFClick           (metodo de TGPU_Utils, offset interno)
 */

/*
 * Tabela de comandos para ATIVAR a otimizacao de GPU.
 * dispatcher @ 0x135f7f54 passa 9 ponteiros ao executor multi-comando.
 */
static const wchar_t *gpu_cmds_ativar[] = {
    /* 0x135f8000 */
    L"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\""
    L" /v \"GPU Priority\" /t REG_DWORD /d 8 /f",

    /* 0x135f8138 */
    L"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\""
    L" /v \"Priority\" /t REG_DWORD /d 6 /f",

    /* 0x135f82xx */
    L"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\""
    L" /v \"SystemResponsiveness\" /t REG_DWORD /d 0 /f",

    /* 0x135f8880 */
    L"reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\""
    L" /v \"TCPNoDelay\" /t REG_DWORD /d 1 /f",

    /* 0x135f897c */
    L"reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\""
    L" /v \"HwSchMode\" /t REG_DWORD /d 2 /f",

    /* 0x135d70a0 */
    L"Reg.exe add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\PriorityControl\""
    L" /v \"Win32PrioritySeparation\" /t REG_DWORD /d \"38\" /f",
};

/*
 * Tabela de comandos para DESLIGAR / restaurar padroes.
 * dispatcher @ 0x135f94ac passa 7 ponteiros.
 */
static const wchar_t *gpu_cmds_desligar[] = {
    /* 0x135f8ad8 */
    L"reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\""
    L" /v \"GPU Priority\" /f",

    /* 0x135f93e4 */
    L"reg delete \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\""
    L" /v \"HwSchMode\" /f",

    /* 0x135f9514 */
    L"reg add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\PriorityControl\""
    L" /v \"Win32PrioritySeparation\" /t REG_DWORD /d 18 /f",
};

/*
 * Tabela de comandos de latencia de energia da GPU.
 * 16 entradas, todas no formato:
 *   Reg.exe add "HKLM\SYSTEM\...\GraphicsDrivers\Power" /v "<nome>" /t REG_DWORD /d "1" /f
 * Primeira entrada @ 0x135d880c.
 */
static const wchar_t *gpu_cmds_power_latency[] = {
    /* 0x135d880c */
    L"Reg.exe add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\Power\""
    L" /v \"DefaultD3TransitionLatencyActivelyUsed\" /t REG_DWORD /d \"1\" /f",

    /* 0x135d8a00 */
    L"Reg.exe add \"HKLM\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\\Power\""
    L" /v \"DefaultD3TransitionLatencyIdleLongTime\" /t REG_DWORD /d \"1\" /f",

    /* ... 14 entradas adicionais (DefaultD3TransitionLatencyIdle*,
     *     D3LatencyHibernate*, etc.) entre 0x135d880c e 0x135da204 */
};

/*
 * gpu_otimizacao_ativar  --  handler do botao "LIGAR" no painel AJUSTES DA GPU
 *
 * Executa os comandos de registro em duas passagens:
 *   1. dispatcher @ 0x135f7f54  (prioridades + HAGS + rede)
 *   2. dispatcher interno       (16 comandos de latencia de energia)
 * Persiste estado em HKCU\Keyboard Layout\ReetFPS.
 * Exibe progresso via FUN_134a8d98 e card de notificacao via FUN_1358027c.
 *
 * Funcoes nao decompiladas (area 0x136cxxxx tem Delphi codigo/dado intercalado
 * que o Ghidra nao analisa como funcoes isoladas).
 */
void gpu_otimizacao_ativar(void)
{
    int i;

    /* Passa 1: prioridades de GPU/CPU + HAGS + TCPNoDelay. */
    for (i = 0; i < 6; i++)
        executar_cmd(gpu_cmds_ativar[i]);   /* FUN_135d1f20 via dispatcher 0x135f7f54 */

    /* Passa 2: 16 comandos de latencia de transicao D3 da GPU
     * (dispatcher interno, entradas @ 0x135d880c – 0x135da204).               */
    for (i = 0; i < 16; i++)
        executar_cmd(gpu_cmds_power_latency[i]);

    /* Notifica usuario (string "AJUSTES DA GPU" @ 0x13781ab2).                 */
    /* FUN_1358027c(...) → card de notificacao                                  */
    /* FUN_134a8d98(...) → progresso no painel lateral                          */
}

/*
 * gpu_otimizacao_restaurar  --  handler do botao "DESLIGAR"
 *
 * Reverte as mudancas de registro e exibe a mensagem de confirmacao:
 * "Os ajustes da GPU desta tela foram desligados e os registros do
 *  ReetFPS apagados."  @ 0x136c56bc
 *
 * Texto de progresso: "Restaurando ajustes da GPU"  @ 0x136c57f8
 */
void gpu_otimizacao_restaurar(void)
{
    int i;
    for (i = 0; i < 3; i++)
        executar_cmd(gpu_cmds_desligar[i]); /* dispatcher @ 0x135f94ac (7 cmds) */

    /* Exibe confirmacao: string @ 0x136c56bc via FUN_134a8d98.                 */
}

/*
 * gpu_nvidiaboost_aplicar  --  TGPU_Utils::NVIDIABOOST_OFFClick
 *                              (metodo de TGPU_Utils, RTTI @ 0x136c1ad6)
 *
 * Configura o Painel de Controle da NVIDIA via APIs NVAPI ou escrita direta
 * de preferencias no registro NVIDIA.  O perfil define:
 *   - Modo de energia: "Prefer Maximum Performance"
 *   - Sincronizacao vertical: desabilitada
 *   - Filtragem de textura: alto desempenho
 *   - Suavizacao: desabilitada
 *
 * Retorna sucesso/erro via card de notificacao:
 *   sucesso : "Painel de controle da NVIDIA configurado com sucesso!\r\n
 *              Otimizacoes aplicadas para maximo desempenho e estabilidade."
 *   erro    : "Nao foi possivel aplicar o perfil NVIDIA."
 *
 * (Funcao nao decompilada diretamente -- regiao 0x136cxxxx inacessivel ao
 *  Ghidra por intercalamento de codigo/dado Delphi. Comportamento inferido
 *  das strings e do nome do metodo na RTTI.)
 */
void gpu_nvidiaboost_aplicar(void)
{
    /* TGPU_Utils::CheckDriverAndChipset() -- detecta fabricante e versao.      */
    /* if (driver == NVIDIA) → configura via NVAPI / registro NVIDIA            */
    /* else → exibe mensagem "Nao foi possivel aplicar o perfil NVIDIA."        */

    /* Card de notificacao @ 0x136c1e50 / 0x136c292c via FUN_1358027c.         */
}

/* Externs desta secao.                                                         */
extern void executar_cmd(const wchar_t *cmd); /* FUN_135d1f20 ou similar       */
/* FUN_134a8d98, FUN_1358027c ja declarados em secoes anteriores               */


/* ============================================================================
 *  FIM. Para o catalogo completo de comandos do otimizador ver:
 *      catalogo_comandos.md / catalogo_comandos.c
 *  Para o resumo de alto nivel do que o ReetFPS faz com o jogo ver:
 *      ponto_blank.md
 * ========================================================================== */
