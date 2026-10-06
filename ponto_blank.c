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
 *  Ajusta a resposta do teclado em HKCU\Control Panel\Accessibility\Keyboard
 *  Response: desliga o FilterKeys (Flags=0) e encurta o atraso/intervalo de
 *  repeticao.  A restauracao volta aos padroes do Windows.
 *
 *  HANDLERS (nenhum dos dois esta definido como funcao no Ghidra; enderecos
 *  obtidos pelo prologo 55 8B EC e pelos literais que seguem cada um)
 *    0x136b54d4  -- ATIVAR    : grava estado, troca botoes, roda 0x135e9af4,
 *                               exibe o toast
 *    0x136b5704  -- RESTAURAR : grava estado, troca botoes, roda 0x135e9d98,
 *                               sem toast
 *
 *  STRINGS DE UI
 *    "ReetFPS"                                   @ 0x136b56f4  (UTF-16LE)
 *    "Teclado Turbo ativado!\r\nLatencia reduzida e resposta imediata para
 *     comandos mais rapidos e precisos."        @ 0x136b5614  (UTF-16LE)
 *    "icon.png"                                  @ 0x136b55f4  (UTF-16LE)
 *
 *  BLOCO ATIVAR  @ 0x135e9af4  -- 3 comandos, executar_lote(arr, 2)
 *    0x135e9b30  reg add "...\Keyboard Response" /v Flags /t REG_SZ /d 0 /f >nul 2>&1
 *    0x135e9c00  reg add "...\Keyboard Response" /v AutoRepeatDelay /t REG_SZ /d 250 /f
 *    0x135e9cd4  reg add "...\Keyboard Response" /v AutoRepeatRate  /t REG_SZ /d 20 /f
 *
 *  BLOCO RESTAURAR  @ 0x135e9d98  -- 4 comandos, executar_lote(arr, 3)
 *    0x135e9ddc  /v AutoRepeatDelay /t REG_SZ /d 300 /f
 *    0x135e9eb0  /v AutoRepeatRate  /t REG_SZ /d 45 /f
 *    0x135e9f80  /v BounceTime      /t REG_SZ /d 0 /f
 *    0x135ea048  /v Flags           /t REG_SZ /d 2 /f
 *
 *  ITENS RELACIONADOS (nao chamados pelos handlers acima)
 *    "TeclasAderencia_ON" @ 0x136b6bec e "OpTeclado_ON" @ 0x136b6cac fazem
 *    parte da tabela generica de chaves de toggle (ver §15).
 *    INFERIDO: a ligacao de qualquer um deles com o "Teclado Turbo" e apenas
 *    pelo nome.
 *
 *  O QUE NAO FAZ PARTE DESTE MODULO
 *    - MouseKeys: "reg add ...\Accessibility\MouseKeys /v Flags /d 0"
 *      @ 0x135ea1a8 e o 1o de um lote de 16 comandos de MOUSE (FUN_135ea0f8:
 *      MouseSonar, MouseSpeed, MouseThreshold1/2, DoubleClickSpeed,
 *      MouseHoverTime em HKCU/HKU..., MouseSensitivity=10).  Nao e chamado
 *      pelo handler do Teclado Turbo.
 *    - Deteccao de teclado HID: a tabela de tipos (RECEPTOR @ 0x1353b088 ...)
 *      e a string "Teclado conectado" @ 0x1353b43c existem, mas o handler
 *      0x136b54d4 nao faz nenhuma checagem antes de executar o bloco.
 */

/* Despachante de comandos: FUN_135d1fb8(arr, ultimo_indice).
 * Cria um objeto de thread (FUN_135d1fdc) que copia as linhas nao vazias
 * para uma lista, monta um script .bat ("@echo off" / "setlocal ..." /
 * linhas / "exit /b %errorlevel%", FUN_135d20d8) e o executa.             */
extern void executar_lote(const wchar_t *cmds[], int ultimo_indice);   /* FUN_135d1fb8 */

/* Decodificador de strings ofuscadas: le um blob cifrado e devolve a
 * UnicodeString em claro em *saida.  NAO exibe nada na tela.              */
extern void decodificar_str(void *ctx, const void *blob, int tam,
                            void **saida, int k1, int k2, int k3);   /* FUN_134a8d98 */

/* Grava a "chave de estado" do toggle: FUN_135529c4 decodifica o caminho
 * (blob @ DAT_13552a34) e FUN_13552a50 abre HKCU (TRegistry, 0x80000001,
 * acesso 0xf003f), OpenKey(caminho, criar=1) e WriteInteger(nome, valor).
 * O caminho e o nome sao ofuscados; nao esta provado que o caminho seja
 * "Keyboard Layout\ReetFPS" (INFERIDO: e a chave de estado usada no resto
 * do programa, ex. @ 0x13467cd4).                                         */
extern void estado_obter_chave(void *obj, void **caminho_out);       /* FUN_135529c4 */
extern void estado_gravar(void *obj, void *caminho, void *nome);     /* FUN_13552a50 */

/* Mostra/oculta um controle VCL (FUN_132abec4).                          */
extern void vcl_set_visible(void *controle, int visivel);

/* Toast do ReetFPS: FUN_1358027c recebe 16 argumentos (titulo, corpo,
 * duracao em ms e parametros de layout/cor/icone).  Todas as chamadas
 * observadas usam os mesmos valores: 0x1194 (4500 ms), 5, 0xe, 0xc, 0xa0,
 * 0x17c, 0xf5, L"icon.png", -1, -1, -1, 1, 1, 1.                          */
extern void FUN_1358027c(const wchar_t *titulo, const wchar_t *corpo,
                         int duracao_ms, int p4, int p5, int p6, int p7,
                         int p8, int p9, const wchar_t *icone,
                         int p11, int p12, int p13, int p14, int p15, int p16);

extern void **PTR_DAT_13811378;   /* contexto do decodificador de strings   */
extern void **PTR_DAT_13811568;   /* objeto dono da gravacao de estado      */

static void teclado_bloco_ativar(void)                           /* 0x135e9af4 */
{
    static const wchar_t *cmds[3] = {
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v Flags /t REG_SZ /d 0 /f >nul 2>&1",                 /* 0x135e9b30 */
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v AutoRepeatDelay /t REG_SZ /d 250 /f",               /* 0x135e9c00 */
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v AutoRepeatRate /t REG_SZ /d 20 /f",                 /* 0x135e9cd4 */
    };
    executar_lote(cmds, 2);
}

static void teclado_bloco_restaurar(void)                        /* 0x135e9d98 */
{
    static const wchar_t *cmds[4] = {
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v AutoRepeatDelay /t REG_SZ /d 300 /f",               /* 0x135e9ddc */
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v AutoRepeatRate /t REG_SZ /d 45 /f",                 /* 0x135e9eb0 */
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v BounceTime /t REG_SZ /d 0 /f",                      /* 0x135e9f80 */
        L"reg add \"HKCU\\Control Panel\\Accessibility\\Keyboard Response\""
        L" /v Flags /t REG_SZ /d 2 /f",                           /* 0x135ea048 */
    };
    executar_lote(cmds, 3);
}

/* Handler ATIVAR @ 0x136b54d4.  `painel` chega em EAX (Self do form).    */
void pb_teclado_precisao_ativar(uint8_t *painel)
{
    void *nome = NULL, *caminho = NULL, *tmp = NULL;

    /* Nome do valor de estado: blob @ 0x136b55d8 (cifrado).              */
    decodificar_str(*PTR_DAT_13811378, (void *)0x136b55d8, 0xa4,
                    &tmp, 0xe, 0x93, 1);
    nome = tmp;                                  /* via FUN_1314c690       */

    estado_obter_chave(*PTR_DAT_13811568, &caminho);
    estado_gravar(*PTR_DAT_13811568, caminho, nome);

    vcl_set_visible(*(void **)(painel + 0x4dc), 0);   /* oculta "ATIVAR"   */
    vcl_set_visible(*(void **)(painel + 0x4e0), 1);   /* exibe  "ATIVO"    */

    teclado_bloco_ativar();                           /* CALL 0x135e9af4   */

    FUN_1358027c(L"ReetFPS",                                     /* 0x136b56f4 */
                 L"Teclado Turbo ativado!\r\nLatência reduzida e resposta "
                 L"imediata para comandos mais rápidos e precisos.",  /* 0x136b5614 */
                 0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                 L"icon.png",                                    /* 0x136b55f4 */
                 -1, -1, -1, 1, 1, 1);
}

/* Handler RESTAURAR @ 0x136b5704.  Mesmo padrao, botoes invertidos, sem
 * toast.  O nome do valor de estado vem de outro blob (0x136b57d0) e o
 * ultimo argumento do decodificador e 0 em vez de 1.                     */
void pb_teclado_precisao_restaurar(uint8_t *painel)
{
    void *nome = NULL, *caminho = NULL, *tmp = NULL;

    decodificar_str(*PTR_DAT_13811378, (void *)0x136b57d0, 0xa4,
                    &tmp, 0xe, 0x93, 0);
    nome = tmp;

    estado_obter_chave(*PTR_DAT_13811568, &caminho);
    estado_gravar(*PTR_DAT_13811568, caminho, nome);

    vcl_set_visible(*(void **)(painel + 0x4e0), 0);   /* oculta "ATIVO"    */
    vcl_set_visible(*(void **)(painel + 0x4dc), 1);   /* exibe  "ATIVAR"   */

    teclado_bloco_restaurar();                        /* CALL 0x135e9d98   */
}


/* ===========================================================================
 *  15) ENTRADA INSTANTANEA
 * ===========================================================================
 *
 *  ATENCAO: o binario NAO contem o rotulo "ENTRADA INSTANTANEA" (nem
 *  "instant"/"entrada" ligados a esta tela).  O nome vem da lista de
 *  recursos anunciados.  O que existe e foi verificado sao as pecas abaixo;
 *  a associacao delas com esse nome e INFERIDA pelo texto dos toasts
 *  ("menor latencia", "resposta imediata", "input lag").
 *
 *  ---------------------------------------------------------------------
 *  A) "Ajustes de desempenho"  -- handler @ 0x136b0796
 *  ---------------------------------------------------------------------
 *    Mesmo padrao do §14: grava estado (blob @ 0x136b089c), oculta +0x518,
 *    exibe +0x51c, roda o lote @ 0x135f22f0 e mostra o toast:
 *      "Ajustes de desempenho aplicados!\r\nSistema otimizado para menor
 *       latencia e resposta imediata em jogos."  @ 0x136b08e0 (UTF-16LE)
 *      titulo @ 0x136b09b8, icone "icon.png" @ 0x136b08c0
 *
 *    LOTE @ 0x135f22f0 -- 35 comandos, executar_lote(arr, 0x22):
 *      0x135f2458  VisualFXSetting=2  (2 = "ajustar para melhor desempenho")
 *      0x135f2554  VisualFXSettingPerUser=2
 *      0x135f2660  powercfg -setactive SCHEME_MIN
 *      0x135f26ac..0x135f2ad8  PROCTHROTTLEMAX 100, PERFINCTHRESHOLD 100,
 *                  PERFBOOSTMODE 0, PERFBOOSTPOL 100  (AC e DC, 8 cmds)
 *      0x135f2b6c..0x135f3078  sc stop + start= disabled para DiagTrack,
 *                  diagnosticshub.standardcollector.service, dmwappushservice,
 *                  WMPNetworkSvc, MapsBroker, DoSvc, SysMain  (14 cmds)
 *      0x135f30e0  MenuShowDelay=20
 *      0x135f3180  WaitToKillAppTimeout=2000
 *      0x135f3230  HungAppTimeout=2000
 *      0x135f32d4  LowLevelHooksTimeout=2000
 *      0x135f3384  MouseHoverTime=20
 *      0x135f3420  BackgroundAccessApplications\GlobalUserDisabled=1
 *      0x135f3530  Search\BackgroundAppGlobalToggle=0
 *      0x135f3620  sc stop wuauserv
 *      0x135f3664  sc config wuauserv start= disabled
 *      0x135f36cc  Rundll32.exe user32.dll, UpdatePerUserSystemParameters
 *
 *  ---------------------------------------------------------------------
 *  B) Game Bar  -- FUN_136b1f48 @ 0x136b1f48
 *  ---------------------------------------------------------------------
 *    Toast "Game Bar desativada!\r\nRecursos em segundo plano foram
 *    desligados para reduzir input lag e melhorar o desempenho."
 *    @ 0x136b2124.  Ver reconstrucao abaixo.
 *
 *  ---------------------------------------------------------------------
 *  C) Tarefa MMCSS "Low Latency"
 *  ---------------------------------------------------------------------
 *    As 9 strings (0x135d5b10 .. 0x135d64d0) e "Games\Latency Sensitive"
 *    (0x135d6f6c) NAO formam uma funcao propria: sao entradas de um lote
 *    unico de 232 comandos (codigo @ 0x135d2d8a, executar_lote(arr, 0xe7),
 *    literais de 0x135d383c ate ~0x135e1300) que mistura MMCSS, prioridade,
 *    rede, energia, latencia da GPU, servicos etc.
 *    Valores da tarefa "Low Latency": Affinity=0, Background Only=False,
 *    BackgroundPriority=0, Clock Rate=10000 (unidades de 100 ns = 1 ms),
 *    GPU Priority=8, Priority=2, Scheduling Category=Medium,
 *    SFIO Priority=High, Latency Sensitive=True.
 *
 *  ---------------------------------------------------------------------
 *  D) Tabela generica de chaves de toggle  (0x136b6adc .. 0x136b6e44)
 *  ---------------------------------------------------------------------
 *    EFFECTS_ON @0x136b6adc, Hibernate_ON @0x136b6b00, Services_ON @0x136b6b28,
 *    Cortana_ON @0x136b6b4c, TarefaTelemetria_ON @0x136b6b70,
 *    Superfetch_ON @0x136b6ba4, ADMENU_ON @0x136b6bcc,
 *    TeclasAderencia_ON @0x136b6bec, TelemetriaChrome_ON @0x136b6c20,
 *    TelemetriaOffice_ON @0x136b6c54, OpMouse_ON @0x136b6c88,
 *    OpTeclado_ON @0x136b6cac, OneDrive_ON @0x136b6cd4, APPS_ON @0x136b6cf8,
 *    Ativador_ON @0x136b6d14, GameDVR_ON @0x136b6d38, GameBar_ON @0x136b6d5c,
 *    XboxLive_ON @0x136b6d80, DarkTheme_ON @0x136b6da4, Volume_ON @0x136b6dcc,
 *    Transparency_ON @0x136b6dec, Notification_ON @0x136b6e18,
 *    Office_ON @0x136b6e44.
 *    Sao as chaves de estado dos toggles da tela de otimizacoes do Windows.
 *    Nada no binario as liga especificamente a "ENTRADA INSTANTANEA".
 */

/* A) Lote "Ajustes de desempenho" @ 0x135f22f0 (35 comandos).
 * Lista completa e enderecos no cabecalho acima; aqui so a forma.        */
extern const wchar_t *g_lote_ajustes_desempenho[35];   /* 0x135f2458 .. 0x135f36cc */

static void entrada_lote_ajustes(void)                           /* 0x135f22f0 */
{
    executar_lote(g_lote_ajustes_desempenho, 0x22);
}

/* Handler @ 0x136b0796 (nao definido como funcao no Ghidra).            */
void pb_ajustes_desempenho_ativar(uint8_t *painel)
{
    void *nome = NULL, *caminho = NULL, *tmp = NULL;

    decodificar_str(*PTR_DAT_13811378, (void *)0x136b089c, 0x1c,
                    &tmp, 0x28, 0x101, 1);
    nome = tmp;

    estado_obter_chave(*PTR_DAT_13811568, &caminho);
    estado_gravar(*PTR_DAT_13811568, caminho, nome);

    vcl_set_visible(*(void **)(painel + 0x518), 0);
    vcl_set_visible(*(void **)(painel + 0x51c), 1);

    entrada_lote_ajustes();                           /* CALL 0x135f22f0   */

    FUN_1358027c(L"ReetFPS",                                     /* 0x136b09b8 */
                 L"Ajustes de desempenho aplicados!\r\nSistema otimizado para "
                 L"menor latência e resposta imediata em jogos.",  /* 0x136b08e0 */
                 0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                 L"icon.png",                                    /* 0x136b08c0 */
                 -1, -1, -1, 1, 1, 1);
}

/* B) Game Bar -- FUN_136b1f48 @ 0x136b1f48.
 *
 * FUN_13551e10 decodifica um nome, le um valor de configuracao
 * (FUN_13554754) e o compara com ate 4 strings decodificadas; o handler
 * so testa se o retorno e 7.  O significado de 7 NAO foi recuperado (as
 * strings comparadas sao cifradas) -- nao e "Game Bar ja desativado".
 *
 *   retorno == 7 : decodifica uma mensagem (blob @ 0x136b20a8) e chama
 *                  FUN_13552fac(obj, msg, 0), que abre um dialogo da
 *                  aplicacao (FUN_13476c8c: botoes "CONFIRMAR"/"CANCELAR").
 *                  Nao grava nada, nao mexe nos botoes, nao mostra toast.
 *   caso contrario: grava a chave de estado (blob @ 0x136b20e8), troca os
 *                  botoes +0x470/+0x474 e mostra o toast @ 0x136b2124.
 *
 * Nenhum comando "reg add" de Game Bar e executado aqui; os comandos de
 * GameDVR/GameBar estao em outros lotes.                                 */
extern int  FUN_13551e10(void *obj);
extern void FUN_13552fac(void *obj, void *mensagem, int tipo);   /* dialogo */

void pb_gamebar_desativar(uint8_t *painel)                       /* 0x136b1f48 */
{
    void *tmp = NULL, *msg = NULL, *nome = NULL, *caminho = NULL;

    if (FUN_13551e10(*PTR_DAT_13811568) == 7) {
        decodificar_str(*PTR_DAT_13811378, (void *)0x136b20a8, 2,
                        &tmp, 0x27, 0x52, 0);
        msg = tmp;                                   /* via FUN_1314c690   */
        FUN_13552fac(*PTR_DAT_13811568, msg, 0);
        return;
    }

    decodificar_str(*PTR_DAT_13811378, (void *)0x136b20e8, 0x1d,
                    &tmp, 0x10, 0x34, 1);
    nome = tmp;
    estado_obter_chave(*PTR_DAT_13811568, &caminho);
    estado_gravar(*PTR_DAT_13811568, caminho, nome);

    vcl_set_visible(*(void **)(painel + 0x470), 0);
    vcl_set_visible(*(void **)(painel + 0x474), 1);

    FUN_1358027c(L"ReetFPS",
                 L"Game Bar desativada!\r\nRecursos em segundo plano foram "
                 L"desligados para reduzir input lag e melhorar o desempenho.",
                 0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                 L"icon.png", -1, -1, -1, 1, 1, 1);
}

/* C) Os 10 comandos MMCSS sao entradas do lote de 232 comandos
 * @ 0x135d2d8a.  Nao ha funcao "configurar MMCSS Low Latency" separada,
 * e nenhum codigo encontrado combina A + B + C numa unica acao
 * "ENTRADA INSTANTANEA" -- por isso nao ha pb_entrada_instantanea_ativar(). */
/* "..." abrevia "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia
 * \SystemProfile"; a 1a linha mostra o comando completo.              */
static const wchar_t *const k_mmcss_low_latency[] = {
    L"Reg.exe add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia"
    L"\\SystemProfile\\Tasks\\Low Latency\" /v \"Affinity\" /t REG_DWORD /d \"0\" /f",            /* 0x135d5b10 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"Background Only\" /t REG_SZ /d \"False\" /f",   /* 0x135d5c3c */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"BackgroundPriority\" /t REG_DWORD /d \"0\" /f", /* 0x135d5d78 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"Clock Rate\" /t REG_DWORD /d \"10000\" /f",     /* 0x135d5eb8 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"GPU Priority\" /t REG_DWORD /d \"8\" /f",       /* 0x135d5ff0 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"Priority\" /t REG_DWORD /d \"2\" /f",           /* 0x135d6124 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"Scheduling Category\" /t REG_SZ /d \"Medium\" /f", /* 0x135d6250 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"SFIO Priority\" /t REG_SZ /d \"High\" /f",      /* 0x135d6398 */
    L"Reg.exe add \"...\\Tasks\\Low Latency\" /v \"Latency Sensitive\" /t REG_SZ /d \"True\" /f",  /* 0x135d64d0 */
    L"Reg.exe add \"...\\Tasks\\Games\" /v \"Latency Sensitive\" /t REG_SZ /d \"True\" /f",        /* 0x135d6f6c */
};


/* ===========================================================================
 *  16) INTERFACE SEM DELAY  (toggle de transparencia do Windows)
 * ===========================================================================
 *
 *  O que o binario comprova: um par de handlers de botao (liga/desliga) que
 *  executam cada um um bloco de 6 comandos "reg add" sobre transparencia,
 *  OLED taskbar, miniaturas do DWM e ColorPrevalence, gravam o estado no
 *  registro do ReetFPS e mostram um card de notificacao.
 *
 *  INFERIDO: a ligacao deste par de handlers com o card "INTERFACE SEM
 *  DELAY" da lista de features vem do texto dos toasts, nao de uma
 *  referencia direta no binario.  O item de recomendacao "INTERFACE"
 *  (@ 0x136ed1e0, label "Interface otimizada" @ 0x136ed2c8, descricao
 *  "Aplica os ajustes recomendados de interface para melhor estabilidade e
 *  fluidez." @ 0x136ed200, icone "window") e outro mecanismo: e despachado
 *  pela lista generica de recomendacoes e nao chama estes handlers.
 *
 *  OBSERVACAO IMPORTANTE (comportamento do proprio ReetFPS):
 *    os textos dos toasts estao TROCADOS em relacao aos comandos.
 *      - o handler @ 0x136b35fc roda EnableTransparency=1 (transparencia
 *        LIGADA) e mostra "Transparencia do Windows desativada!";
 *      - o handler @ 0x136b383c roda EnableTransparency=0 (transparencia
 *        DESLIGADA) e mostra "Transparencia do Windows ativada!".
 *    A reconstrucao abaixo nomeia cada funcao pelo EFEITO dos comandos e
 *    mantem o toast que o binario realmente exibe.
 *
 *  HANDLERS (funcoes nao definidas no Ghidra; prologo 55 8B EC confirmado)
 *    0x136b35fc  -> pb_interface_transparencia_religar()
 *                   oculta +0x4ac, exibe +0x4b0, chama FUN_135e79a4
 *    0x136b383c  -> pb_interface_transparencia_desligar()
 *                   oculta +0x4b0, exibe +0x4ac, chama FUN_135e7fdc
 *    Ponteiros para os dois handlers aparecem em 0x136ae989/0x136ae9a5 e
 *    0x136af81a/0x136af85f (ligacao dos eventos de clique).
 *
 *  BLOCO "DESLIGAR" -- FUN_135e7fdc @ 0x135e7fdc (6 cmds, FUN_135d1fb8, High=5)
 *    Tambem registrado na tabela nome->rotina sob a chave
 *    "DisableTransparencyWindows" (@ 0x135fa5f4; ponteiro em 0x135fa3fd).
 *      0x135e8030  ...\Themes\Personalize  EnableTransparency         = 0
 *      0x135e8144  HKCU ...\Explorer\Advanced UseOLEDTaskbarTransparency = 0
 *      0x135e8268  HKLM ...\Explorer\Advanced UseOLEDTaskbarTransparency = 0
 *      0x135e8370  HKCU ...\DWM            AlwaysHibernateThumbnails  = 0
 *      0x135e843c  HKCU ...\DWM            ColorPrevalence            = 0
 *      0x135e8510  ...\Themes\Personalize  ColorPrevalence            = 1
 *
 *  BLOCO "RELIGAR" -- FUN_135e79a4 @ 0x135e79a4 (6 cmds, FUN_135d1fb8, High=5)
 *      0x135e79f8  ...\Themes\Personalize  EnableTransparency         = 1
 *      0x135e7b0c  HKCU ...\Explorer\Advanced UseOLEDTaskbarTransparency = 1
 *      0x135e7c30  HKLM ...\Explorer\Advanced UseOLEDTaskbarTransparency = 1
 *      0x135e7d38  HKCU ...\DWM            AlwaysHibernateThumbnails  = 1
 *      0x135e7e04  HKCU ...\DWM            ColorPrevalence            = 1
 *      0x135e7ed8  ...\Themes\Personalize  ColorPrevalence            = 0
 *
 *  STRINGS DE UI (UnicodeString)
 *    "Transparencia do Windows desativada!\r\nEfeitos visuais desativados
 *     para priorizar desempenho e reduzir latencia."     @ 0x136b3740
 *    "Transparencia do Windows ativada!\r\nEfeitos visuais restaurados
 *     para uma interface mais fluida e moderna."         @ 0x136b3980
 *    "ReetFPS" (titulo)                    @ 0x136b382c / 0x136b3a60
 *    "icon.png"                            @ 0x136b3720 / 0x136b3960
 *    Item de perfil "Transparency_ON"      @ 0x136b6dec  (INFERIDO: chave
 *    de estado; o nome gravado pelos handlers vem ofuscado, ver abaixo)
 *
 *  BLOCOS RELACIONADOS QUE NAO FAZEM PARTE DESTES HANDLERS
 *    - VisualFX "melhor desempenho": funcao @ 0x135f373c (26 cmds), inclui
 *      VisualFXSetting=0 @ 0x135f3844 e VisualFXSettingPerUser=0
 *      @ 0x135f3940.  Chamada apenas por outro handler (CALL @ 0x136b0a4a,
 *      botoes +0x51c/+0x518), sem toast.
 *    - VisualFX "restaurar": funcao @ 0x135f22f0 (35 cmds), inclui
 *      VisualFXSetting=2 @ 0x135f2458 e PerUser=2 @ 0x135f2554.  Chamada
 *      por CALL @ 0x136b081d.
 *      INFERIDO: estes dois handlers (0x136b08xx/0x136b0axx) parecem ser o
 *      item "EFFECTS_ON" (@ 0x136b6adc); nao ha referencia direta.
 *    - DisableAnimations=1 (@ 0x135d3e50) esta dentro do bloco gigante
 *      @ 0x135d2d98 (232 cmds), registrado sob a chave "TweaksAll"
 *      (@ 0x135fa5a4).  Nao e executado pelo toggle de transparencia.
 *    - MenuShowDelay/ForegroundLock/HungApp/WaitToKill/LowLevelHooks e
 *      PowerThrottlingOff: funcao @ 0x135eb0ac (6 cmds), registrada sob a
 *      chave "OptimizeProcessorForGaming" (@ 0x135fa794).  Nao pertence a
 *      esta feature.  GameMode=0 (@ 0x135eb018) fica fora dessa funcao.
 */

/* Executor de lote: recebe array de UnicodeString e o indice High.          */
extern void executar_lote_cmd(const wchar_t **cmds, int high);  /* FUN_135d1fb8 */

/* Decodificador de strings ofuscadas do ReetFPS (nao mostra nada na tela). */
extern void decodificar_string(void *ctx, const void *blob, int tam,
                               DelphiStr *saida, int k1, int k2, int k3); /* FUN_134a8d98 */

/* Escrita de estado no registro do ReetFPS (TRegistry).                     */
extern void reet_settings_preparar(void *settings, DelphiStr *saida);   /* FUN_135529c4 */
extern void reet_settings_gravar(void *settings, DelphiStr chave,
                                 DelphiStr valor);                       /* FUN_13552a50 */

extern void FUN_132abec4(int controle, int visivel);      /* TControl.Visible */
extern int *PTR_DAT_13811568;   /* objeto de configuracoes do ReetFPS          */
extern int *PTR_DAT_13811378;   /* contexto do decodificador de strings        */

/* Card de notificacao.  Convencao register do Delphi: EAX=titulo, EDX=corpo,
 * ECX=0x1194; demais 13 argumentos vao pela pilha (confirmado nos PUSH antes
 * de CALL 0x1358027c em 0x136b36b8 e 0x136b38f8).                            */
extern void FUN_1358027c(const wchar_t *titulo, const wchar_t *corpo, int p3,
                         int p4, int p5, int p6, int p7, int p8, int p9,
                         const wchar_t *icone, int c1, int c2, int c3,
                         int f1, int f2, int f3);

/* FUN_135e7fdc @ 0x135e7fdc -- desliga transparencia e efeitos do DWM.      */
static void interface_bloco_desligar(void)
{
    static const wchar_t *cmds[6] = {
        /* 0x135e8030 */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize\" /v EnableTransparency /t REG_DWORD /d 0 /f",
        /* 0x135e8144 */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\" /v UseOLEDTaskbarTransparency /t REG_DWORD /d 0 /f",
        /* 0x135e8268 */ L"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\" /v UseOLEDTaskbarTransparency /t REG_DWORD /d 0 /f",
        /* 0x135e8370 */ L"reg add \"HKCU\\SOFTWARE\\Microsoft\\Windows\\DWM\" /v AlwaysHibernateThumbnails /t REG_DWORD /d 0 /f",
        /* 0x135e843c */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\DWM\" /v ColorPrevalence /t REG_DWORD /d 0 /f",
        /* 0x135e8510 */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize\" /v ColorPrevalence /t REG_DWORD /d 1 /f",
    };
    executar_lote_cmd(cmds, 5);                              /* EDX = 5 (High) */
}

/* FUN_135e79a4 @ 0x135e79a4 -- religa transparencia e efeitos do DWM.       */
static void interface_bloco_religar(void)
{
    static const wchar_t *cmds[6] = {
        /* 0x135e79f8 */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize\" /v EnableTransparency /t REG_DWORD /d 1 /f",
        /* 0x135e7b0c */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\" /v UseOLEDTaskbarTransparency /t REG_DWORD /d 1 /f",
        /* 0x135e7c30 */ L"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\" /v UseOLEDTaskbarTransparency /t REG_DWORD /d 1 /f",
        /* 0x135e7d38 */ L"reg add \"HKCU\\SOFTWARE\\Microsoft\\Windows\\DWM\" /v AlwaysHibernateThumbnails /t REG_DWORD /d 1 /f",
        /* 0x135e7e04 */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\DWM\" /v ColorPrevalence /t REG_DWORD /d 1 /f",
        /* 0x135e7ed8 */ L"reg add \"HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize\" /v ColorPrevalence /t REG_DWORD /d 0 /f",
    };
    executar_lote_cmd(cmds, 5);
}

/* Parte comum dos dois handlers: decodifica um texto ofuscado e o grava no
 * registro do ReetFPS.  O conteudo decodificado nao foi recuperado.
 * blob = 0x136b3700 (religar) ou 0x136b3940 (desligar), tam = 0x11a,
 * chaves (0x46, 0x15, 1) ou (0x46, 0x15, 0).                                 */
static void interface_gravar_estado(const void *blob, int k3)
{
    DelphiStr dec = NULL, tmp = NULL, ref = NULL;
    decodificar_string((void *)*PTR_DAT_13811378, blob, 0x11a, &dec, 0x46, 0x15, k3);
    str_assign(&tmp, dec);                                          /* FUN_1314c690 */
    reet_settings_preparar((void *)*PTR_DAT_13811568, &ref);
    reet_settings_gravar((void *)*PTR_DAT_13811568, ref, tmp);
}

/* Handler @ 0x136b383c -- comandos DESLIGAM a transparencia; o toast exibido
 * pelo binario diz "ativada" (ver OBSERVACAO no cabecalho).                  */
void pb_interface_transparencia_desligar(uint8_t *painel)
{
    interface_gravar_estado((const void *)0x136b3940, 0);
    FUN_132abec4(*(int *)(painel + 0x4b0), 0);
    FUN_132abec4(*(int *)(painel + 0x4ac), 1);
    interface_bloco_desligar();                                     /* FUN_135e7fdc */

    FUN_1358027c(L"ReetFPS",                                         /* 0x136b3a60 */
                 L"Transparencia do Windows ativada!\r\n"
                 L"Efeitos visuais restaurados para uma interface "
                 L"mais fluida e moderna.",                          /* 0x136b3980 */
                 0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                 L"icon.png",                                        /* 0x136b3960 */
                 -1, -1, -1, 1, 1, 1);
}

/* Handler @ 0x136b35fc -- comandos RELIGAM a transparencia; o toast exibido
 * pelo binario diz "desativada" (ver OBSERVACAO no cabecalho).               */
void pb_interface_transparencia_religar(uint8_t *painel)
{
    interface_gravar_estado((const void *)0x136b3700, 1);
    FUN_132abec4(*(int *)(painel + 0x4ac), 0);
    FUN_132abec4(*(int *)(painel + 0x4b0), 1);
    interface_bloco_religar();                                      /* FUN_135e79a4 */

    FUN_1358027c(L"ReetFPS",                                         /* 0x136b382c */
                 L"Transparencia do Windows desativada!\r\n"
                 L"Efeitos visuais desativados para priorizar "
                 L"desempenho e reduzir latencia.",                  /* 0x136b3740 */
                 0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                 L"icon.png",                                        /* 0x136b3720 */
                 -1, -1, -1, 1, 1, 1);
}


/* ===========================================================================
 *  17) FPS ILIMITADO  (formulario TUNLOCK_FPS / controle TRPFpsLimit)
 * ===========================================================================
 *
 *  O que o binario mostra: e uma TELA de escolha de preset de FPS (1..6).
 *  O indice escolhido vai para os controles da interface e para o JSON de
 *  configuracoes do ReetFPS. Nenhuma funcao desta cadeia escreve no processo
 *  do jogo nem no registro.
 *
 *  FORMULARIO: TUNLOCK_FPS  (RTTI: nome da classe @ 0x13693538,
 *              nome da unit "UNLOCKEDFPS" @ 0x1369372c)
 *    "UNLOCKEDFPS" NAO e um valor gravado: e o nome da unit Delphi do form.
 *    Metodos publicados (tabela RTTI logo apos 0x13693538):
 *      INC_FPS_VALUEClick  @ 0x136940f8  idx = (idx < 6) ? idx + 1 : 1
 *      DEC_FPS_VALUEClick  @ 0x13694118  idx = (idx > 1) ? idx - 1 : 1
 *      FormCreate          @ 0x13694138  vazio (so RET)
 *      FormShow            @ 0x1369413c  inicializa o form (escala/estado)
 *      Image3Click         @ 0x136941cc  aplica (FUN_1369407c) e chama
 *                                        FUN_13358248(self) (INFERIDO: Close)
 *      RPFpsLimit1Change   @ 0x136941e4  (Sender, Index) -> pre-visualiza
 *      RPFpsLimit1Apply    @ 0x136941ec  chama Image3Click
 *    O controle TRPFpsLimit fica em form+0x448 (titulo "TURBINAR FPS"
 *    @ 0x136918f0). "SEM LIMITE" @ 0x13692714 e "LIMITE DE FPS" @ 0x13692738
 *    sao textos desenhados por esse controle; a regra que escolhe entre eles
 *    nao foi rastreada.
 *
 *  ESTADO
 *    _DAT_1380f72c  indice de preset atual (1..6)
 *    _DAT_1380f728  copia do indice enviada ao controle TRPFpsLimit
 *    DAT_13819b44   instancia do form TUNLOCK_FPS (0 se fechado)
 *
 *  PERSISTENCIA: chave "FPS_SELECTION_INDEX" @ 0x13694054 no JSON de
 *  configuracoes (FUN_1369b158 -- ver abaixo). Nao e registro direto nem o
 *  INI do jogo. Como o JSON chega ao disco nao foi rastreado aqui.
 *
 *  ITEM DE RECOMENDACAO: "FPS_SELECTION_INDEX" ("FPS recomendado") na lista
 *  de FUN_136ec13c (ver §9/§18). O despachante FUN_136f78d0 liga essa chave
 *  a FUN_137294d8, que troca os botoes +0x478/+0x474 do form principal e
 *  chama vtable+0x1cc do objeto em PTR_DAT_13811880 (INFERIDO: exibe este
 *  formulario). Sem o jogo aberto chama FUN_135fcd18.
 *
 *  LEITURA NO INICIO: FUN_13693750 @ 0x13693750 le "FPS_SELECTION_INDEX"
 *  (@ 0x136937e8) do JSON (FUN_1369b8f4), faz Trim (FUN_1316c638) e
 *  TryStrToInt (FUN_1316d204); se nao houver valor, usa 3.
 *
 *  ONDE O FPS CHEGA AO JOGO: NAO LOCALIZADO. O TRPPBConfig (§19) grava a
 *  secao [Graphics] ("Graphics" @ 0x1358595c) de
 *  <pasta do jogo>\EnvSet\env_settings.ini (@ 0x13584570) em FUN_13585130;
 *  ali FPSType vem do campo +0x51c (chave @ 0x13585acc) e FPSVal do campo
 *  +0x520 (chave @ 0x13585ae8). Nenhuma escrita nesses campos a partir do
 *  indice FPS_SELECTION_INDEX foi encontrada: o global _DAT_1380f72c so e
 *  acessado pelo proprio form, e os acessos a +0x51c perto de 0x1372b36d /
 *  0x1372b6ce sao toggles do form principal, nao do TRPPBConfig.
 */

/*
 * fps_ler_indice_salvo  --  FUN_13693750 @ 0x13693750
 */
void fps_ler_indice_salvo(int *out)
{
    DelphiStr s = NULL, t = NULL;
    *out = 3;
    config_ler_str(*(void **)PTR_DAT_13811bac,
                   L"FPS_SELECTION_INDEX", &s);                /* FUN_1369b8f4 */
    trim(s, &t);                                               /* FUN_1316c638 */
    if (t != NULL)
        trystrtoint(t, out);                                   /* FUN_1316d204 */
}

/*
 * fps_clamp_preset  --  FUN_13693740 @ 0x13693740
 */
int fps_clamp_preset(int index)
{
    if (index < 1 || index > 6)
        index = 3;
    return index;
}

/*
 * fps_ao_aplicar_com_jogo_aberto  --  FUN_13693d44 @ 0x13693d44
 *
 * NAO verifica o jogo: so age quando ele ja esta aberto.
 */
void fps_ao_aplicar_com_jogo_aberto(void)
{
    if (*(int *)PTR_DAT_13810cd8 != 0 || *(int *)PTR_DAT_13811928 != 0) {
        DAT_1380f724 = 1;
        /* Consulta a chave L"FPS" (DAT_13693d80) no JSON de configuracoes;
         * o valor lido e descartado nesta funcao.                            */
        config_ler(*(void **)PTR_DAT_13811bac, L"FPS");      /* FUN_1369b87c */
    }
}

/*
 * fps_atualizar_controle  --  FUN_13693f74 @ 0x13693f74
 *
 * Repassa o indice ao controle TRPFpsLimit do form TUNLOCK_FPS (se aberto).
 * NAO e uma thread de desempenho.
 */
void fps_atualizar_controle(int index)
{
    int idx = fps_clamp_preset(index);
    if (DAT_13819b44 != 0) {
        _DAT_1380f728 = idx;
        /* FUN_1369190c: clamp 1..6 (FUN_13191200), grava em campo [0xc4]
         * do controle e chama vtable+0xe0 (repintar) se mudou.               */
        fpslimit_set_indice(*(void **)(DAT_13819b44 + 0x448), idx);
    }
}

/*
 * fps_preset_salvar  --  FUN_13693fec @ 0x13693fec
 *
 * IntToStr (FUN_1316cf60) + grava no JSON de configuracoes.
 */
void fps_preset_salvar(int index)
{
    DelphiStr s = NULL;
    inttostr(fps_clamp_preset(index), &s);                     /* FUN_1316cf60 */
    config_gravar(*(void **)PTR_DAT_13811bac,
                  L"FPS_SELECTION_INDEX", s);                  /* FUN_1369b158 */
}

/*
 * fps_atualizar_interface  --  FUN_13693e20 @ 0x13693e20
 *
 * So interface: rotulos do form principal (PTR_DAT_13810970) e legenda
 * de um item de lista. Os textos de cada preset sao blobs ofuscados
 * decodificados por FUN_134a8d98 (FUN_13693810 / FUN_13693a64), entao o
 * texto exato de cada preset nao aparece em claro no binario.
 */
void fps_atualizar_interface(int index)
{
    DelphiStr rotulo = NULL, legenda = NULL;
    int idx = fps_clamp_preset(index);

    fps_rotulo_preset(idx, &rotulo);                           /* FUN_13693810 */
    fps_mostrar_rotulos();                                     /* FUN_13693db4 */
    vcl_set_text(*(void **)(*(int *)PTR_DAT_13810970 + 0x560), rotulo);
    vcl_set_text(*(void **)(*(int *)PTR_DAT_13810970 + 0x514), rotulo);
    vcl_set_text(*(void **)(*(int *)PTR_DAT_13810970 + 0x55c), rotulo);
    if (DAT_13819b44 != 0)
        fpslimit_set_indice(*(void **)(DAT_13819b44 + 0x448), idx);

    fps_legenda_preset(idx, &legenda);                         /* FUN_13693a64 */
    /* FUN_13574f7c: troca o texto em item+0xc e repinta (FUN_13203484).
     * O item vem de form+0x5f8 -> +0x310 (FUN_13575414 / FUN_135750f8).     */
    item_set_texto(lista_item(*(int *)PTR_DAT_13810970 + 0x5f8), legenda);
}

/*
 * fps_previsualizar  --  @ 0x13693f9c (sem funcao definida no Ghidra)
 *
 * Usado por INC/DEC e RPFpsLimit1Change: atualiza o indice e a interface,
 * mas NAO grava FPS_SELECTION_INDEX.
 */
void fps_previsualizar(int index)
{
    int idx = fps_clamp_preset(index);
    _DAT_1380f72c = idx;
    if (*(int *)PTR_DAT_1381110c != 0 &&
        *(int *)(*(int *)PTR_DAT_1381110c + 0x4fc) != 0)
        trackbar_set_posicao(*(void **)(*(int *)PTR_DAT_1381110c + 0x4fc),
                             idx);                             /* FUN_132db2e0 */
    fps_atualizar_controle(idx);
    fps_atualizar_rotulo(idx);                                 /* FUN_13693f0c */
}

/*
 * pb_fps_definir_preset  --  FUN_1369407c @ 0x1369407c
 *
 * Aplicar (Image3Click / RPFpsLimit1Apply). Igual a previsualizacao, mas
 * tambem grava o indice no JSON e atualiza toda a interface.
 *
 * O objeto em PTR_DAT_1381110c + 0x4fc e um controle de interface:
 * FUN_132db2e0 -> FUN_132db164(obj, pos, [+0x2ec], [+0x2f0]), padrao de
 * SetParams de trackbar (posicao, minimo, maximo).
 */
void pb_fps_definir_preset(int index)
{
    int idx = fps_clamp_preset(index);

    fps_ao_aplicar_com_jogo_aberto();

    if (*(int *)PTR_DAT_1381110c != 0 &&
        *(int *)(*(int *)PTR_DAT_1381110c + 0x4fc) != 0)
        trackbar_set_posicao(*(void **)(*(int *)PTR_DAT_1381110c + 0x4fc),
                             idx);                             /* FUN_132db2e0 */

    _DAT_1380f72c = idx;
    fps_atualizar_controle(idx);
    fps_preset_salvar(idx);
    fps_atualizar_interface(idx);
}

/*
 * config_gravar  --  FUN_1369b158 @ 0x1369b158
 *
 * Grava um par chave/valor no JSON de configuracoes guardado como string
 * em PTR_DAT_13811610:
 *   1. FUN_133db684: ParseJSONValue da string atual;
 *   2. FUN_133e28dc: le o valor atual da chave;
 *   3. FUN_133dd464: remove o par antigo; para certas chaves (comparadas
 *      com strings ofuscadas) remove tambem pares relacionados;
 *   4. FUN_133dd2e0: adiciona o novo par;
 *   5. FUN_133d9aac: serializa e grava de volta em PTR_DAT_13811610.
 */

/* Prototipos desta secao. */
extern void  config_ler(void *cfg, const wchar_t *chave);                 /* FUN_1369b87c */
extern void  config_gravar(void *cfg, const wchar_t *chave, DelphiStr v); /* FUN_1369b158 */
extern void  config_ler_str(void *cfg, const wchar_t *chave, DelphiStr *v); /* FUN_1369b8f4 */
extern void  trim(DelphiStr s, DelphiStr *dst);                           /* FUN_1316c638 */
extern bool  trystrtoint(DelphiStr s, int *v);                            /* FUN_1316d204 */
extern void  inttostr(int v, DelphiStr *dst);                             /* FUN_1316cf60 */
extern void  fpslimit_set_indice(void *controle, int idx);                /* FUN_1369190c */
extern void  trackbar_set_posicao(void *controle, int pos);               /* FUN_132db2e0 */
extern void  fps_rotulo_preset(int idx, DelphiStr *dst);                  /* FUN_13693810 */
extern void  fps_legenda_preset(int idx, DelphiStr *dst);                 /* FUN_13693a64 */
extern void  fps_mostrar_rotulos(void);                                   /* FUN_13693db4 */
extern void  fps_atualizar_rotulo(int idx);                               /* FUN_13693f0c */
extern void  vcl_set_text(void *controle, DelphiStr texto);               /* FUN_132ac010 */
extern void *lista_item(int controle_lista);           /* FUN_13575414 + FUN_135750f8 */
extern void  item_set_texto(void *item, DelphiStr texto);                 /* FUN_13574f7c */
extern int   DAT_1380f724, _DAT_1380f728, _DAT_1380f72c, DAT_13819b44;


/* ===========================================================================
 *  18) MAPAS INSTANTANEOS  (item de recomendacao "LOADINGMAP")
 * ===========================================================================
 *
 *  No binario, "mapas instantaneos" corresponde ao item LOADINGMAP
 *  ("Carregamento de mapa otimizado" @ 0x136ece50, icone "clock") da lista
 *  generica de recomendacoes. Nao existe um painel proprio.
 *
 *  LISTA DE RECOMENDACOES: FUN_136ec13c @ 0x136ec13c (a mesma tabela que a
 *  §9 descreve). Cada item e criado por FUN_136ebbb4(painel, chave, titulo,
 *  1, grupo, icone, descricao):
 *    grupo +0x310 (ajustes do Windows): Energia_ON ("Windows Turbo +FPS"),
 *      Hibernate_ON, Cortana_ON, TarefaTelemetria_ON, Superfetch_ON,
 *      ADMENU_ON, OpMouse_ON
 *    grupo +0x311 (perfil do PointBlank): FLUIDEZMAX, LOADINGMAP,
 *      FULLSCREEN, OPTIMIZER_PB_MANAGER, PRIORITYPB, INTERFACE,
 *      FPS_SELECTION_INDEX, REETGAMEMODE
 *  Superfetch_ON e um item separado, do outro grupo: NAO faz parte do
 *  LOADINGMAP.
 *
 *  DESPACHANTE: FUN_136f78d0 @ 0x136f78d0 compara a chave do item com as
 *  strings em 0x136f7ae0..0x136f7c24 e chama o handler pelo invocador
 *  FUN_136f782c(codigo, form_principal):
 *    FLUIDEZMAX / FLUIDEZMAXIMA  -> 0x1372a03c
 *    FULLSCREEN                  -> 0x1372dfb0
 *    OPTIMIZER_PB_MANAGER        -> 0x13729094
 *    FPS_SELECTION_INDEX         -> 0x137294d8  (abre o form da §17)
 *    PRIORITYPB                  -> 0x1372e888
 *    LOADINGMAP  @ 0x136f7be0    -> FUN_13728d20
 *    INTERFACE   @ 0x136f7c04    -> 0x1372d4b8
 *    REETGAMEMODE @ 0x136f7c24   -> 0x1372c00c
 *  Chave desconhecida gera excecao (mensagem @ 0x136f7c4c). Se o form
 *  principal nao existe, a excecao usa "GameBooster nao esta criado."
 *  @ 0x136f7a98. Estas strings sao comparacoes do despachante, NAO chaves
 *  de registro.
 *
 *  HANDLER: FUN_13728d20 @ 0x13728d20
 *    Mesmo padrao das §20/§21, com o objeto em PTR_DAT_1381110c + 0x4b0:
 *    troca os botoes do painel, chama vtable+0x188(obj, 1), grava um par no
 *    JSON de configuracoes e, se pedido, mostra o card. Chave, valor e
 *    textos do card sao blobs ofuscados (DAT_13728ef8, DAT_13728f10,
 *    DAT_13728f4c, LAB_13728fbc).
 *
 *  O QUE NAO FOI COMPROVADO
 *    Que comandos o sistema executa quando o objeto em +0x4b0 recebe
 *    vtable+0x188(1). A versao anterior desta secao ligava LOADINGMAP aos
 *    caches do LanmanWorkstation (0x135d50b4 / 0x135d51ec / 0x135d5328) e
 *    ao SysMain/Prefetch. Esses comandos existem no catalogo, mas nenhuma
 *    referencia a partir de FUN_13728d20 foi encontrada.
 *    INFERIDO: o objeto em +0x4b0 e um controle do painel (o metodo
 *    vtable+0x188 e o mesmo usado em outros toggles de interface). O
 *    trabalho real deve ocorrer num evento desse controle, que nao foi
 *    rastreado.
 */

/*
 * pb_loadingmap_ativar  --  FUN_13728d20 @ 0x13728d20
 *
 * form       : form principal (passado pelo despachante)
 * mostrar_card: != 0 exibe o card de notificacao
 */
void pb_loadingmap_ativar(int form, int mostrar_card)
{
    if (*(int *)PTR_DAT_13810cd8 == 0 && *(int *)PTR_DAT_13811928 == 0) {
        pb_jogo_nao_aberto();                                 /* FUN_135fcd18 */
        return;
    }

    vcl_set_visible(*(void **)(form + 0x4e0), 0);             /* FUN_132abec4 */
    vcl_set_visible(*(void **)(form + 0x4dc), 1);

    {
        int *obj = *(int **)(*(int *)PTR_DAT_1381110c + 0x4b0);
        (*(void (**)(int *, int))(*obj + 0x188))(obj, 1);
    }

    {
        DelphiStr chave = NULL, valor = NULL;
        decodificar_string(&chave, (void *)0x13728ef8);       /* FUN_134a8d98 */
        decodificar_string(&valor, (void *)0x13728f10);
        config_gravar(*(void **)PTR_DAT_13811bac, chave, valor); /* FUN_1369b158 */
    }

    if (mostrar_card) {
        DelphiStr titulo = NULL, corpo = NULL;
        decodificar_string(&titulo, (void *)0x13728f4c);
        decodificar_string(&corpo,  (void *)0x13728fbc);
        notificacao_card(titulo, corpo, 0x1194);              /* FUN_1358027c */
    }
}

/* Prototipos desta secao. */
extern void pb_jogo_nao_aberto(void);                              /* FUN_135fcd18 */
extern void vcl_set_visible(void *controle, int visivel);          /* FUN_132abec4 */
extern void decodificar_string(DelphiStr *dst, void *blob);        /* FUN_134a8d98 + FUN_1314c690 */
extern void notificacao_card(DelphiStr titulo, DelphiStr corpo, int duracao_ms); /* FUN_1358027c */


/* ===========================================================================
 *  19) MINI-MAP OFF
 * ===========================================================================
 *
 *  MECANISMO NAO LOCALIZADO NO BINARIO.
 *
 *  O nome "MINI-MAP OFF" vem da lista de funcionalidades anunciadas; ele
 *  NAO aparece como string no executavel (busca por "mini", "minimap",
 *  "radar", "mapa" e "missao": nenhum resultado relevante).  Tambem nao
 *  existe chave de item de perfil para ele: o despachante de itens do
 *  PointBlank (codigo em 0x136f78d0, ver abaixo) conhece apenas
 *  FLUIDEZMAX/FLUIDEZMAXIMA, FULLSCREEN, OPTIMIZER_PB_MANAGER,
 *  FPS_SELECTION_INDEX, PRIORITYPB, LOADINGMAP, INTERFACE e REETGAMEMODE.
 *
 *  O unico vestigio literal e o nome de icone "map-off" @ 0x13441d9c, que
 *  fica numa tabela de icones (nome -> caminho SVG, ao lado de "fullscreen"
 *  @ 0x13441d18).  Isso prova que a UI tem um icone de "mapa desligado", nao
 *  qual codigo ele dispara.
 *
 *  O que esta comprovado e a classe que o ReetFPS usa para ler e gravar o
 *  arquivo de configuracao do jogo, TRPPBConfig.  Ela contem a chave
 *  [Game] Enable_MissionIndicator, que e a candidata mais provavel para um
 *  "mini-mapa off" -- mas nenhum trecho encontrado grava essa chave com 0.
 *
 *  CLASSE TRPPBConfig  (unidade uRPPBConfig)
 *    TypeInfo  @ 0x13582610  (tkClass, nome "TRPPBConfig")
 *    VMT       @ 0x135818e0
 *    52 propriedades publicadas; a tabela de propriedades comeca em ~0x13582640
 *
 *    Carregar : FUN_135845f4 @ 0x135845f4   (le todas as chaves do .ini)
 *    Salvar   : FUN_13585130 @ 0x13585130   (grava todas as chaves no .ini)
 *    Caminho  : FUN_1358450c @ 0x1358450c   -> <pasta do jogo> +
 *               "EnvSet\env_settings.ini"
 *               (a pasta vem de FUN_13584484)
 *
 *  CHAVES DO env_settings.ini, na ordem em que FUN_135845f4 as le
 *  (offset do campo no objeto TRPPBConfig; padrao usado na leitura quando
 *   o valor nao existe no arquivo)
 *
 *    [Default]  DXVersion               +0x4f4  int
 *    [Game]     EnablePhysX             +0x531  bool
 *               TeamBand                +0x536  bool  padrao 1
 *               DisableAccessory        +0x537  bool  padrao 0
 *               WeaponEffect            +0x532  bool  padrao 0
 *               HUD_Effect              +0x533  bool  padrao 0
 *               Enable_MissionIndicator +0x538  bool  padrao 1
 *               EnableBulletTrace       +0x534  bool  padrao 1
 *               EnableBulletSmoke       +0x535  bool  padrao 1
 *    [Graphics] ScreenMode              +0x4f8  int
 *               ScreenWidth             +0x4fc  int
 *               ScreenHeight            +0x500  int
 *               RefreshRate             +0x504  int
 *               AntiAlias               +0x508  int
 *               ShadowQualityType       +0x50c  int
 *               TextureQualityType      +0x510  int
 *               SpecularQualityType     +0x514  int
 *               EffectQuality           +0x518  int
 *               FPSType                 +0x51c  int
 *               FPSVal                  +0x520  int
 *               GammaVal                +0x524  float
 *               FovValue                +0x528  float
 *               VSync                   +0x52c  bool
 *               TriLinearFilter         +0x52d  bool
 *               DynamicLight            +0x52e  bool
 *               EnableNormalMap         +0x52f  bool
 *               EnableTerrainEffect     +0x530  bool
 *    [DX11]     HDR                     +0x539  bool
 *               RimLight                +0x53a  bool
 *               IBL                     +0x53b  bool
 *               SSAO                    +0x53c  bool
 *               SSR                     +0x53d  bool
 *
 *  Os offsets conferem com a tabela RTTI de propriedades: cada entrada tem
 *  GetProc/SetProc = 0xFF000000 | offset (acesso direto ao campo), por
 *  exemplo ScreenMode = 0xff0004f8 e HUDEffect = 0xff000533.  As chaves
 *  TeamBand, DisableAccessory e Enable_MissionIndicator sao lidas e gravadas
 *  no .ini, mas NAO sao propriedades publicadas (nao aparecem na RTTI).
 *
 *  NAO CONFUNDIR: FUN_13600598 @ 0x13600598 nao tem relacao com esta classe.
 *  Ela enfileira LAB_135bc7e8 no worker generico FUN_135c52bc e mostra o
 *  card "Iniciando verificacao e reparacao do sistema..." (DAT_1360069c),
 *  ou seja, e o REPARO DO SISTEMA.
 */

/* Campos de TRPPBConfig usados abaixo (offsets comprovados acima).          */
#define PBCFG_SCREENMODE           0x4f8
#define PBCFG_HUD_EFFECT           0x533
#define PBCFG_MISSION_INDICATOR    0x538

/* Metodos da instancia de TIniFile criada por FUN_132655fc(&PTR_LAB_13264f44,
 * 1, caminho).  Chamados via vtable: +0x10 le inteiro, +0x34 le float,
 * +0x0c grava string, +0x14 grava inteiro.  Booleanos passam por
 * FUN_13582d40 (leitura) e FUN_135845a0 (bool -> texto) na gravacao.      */
extern void *ini_criar(DelphiStr caminho);                 /* FUN_132655fc */
extern void  ini_liberar(void *ini);                       /* FUN_13149ad8 */
extern int   ini_ler_int (void *ini, const wchar_t *sec, const wchar_t *chave, int padrao);
extern bool  ini_ler_bool(void *ini, const wchar_t *sec, const wchar_t *chave, bool padrao); /* FUN_13582d40 */
extern void  ini_gravar_int (void *ini, const wchar_t *sec, const wchar_t *chave, int valor);
extern void  ini_gravar_bool(void *ini, const wchar_t *sec, const wchar_t *chave, bool valor);
extern void  pbconfig_caminho_ini(void *cfg, DelphiStr *dst); /* FUN_1358450c */

/*
 * pbconfig_carregar  --  FUN_135845f4 @ 0x135845f4
 *
 * Trecho reconstruido: so as chaves relevantes para §19 e §22.  A funcao
 * original le as 34 chaves da tabela acima, na mesma ordem.
 */
void pbconfig_carregar(uint8_t *cfg)
{
    DelphiStr caminho = NULL;
    pbconfig_caminho_ini(cfg, &caminho);   /* ...\EnvSet\env_settings.ini */
    void *ini = ini_criar(caminho);

    *(bool *)(cfg + PBCFG_HUD_EFFECT) =
        ini_ler_bool(ini, L"Game", L"HUD_Effect", false);
    *(bool *)(cfg + PBCFG_MISSION_INDICATOR) =
        ini_ler_bool(ini, L"Game", L"Enable_MissionIndicator", true);
    *(int *)(cfg + PBCFG_SCREENMODE) =
        ini_ler_int(ini, L"Graphics", L"ScreenMode",
                    *(int *)(cfg + PBCFG_SCREENMODE));
    /* ... demais chaves ... */

    ini_liberar(ini);
}

/*
 * pbconfig_salvar  --  FUN_13585130 @ 0x13585130
 *
 * Grava os campos de volta no env_settings.ini (mesma ordem da leitura).
 */
void pbconfig_salvar(uint8_t *cfg)
{
    DelphiStr caminho = NULL;
    pbconfig_caminho_ini(cfg, &caminho);
    void *ini = ini_criar(caminho);

    ini_gravar_bool(ini, L"Game", L"HUD_Effect",
                    *(bool *)(cfg + PBCFG_HUD_EFFECT));
    ini_gravar_bool(ini, L"Game", L"Enable_MissionIndicator",
                    *(bool *)(cfg + PBCFG_MISSION_INDICATOR));
    ini_gravar_int (ini, L"Graphics", L"ScreenMode",
                    *(int *)(cfg + PBCFG_SCREENMODE));
    /* ... demais chaves ... */

    ini_liberar(ini);
}

/* INFERIDO: se o "MINI-MAP OFF" existe como acao do ReetFPS, o caminho mais
 * provavel e carregar o TRPPBConfig, zerar Enable_MissionIndicator (+0x538)
 * e salvar.  Nenhum codigo que faca essa escrita foi localizado; por isso
 * nao ha funcao pb_minimap_off_ativar() nesta reconstrucao.                 */


/* ===========================================================================
 *  20) DESBLOQUEADOR DE FPS  (botoes FPSUNLOCKED_ON / FPSUNLOCKED_OFF)
 * ===========================================================================
 *
 *  ONDE ESTA
 *    Formulario TGameBooster (unidade UGameBooster).  A tabela de metodos
 *    publicados (~0x13724890 em diante) liga cada botao ao seu handler:
 *      FPSUNLOCKED_OFFClick  @ 0x137294d8   (botao FPSUNLOCKED_OFF, campo +0x478)
 *      FPSUNLOCKED_ONClick   @ 0x13729524   (botao FPSUNLOCKED_ON,  campo +0x474)
 *    Campos (tabela de campos publicados, ~0x13723ca0):
 *      Panel_FPSUNLOCKED +0x46c, Label1 +0x470, FPSUNLOCKED_ON +0x474,
 *      FPSUNLOCKED_OFF +0x478.
 *    O Ghidra nao criou funcoes nesses dois enderecos; o fluxo abaixo vem da
 *    desmontagem direta (disassemble_bytes 0x137294d8..0x13729574).
 *
 *    Convencao dos botoes: o botao "_OFF" fica visivel quando o recurso esta
 *    desligado; clicar nele LIGA o recurso e troca para o botao "_ON".
 *    Clicar em "_ON" DESLIGA.
 *
 *  LIGAR  (FPSUNLOCKED_OFFClick @ 0x137294d8)
 *    1. Pre-condicao: *PTR_DAT_13810cd8 != 0 ou *PTR_DAT_13811928 != 0;
 *       senao chama FUN_135fcd18 (mostra um aviso de texto cifrado,
 *       DAT_135fcdc0) e sai.
 *    2. Oculta FPSUNLOCKED_OFF e exibe FPSUNLOCKED_ON (FUN_132abec4 =
 *       TControl.SetVisible: grava +0x69 e envia CM_VISIBLECHANGED 0xB00B).
 *    3. Chama o metodo virtual +0x1cc do formulario em *PTR_DAT_13811880.
 *       INFERIDO: abre o dialogo de escolha de limite de FPS.  O mesmo slot
 *       +0x1cc e usado pelos handlers de outros botoes deste form para abrir
 *       outros formularios (ver 0x13727a00..0x13727a5b); a classe do form em
 *       PTR_DAT_13811880 nao foi identificada.
 *    Nao ha comando de shell, escrita de registro nem card de notificacao
 *    neste handler.
 *
 *  DESLIGAR  (FPSUNLOCKED_ONClick @ 0x13729524)
 *    1. Oculta FPSUNLOCKED_ON e exibe FPSUNLOCKED_OFF.
 *    2. FUN_13728c00:
 *         - FUN_132db2e0(*(*PTR_DAT_1381110c + 0x4fc), 0): zera o mesmo
 *           controle que o secao 17 usa para o preset de FPS;
 *         - remove uma chave cifrada (DAT_13728cac) do store de configuracoes
 *           (FUN_1369ae6c, ver nota no fim da secao);
 *         - oculta o controle +0x560 do form em DAT_13819fac.
 *    3. FUN_13728cc4: oculta os controles +0x554, +0x558, +0x560, +0x514 e
 *       +0x55c do form em DAT_13819fac (os tres ultimos sao os que o secao 17
 *       preenche com o rotulo do preset).
 *    4. Volta o texto do item [0][0] do ReetFPSSettingsPanel1 (campo +0x5f8,
 *       categorias em +0x310, itens em +0x24) para "DESBLOQUEIO DE FPS"
 *       (@ 0x13729580, UTF-16LE) via FUN_13574f7c, que so troca o texto
 *       (item+0xc) e redesenha.
 *
 *  RELACAO COM O secao 17
 *    O DESBLOQUEADOR nao tem mecanismo proprio: ligar abre um dialogo
 *    (INFERIDO: o de presets do secao 17) e desligar desfaz o estado do secao 17
 *    (controle +0x4fc = 0, chave removida, rotulos ocultos).  Nada aqui
 *    escreve no processo do Point Blank.
 */

/* FUN_13728c00 @ 0x13728c00 */
static void fps_desbloqueio_limpar_estado(void)
{
    DelphiStr chave = NULL;

    FUN_132db2e0(*(int *)(*(int *)PTR_DAT_1381110c + 0x4fc), 0);

    decodificar_string(*(void **)PTR_DAT_13811378, (void *)0x13728cac, 0x46,
                       /*chaves*/ &chave);
    settings_remover(*(void **)PTR_DAT_13811bac, chave);      /* FUN_1369ae6c */

    FUN_132abec4(*(int *)(DAT_13819fac + 0x560), 0);
}

/* FUN_13728cc4 @ 0x13728cc4 */
static void fps_desbloqueio_ocultar_rotulos(void)
{
    FUN_132abec4(*(int *)(DAT_13819fac + 0x554), 0);
    FUN_132abec4(*(int *)(DAT_13819fac + 0x558), 0);
    FUN_132abec4(*(int *)(DAT_13819fac + 0x560), 0);
    FUN_132abec4(*(int *)(DAT_13819fac + 0x514), 0);
    FUN_132abec4(*(int *)(DAT_13819fac + 0x55c), 0);
}

/* FPSUNLOCKED_OFFClick @ 0x137294d8  -- liga */
void pb_desbloqueador_fps_ativar(int form)
{
    if (*(int *)PTR_DAT_13810cd8 == 0 && *(int *)PTR_DAT_13811928 == 0) {
        FUN_135fcd18();                     /* aviso cifrado DAT_135fcdc0 */
        return;
    }

    FUN_132abec4(*(int *)(form + 0x478), 0);    /* oculta FPSUNLOCKED_OFF */
    FUN_132abec4(*(int *)(form + 0x474), 1);    /* exibe  FPSUNLOCKED_ON  */

    /* INFERIDO: abre o dialogo de limite de FPS (classe nao identificada). */
    (**(void (**)(void))(**(int **)PTR_DAT_13811880 + 0x1cc))();
}

/* FPSUNLOCKED_ONClick @ 0x13729524  -- desliga */
void pb_desbloqueador_fps_restaurar(int form)
{
    int item;

    FUN_132abec4(*(int *)(form + 0x474), 0);    /* oculta FPSUNLOCKED_ON  */
    FUN_132abec4(*(int *)(form + 0x478), 1);    /* exibe  FPSUNLOCKED_OFF */

    fps_desbloqueio_limpar_estado();            /* FUN_13728c00 */
    fps_desbloqueio_ocultar_rotulos();          /* FUN_13728cc4 */

    item = FUN_135750f8(*(int *)(FUN_13575414(
               *(int *)(*(int *)(form + 0x5f8) + 0x310), 0) + 0x24), 0);
    FUN_13574f7c(item, L"DESBLOQUEIO DE FPS");  /* @ 0x13729580 */
}

/* Auxiliares desta secao (assinaturas conferidas no decompilado).
 *
 * decodificar_string = FUN_134a8d98: (ctx, blob, tamanho, chaves..., &saida).
 *   EAX = *PTR_DAT_13811378, EDX = blob cifrado, ECX = tamanho; na pilha,
 *   na ordem de push: chave2, chave1, &saida.  Nao exibe nada: so devolve a
 *   string decodificada.  Por isso os textos destas telas nao aparecem em
 *   claro no binario.
 *
 * settings_gravar = FUN_1369b158 (store, chave, valor) e
 * settings_remover = FUN_1369ae6c (store, chave): ambos carregam o documento
 *   JSON guardado em PTR_DAT_13811610 (classe em PTR_LAB_133d2d08), procuram
 *   a chave (FUN_133e28dc), removem (FUN_133dd464) e/ou adicionam o par
 *   (FUN_133dd2e0) e serializam de volta.  store = *PTR_DAT_13811bac.
 *   INFERIDO: e o arquivo de configuracoes do ReetFPS; a gravacao em disco
 *   nao foi rastreada.
 */
extern void decodificar_string(void *ctx, const void *blob, int tamanho, ...);
extern void settings_gravar(void *store, DelphiStr chave, DelphiStr valor);
extern void settings_remover(void *store, DelphiStr chave);
extern void FUN_132db2e0(int controle, int valor);
extern int  FUN_13575414(int lista, int indice);
extern int  FUN_135750f8(int lista, int indice);
extern void FUN_13574f7c(int item, const wchar_t *texto);
extern void FUN_135fcd18(void);
extern int *PTR_DAT_13811880;   /* form aberto pelo botao FPSUNLOCKED_OFF   */
extern int *PTR_DAT_13811378;   /* contexto do decodificador de strings    */
extern int *PTR_DAT_13811bac;   /* store de configuracoes (JSON)           */
extern int  DAT_13819fac;       /* form com os rotulos de FPS (+0x514...)  */


/* ===========================================================================
 *  21) IMPULSIONAR POINTBLANK  (tela "FPS Game Booster" / TGameBooster)
 * ===========================================================================
 *
 *  A TELA
 *    TGameBooster (unidade UGameBooster) e o formulario onde ficam os botoes
 *    liga/desliga de varios recursos do PB.  Strings conferidas:
 *      " FPS Game Booster"                         @ 0x136ea1e8
 *      "Ative a configuracao recomendada para priorizar fluidez, desempenho
 *       e estabilidade no PointBlank."             @ 0x136ea218 (UTF-16LE)
 *      "BOOST ATIVO"                               @ 0x1356f25c
 *      "INICIAR POINTBLANK" / "INICIAR JOGO"       @ 0x136f47fc / 0x136f4830
 *      "Launcher encontrado. Abra o PointBlank e acesse rapidamente o FPS
 *       Game Booster."                             @ 0x136f4858
 *      "PB LOCALIZADO"                             @ 0x136f4904
 *      "Validando FPS Game Booster"                @ 0x136e9b94
 *      "Conferindo ajustes pendentes"              @ 0x136e9bd8
 *      "FirstAccessPointBlankBoosterApplied"       @ 0x136ea420,
 *                                                    0x136f16dc, 0x136f1bd0
 *    Nenhuma dessas strings e referenciada pelas funcoes abaixo; nao ha
 *    string "IMPULSIONAR" no binario.  Os nomes "TGameBooster.FormCreate",
 *    "...FormShow" e "...StartPointBlankFromAssistant" (@ 0x13733533,
 *    0x137338f7, 0x137320c6) sao registros de metodo anonimo ($ActRec), nao
 *    entradas de RTTI de metodo.
 *
 *  QUAL BOTAO E O "IMPULSIONAR"
 *    INFERIDO: o par PRIORITYPB_ON / PRIORITYPB_OFF (campos +0x56c / +0x570,
 *    painel PANEL_PRIORITYPB +0x564).  "PRIORITYPB" tambem e a chave do item
 *    de prioridade na lista de recomendacoes (@ 0x136ed0c4).  Nenhuma string liga
 *    "IMPULSIONAR POINTBLANK" a este botao.
 *      PRIORITYPB_OFFClick @ 0x1372e888   (liga)
 *      PRIORITYPB_ONClick  @ 0x1372ebb0   (desliga)
 *
 *  LIGAR  (PRIORITYPB_OFFClick @ 0x1372e888)
 *    1. Oculta PRIORITYPB_OFF (+0x570) e exibe PRIORITYPB_ON (+0x56c).
 *    2. _DAT_138103f8 = -1  (flag global ligada).
 *       INFERIDO: e a flag que libera a elevacao de prioridade do processo
 *       do PB (secao 3-secao 6); o leitor da flag nao foi localizado.
 *    3. Grava um par chave/valor cifrado no store de configuracoes
 *       (chave DAT_1372ead8, valor DAT_1372eac0; settings_gravar).
 *    4. Se param_2 != 0: monta titulo/corpo cifrados (DAT_1372eb0c,
 *       DAT_1372eb58, DAT_1372eba4) e mostra o card via FUN_1358027c.
 *
 *  DESLIGAR  (PRIORITYPB_ONClick @ 0x1372ebb0)
 *    Exibe PRIORITYPB_OFF, oculta PRIORITYPB_ON, _DAT_138103f8 = 0 e remove
 *    a chave cifrada DAT_1372ec5c do store.
 *
 *  CORRECAO DE VERSOES ANTERIORES
 *    As funcoes antes documentadas aqui e no secao 20 sao outros botoes do
 *    mesmo form:
 *      0x13728234 = COUNTERPING_OFFClick, 0x13728578 = COUNTERPING_ONClick
 *        (contador de ping; botoes +0x580 / +0x57c);
 *      0x137295a8 = REETSTATS_OFFClick,   0x1372987c = REETSTATS_ONClick
 *        (botoes +0x484 / +0x480).
 *    Esses quatro handlers chamam o metodo virtual +0x188 de controles em
 *    *PTR_DAT_1381110c (+0x500 e +0x508) com 1/0.  INFERIDO: e o setter
 *    Checked de um controle do overlay, porque FUN_13696e3c sincroniza do
 *    mesmo jeito o controle +0x550 e logo depois chama FUN_13687120, que e
 *    o SetChecked animado de um checkbox (estado em +0x2e4, timer de 16 ms).
 *    Nenhum deles remove "cap de FPS" nem ativa "booster" no motor do jogo.
 *    0x13727a00 e BUTTON_APPLY_CROSSClick (abre o form em PTR_DAT_13810ae8
 *    pelo slot +0x1cc e ajusta a selecao da lista em +0x608 com
 *    FUN_1372f160); nao inicia o PB.  O handler real de iniciar o jogo e
 *    BUTTON_STARTPBClick @ 0x13730064 (nao reconstruido).
 *
 *  FUN_13727a5c (sem nome na tabela de metodos)
 *    Decodifica 15 chaves cifradas (DAT_13727f88..DAT_13728178) e remove cada
 *    uma do store (FUN_1369ae6c), atribui valores fixos a 20 globais
 *    (ex.: PTR_DAT_138116e4 = 3, PTR_DAT_138117c4 = 7, PTR_DAT_1381147c = 1)
 *    e redesenha o painel +0x608 do form em DAT_13819fac.  E um reset de
 *    configuracoes; nao localiza o launcher e nao exibe as strings de
 *    progresso em claro.  INFERIDO: e o "aplicar configuracao recomendada"
 *    do primeiro acesso (FirstAccessPointBlankBoosterApplied); quem chama
 *    nao foi localizado.
 */

/* PRIORITYPB_OFFClick @ 0x1372e888  -- liga */
void pb_impulsionar_pb_ativar(int form, int mostrar_card)
{
    DelphiStr valor = NULL, chave = NULL;
    DelphiStr titulo = NULL, corpo = NULL;

    FUN_132abec4(*(int *)(form + 0x570), 0);    /* oculta PRIORITYPB_OFF */
    FUN_132abec4(*(int *)(form + 0x56c), 1);    /* exibe  PRIORITYPB_ON  */

    _DAT_138103f8 = -1;

    /* chaves de decodificacao omitidas: o decompilado embaralha os pushes */
    decodificar_string(*(void **)PTR_DAT_13811378, (void *)0x1372eac0, 0x16,
                       /*chaves*/ &valor);
    decodificar_string(*(void **)PTR_DAT_13811378, (void *)0x1372ead8, 0x99,
                       /*chaves*/ &chave);
    settings_gravar(*(void **)PTR_DAT_13811bac, chave, valor);  /* FUN_1369b158 */

    if (mostrar_card) {
        /* titulo, corpo e icone tambem sao strings cifradas
         * (LAB_1372eaf4, DAT_1372eb0c, DAT_1372eb58, DAT_1372eba4).       */
        FUN_1358027c(titulo, corpo, 0x1194, 5, 0xe, 0xc, 0xa0, 0x17c, 0xf5,
                     /*icone*/ NULL, -1, -1, -1, 1, 1, 1);
    }
}

/* PRIORITYPB_ONClick @ 0x1372ebb0  -- desliga */
void pb_impulsionar_pb_restaurar(int form)
{
    DelphiStr chave = NULL;

    FUN_132abec4(*(int *)(form + 0x570), 1);    /* exibe  PRIORITYPB_OFF */
    FUN_132abec4(*(int *)(form + 0x56c), 0);    /* oculta PRIORITYPB_ON  */

    _DAT_138103f8 = 0;

    decodificar_string(*(void **)PTR_DAT_13811378, (void *)0x1372ec5c, 0x36,
                       /*chaves*/ &chave);
    settings_remover(*(void **)PTR_DAT_13811bac, chave);       /* FUN_1369ae6c */
}

/* FUN_13727a5c @ 0x13727a5c  -- reset das configuracoes da tela */
void pb_game_booster_resetar_config(void)
{
    /* 15x: decodificar_string(DAT_13727f88 .. DAT_13728178) + settings_remover */

    *(int *)PTR_DAT_138116e4 = 3;  *(int *)PTR_DAT_138113d4 = 0;
    *(int *)PTR_DAT_13811170 = 0;  *(int *)PTR_DAT_13810ac4 = 3;
    *(int *)PTR_DAT_138117c4 = 7;  *(int *)PTR_DAT_1381147c = 1;
    *(int *)PTR_DAT_13811244 = 0;  *(int *)PTR_DAT_13810b8c = 3;
    *(int *)PTR_DAT_138110a0 = 5;  *(int *)PTR_DAT_13810cf4 = 3;
    *(int *)PTR_DAT_13810aa8 = 0;  *(int *)PTR_DAT_138119c8 = 3;
    *(int *)PTR_DAT_13810aec = 7;  *(int *)PTR_DAT_13811cf8 = 3;
    *(int *)PTR_DAT_13811ad8 = 0;  *(int *)PTR_DAT_138114bc = 3;
    *(int *)PTR_DAT_13811464 = 3;  *(int *)PTR_DAT_1381112c = 2;
    *(int *)PTR_DAT_13810ea4 = 0;  *(int *)PTR_DAT_13811d8c = 3;

    FUN_132ac618(DAT_13819fac);                 /* vtable +0xe4 do form   */
    (**(void (**)(void))(**(int **)(DAT_13819fac + 0x608) + 0xe0))();
}

/* Auxiliares desta secao.
 *
 * FUN_1358027c @ 0x1358027c -- card de notificacao, 16 parametros:
 *   EAX = titulo, EDX = corpo, ECX = duracao em ms (0x1194 = 4500); na pilha
 *   p4..p8 = 5, 0xe, 0xc, 0xa0, 0x17c; p9 (byte) = 0xf5; p10 = icone
 *   (string, 0 = sem imagem); p11..p13 = int, -1 = padrao; p14..p16 = bytes.
 *   Conferido pela desmontagem de 0x13729680..0x13729705.
 */
extern void FUN_1358027c(DelphiStr titulo, DelphiStr corpo, int duracao_ms,
                         int p4, int p5, int p6, int p7, int p8,
                         uint8_t p9, const wchar_t *icone,
                         int p11, int p12, int p13,
                         uint8_t p14, uint8_t p15, uint8_t p16);
extern void FUN_132abec4(int controle, int visivel);  /* TControl.SetVisible */
extern void FUN_132ac618(int form);
extern int  _DAT_138103f8;      /* flag do PRIORITYPB */
/* PTR_DAT_1381110c, PTR_DAT_13810cd8, PTR_DAT_13811928, PTR_DAT_138116e4 etc.
 * sao globais do binario (ponteiros para variaveis Delphi).                 */


/* ===========================================================================
 *  22) FULL SCREEN
 * ===========================================================================
 *
 *  Item de perfil "FULLSCREEN" da lista de recomendacoes do PointBlank.
 *
 *  STRINGS
 *    "FULLSCREEN"            @ 0x136ece9c  (chave na lista de recomendacoes)
 *    "Tela cheia otimizada"  @ 0x136ecfb4  (rotulo do item)
 *    "fullscreen"            @ 0x136ecf90  (nome do icone do item)
 *    "FULLSCREEN"            @ 0x136f7b2c  (literal comparado pelo despachante)
 *
 *  DESPACHANTE DE ITENS DO POINTBLANK  (codigo em 0x136f78d0, sem funcao
 *  criada no Ghidra)
 *    Compara o nome do item (FUN_1316c388) com cada literal e chama o
 *    metodo correspondente do formulario principal (*PTR_DAT_13810970)
 *    atraves de FUN_136f782c.  Se o formulario nao existe, levanta
 *    "Handler da otimizacao do PointBlank nao atribuido."; se o nome nao
 *    casa com nenhum literal, levanta uma excecao com a mensagem em
 *    0x136f7c4c.
 *
 *      "FLUIDEZMAX" / "FLUIDEZMAXIMA"  -> 0x1372a03c
 *      "FULLSCREEN"                    -> 0x1372dfb0   (esta secao)
 *      "OPTIMIZER_PB_MANAGER"          -> 0x13729094
 *      "FPS_SELECTION_INDEX"           -> 0x137294d8
 *      "PRIORITYPB"                    -> 0x1372e888
 *      "LOADINGMAP"                    -> 0x13728d20
 *      "INTERFACE"                     -> 0x1372d4b8
 *      "REETGAMEMODE"                  -> 0x1372c00c
 *
 *  HANDLER: FUN_1372dfb0 @ 0x1372dfb0  (form, mostrar_card)
 *    1. FUN_13727540(form, 1, 3): seleciona linha 1, coluna 3 da grade
 *       de opcoes do formulario (form+0x5f8).
 *    2. FUN_1372e7bc(form, 0): desliga a opcao concorrente -- apaga a sua
 *       chave de configuracao (FUN_1369ae6c), troca os botoes
 *       form+0x4f0 (exibe) / form+0x4ec (oculta) e chama
 *       vtable[0x188](*(mgr+0x514), 0).
 *    3. Se o jogo nao esta aberto (PTR_DAT_13810cd8 e PTR_DAT_13811928
 *       zerados): FUN_135fcd18() e sai.
 *    4. Senao: oculta form+0x4a0, exibe form+0x49c,
 *       vtable[0x188](*(mgr+0x49c), 1), grava a configuracao via
 *       FUN_1369b158(*PTR_DAT_13811bac, chave, valor) e, se mostrar_card,
 *       exibe o card com FUN_1358027c(..., 0x1194).
 *    mgr = *PTR_DAT_1381110c.  Chave, valor e textos do card sao strings
 *    ofuscadas, decodificadas em tempo de execucao por FUN_134a8d98
 *    (blobs DAT_1372e228, DAT_1372e240, DAT_1372e25c, DAT_1372e274,
 *    DAT_1372e2a8, DAT_1372e2f8); por isso nao estao citadas aqui.
 *
 *  O QUE NAO FOI COMPROVADO
 *    Nenhum trecho do handler toca o TRPPBConfig (§19) nem a chave
 *    [Graphics] ScreenMode (+0x4f8) do env_settings.ini.  Se a tela cheia
 *    e aplicada gravando ScreenMode, isso acontece em outro ponto (por
 *    exemplo ao iniciar o jogo) que nao foi localizado.  O significado de
 *    vtable[0x188] nos objetos mgr+0x49c / mgr+0x514 tambem nao foi
 *    identificado (pode ser apenas um setter de controle da UI).
 */

extern void form_grade_selecionar(void *form, int linha, int coluna); /* FUN_13727540 */
extern void form_desligar_opcao_concorrente(void *form, int card);    /* FUN_1372e7bc */
extern void jogo_nao_encontrado(void);                                /* FUN_135fcd18 */
extern void ui_set_visivel(void *controle, int visivel);              /* FUN_132abec4 */
extern void config_gravar(void *settings, DelphiStr chave, DelphiStr valor); /* FUN_1369b158 */
extern int *PTR_DAT_1381110c;   /* mgr */
extern int *PTR_DAT_13810cd8;   /* jogo em execucao (1a verificacao) */
extern int *PTR_DAT_13811928;   /* jogo em execucao (2a verificacao) */
extern int *PTR_DAT_13811bac;   /* objeto de configuracoes do ReetFPS */

/*
 * pb_fullscreen_ativar  --  FUN_1372dfb0 @ 0x1372dfb0
 */
void pb_fullscreen_ativar(uint8_t *form, int mostrar_card)
{
    form_grade_selecionar(form, 1, 3);
    form_desligar_opcao_concorrente(form, 0);

    if (*PTR_DAT_13810cd8 == 0 && *PTR_DAT_13811928 == 0) {
        jogo_nao_encontrado();
        return;
    }

    ui_set_visivel(*(void **)(form + 0x4a0), 0);
    ui_set_visivel(*(void **)(form + 0x49c), 1);

    {
        int *obj = *(int **)(*PTR_DAT_1381110c + 0x49c);
        ((void (*)(int *, int))(*(int **)obj)[0x188 / 4])(obj, 1);
    }

    /* chave/valor ofuscados (DAT_1372e228 / DAT_1372e240) */
    config_gravar((void *)*PTR_DAT_13811bac, /*chave*/ NULL, /*valor*/ NULL);

    if (mostrar_card) {
        /* titulo e corpo ofuscados (DAT_1372e25c .. DAT_1372e2f8) */
        FUN_1358027c(/*titulo*/ NULL, /*corpo*/ NULL, 0x1194);
    }
}

/* Nao ha pb_fullscreen_restaurar(): o caminho de desligar FULLSCREEN nao
 * foi localizado.  O padrao de FUN_1372e7bc (desligar opcao, apagar chave,
 * vtable[0x188](..., 0)) sugere uma funcao irma, mas ela nao foi
 * identificada.                                                             */


/* ===========================================================================
 *  23) OTIMIZACAO GPU
 * ===========================================================================
 *
 *  Painel "OTIMIZACOES DA GPU" (formulario TGPU_Utils).  O que esta comprovado
 *  no binario sao duas tabelas de comandos -- uma de ATIVAR e uma espelho de
 *  RESTAURAR -- executadas pelo despachante generico de comandos, mais a
 *  sub-ferramenta NVIDIABOOST, que importa um perfil pronto no driver NVIDIA
 *  usando o NVIDIA Profile Inspector.
 *
 *  STRINGS DE UI
 *    "OTIMIZACOES DA GPU"                                   @ 0x13781aa0
 *      (UTF-16, com cedilha/til; nao e "AJUSTES DA GPU")
 *    "Restaurando ajustes da GPU"                           @ 0x136c57f8
 *    "Os ajustes da GPU desta tela foram desligados e os
 *     registros do ReetFPS apagados."                       @ 0x136c56bc
 *
 *  CLASSES / RTTI
 *    TGPU_Utils -- so os nomes dos registros de metodos anonimos foram
 *    localizados (nao o typeinfo da classe):
 *      "TGPU_Utils.NVIDIABOOST_OFFClick$ActRec"            @ 0x136c1ad6
 *      "TGPU_Utils.CheckDriverAndChipset$ActRec"           @ 0x136c37f2
 *      "TGPU_Utils.DriverRowClick$ActRec"                  @ 0x136c4a7e
 *      "TGPU_Utils.RunDriverInstaller$ActRec"              @ 0x136c4de2
 *      "TGPU_Utils.RPSidePanel1RowClick$ActRec"            @ 0x136c5286
 *    TGPURegistryWorker                                    @ 0x13734cd1
 *      Consulta PDH enquanto o painel esta aberto:
 *        "\GPU Engine(*)\Utilization Percentage"           @ 0x137350f0
 *        "\GPU Adapter Memory(*)\Dedicated Limit"          @ 0x13735180
 *
 *  DESPACHANTE DE COMANDOS: FUN_135d1fb8 @ 0x135d1fb8
 *    void FUN_135d1fb8(const wchar_t **cmds, int high)
 *      -> FUN_135d1fdc(&PTR_FUN_135d1ec4, 1, cmds, high)
 *    Recebe um open array Delphi: ponteiro para o array e o INDICE MAXIMO
 *    (contagem - 1) em EDX.  Ex.: EDX=9 => 10 comandos.
 *
 *  TABELA ATIVAR -- codigo @ 0x135f7f88 (ADD ESP,-0x28; EDX=9; CALL 0x135d1fb8)
 *    0x135f8000  Tasks\Games            "GPU Priority"         = 8
 *    0x135f8138  Tasks\Games            "Priority"             = 6
 *    0x135f8268  SystemProfile          "SystemResponsiveness" = 0
 *    0x135f8398  PolicyManager\...\ApplicationManagement\AllowGameDVR "value" = 0
 *    0x135f84b8  HKCU\System\GameConfigStore "GameDVR_Enabled" = 0
 *    0x135f8580  HKCU\...\CurrentVersion\GameDVR "AppCaptureEnabled"   = 0
 *    0x135f8680  HKCU\...\CurrentVersion\GameDVR "AudioCaptureEnabled" = 0
 *    0x135f8784  Tcpip\Parameters       "TcpAckFrequency"      = 1
 *    0x135f8884  Tcpip\Parameters       "TCPNoDelay"           = 1
 *    0x135f897c  Control\GraphicsDrivers "HwSchMode"           = 2   (HAGS)
 *    Obs.: TcpAckFrequency/TCPNoDelay sao ajustes de REDE (Nagle/ACK
 *    atrasado), mas estao de fato nesta tabela do painel de GPU.
 *
 *  TABELA RESTAURAR -- codigo @ 0x135f8a60 (ADD ESP,-0x28; EDX=9; CALL 0x135d1fb8)
 *    0x135f8ad8  reg delete Tasks\Games "GPU Priority"
 *    0x135f8bf4  reg delete Tasks\Games "Priority"
 *    0x135f8d08  SystemProfile "SystemResponsiveness" = 20  (padrao do Windows)
 *    0x135f8e3c  AllowGameDVR "value"           = 1
 *    0x135f8f5c  GameConfigStore "GameDVR_Enabled" = 1
 *    0x135f9024  GameDVR "AppCaptureEnabled"    = 1
 *    0x135f9124  GameDVR "AudioCaptureEnabled"  = 1
 *    0x135f9228  reg delete Tcpip "TcpAckFrequency"
 *    0x135f930c  reg delete Tcpip "TCPNoDelay"
 *    0x135f93e4  reg delete GraphicsDrivers "HwSchMode"
 *
 *  NAO FAZEM PARTE DESTE PAINEL (correcao de versao anterior)
 *    - 0x135f94ac (EDX=7, 8 cmds a partir de 0x135f9514) e a restauracao de
 *      OUTRO conjunto: Win32PrioritySeparation=18, pagefile automatico (wmic
 *      @ 0x135f9608), GlobalUserDisabled=0 (0x135f96b8),
 *      BackgroundAppGlobalToggle=1 (0x135f97c8), BackgroundTaskHost /
 *      BackgroundTransferHost / cloudexperiencehost / LockApp (AppInfo).
 *    - "Win32PrioritySeparation"=38 @ 0x135d70a0 pertence ao bloco "Reg.exe
 *      add" da regiao 0x135d5xxx-0x135d7xxx (mesmo estilo das tarefas MMCSS
 *      da secao 15).  Despachante que o empilha nao identificado (sem xrefs).
 *    - Os 6 valores de latencia D3 abaixo tambem estao nessa regiao; nao ha
 *      evidencia de que o painel de GPU os execute.
 *        0x135d880c  DefaultD3TransitionLatencyActivelyUsed   = 1
 *        0x135d8934  DefaultD3TransitionLatencyIdleLongTime   = 1
 *        0x135d8a5c  DefaultD3TransitionLatencyIdleMonitorOff = 1
 *        0x135d8b88  DefaultD3TransitionLatencyIdleNoContext  = 1
 *        0x135d8cb0  DefaultD3TransitionLatencyIdleShortTime  = 1
 *        0x135d8dd8  DefaultD3TransitionLatencyIdleVeryLongTime = 1
 *      (todos em HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers\Power)
 *    - gpu_directx / gpu_nvidia / gpu_amd / gpu_intel (0x136697f4,
 *      0x13669bbc, 0x13669ea0, 0x1366a098) sao itens da LISTA DE LIMPEZA de
 *      caches de shader (mesma regiao da tabela da secao 11), nao ajustes de GPU.
 *      Cada registro = aviso, caminhos, rotulo, chave.  Ex.: gpu_directx ->
 *      "Shaders do DirectX": ...\AppData\Local\D3DSCache e
 *      ...\Microsoft\DirectX Shader Cache; gpu_nvidia -> NVIDIA\DXCache,
 *      GLCache, VkCache; gpu_amd -> AMD\DxCache, DxcCache, GLCache, VkCache;
 *      gpu_intel -> Intel\ShaderCache, GfxCache.
 *
 *  INFERIDO: os botoes LIGAR/DESLIGAR do painel chamam 0x135f7f88 /
 *  0x135f8a60.  Os dois blocos nao estao definidos como funcoes no Ghidra e
 *  nao tem xrefs; a ligacao vem do conteudo (espelho exato um do outro) e da
 *  mensagem de restauracao do proprio painel.
 */

/* Despachante generico (open array Delphi: array + indice maximo).          */
extern void FUN_135d1fb8(const wchar_t **cmds, int high);

/* Tabela ATIVAR -- ordem e texto exatos do bloco @ 0x135f7f88.              */
static const wchar_t *gpu_cmds_ativar[] = {
    /* 0x135f8000 */
    L"reg add \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\" /v \"GPU Priority\" /t REG_DWORD /d 8 /f",
    /* 0x135f8138 */
    L"reg add \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\" /v \"Priority\" /t REG_DWORD /d 6 /f",
    /* 0x135f8268 */
    L"reg add \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\" /v \"SystemResponsiveness\" /t REG_DWORD /d 0 /f",
    /* 0x135f8398 */
    L"reg add \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\PolicyManager\\default"
    L"\\ApplicationManagement\\AllowGameDVR\" /v \"value\" /t REG_DWORD /d 0 /f",
    /* 0x135f84b8 */
    L"reg add \"HKEY_CURRENT_USER\\System\\GameConfigStore\""
    L" /v \"GameDVR_Enabled\" /t REG_DWORD /d 0 /f",
    /* 0x135f8580 */
    L"reg add \"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR\""
    L" /v \"AppCaptureEnabled\" /t REG_DWORD /d 0 /f",
    /* 0x135f8680 */
    L"reg add \"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR\""
    L" /v \"AudioCaptureEnabled\" /t REG_DWORD /d 0 /f",
    /* 0x135f8784 -- ajuste de rede, mas esta nesta tabela */
    L"reg add \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\""
    L" /v \"TcpAckFrequency\" /t REG_DWORD /d 1 /f",
    /* 0x135f8884 -- ajuste de rede, mas esta nesta tabela */
    L"reg add \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\""
    L" /v \"TCPNoDelay\" /t REG_DWORD /d 1 /f",
    /* 0x135f897c -- HAGS */
    L"reg add \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\""
    L" /v \"HwSchMode\" /t REG_DWORD /d 2 /f",
};

/* Tabela RESTAURAR -- espelho exato, bloco @ 0x135f8a60.                    */
static const wchar_t *gpu_cmds_restaurar[] = {
    /* 0x135f8ad8 */
    L"reg delete \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\" /v \"GPU Priority\" /f",
    /* 0x135f8bf4 */
    L"reg delete \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\\Tasks\\Games\" /v \"Priority\" /f",
    /* 0x135f8d08 */
    L"reg add \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
    L"\\Multimedia\\SystemProfile\" /v \"SystemResponsiveness\" /t REG_DWORD /d 20 /f",
    /* 0x135f8e3c */
    L"reg add \"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\PolicyManager\\default"
    L"\\ApplicationManagement\\AllowGameDVR\" /v \"value\" /t REG_DWORD /d 1 /f",
    /* 0x135f8f5c */
    L"reg add \"HKEY_CURRENT_USER\\System\\GameConfigStore\""
    L" /v \"GameDVR_Enabled\" /t REG_DWORD /d 1 /f",
    /* 0x135f9024 */
    L"reg add \"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR\""
    L" /v \"AppCaptureEnabled\" /t REG_DWORD /d 1 /f",
    /* 0x135f9124 */
    L"reg add \"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR\""
    L" /v \"AudioCaptureEnabled\" /t REG_DWORD /d 1 /f",
    /* 0x135f9228 */
    L"reg delete \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\""
    L" /v \"TcpAckFrequency\" /f",
    /* 0x135f930c */
    L"reg delete \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\""
    L" /v \"TCPNoDelay\" /f",
    /* 0x135f93e4 */
    L"reg delete \"HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers\""
    L" /v \"HwSchMode\" /f",
};

#define GPU_QTD(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Bloco @ 0x135f7f88: monta o array na pilha e chama o despachante com
 * EDX = 9 (indice maximo => 10 comandos).                                   */
void gpu_otimizacao_ativar(void)
{
    FUN_135d1fb8(gpu_cmds_ativar, GPU_QTD(gpu_cmds_ativar) - 1);
}

/* Bloco @ 0x135f8a60: mesmo formato, EDX = 9.                               */
void gpu_otimizacao_restaurar(void)
{
    FUN_135d1fb8(gpu_cmds_restaurar, GPU_QTD(gpu_cmds_restaurar) - 1);
}

/*
 * gpu_nvidiaboost_aplicar  --  TGPU_Utils.NVIDIABOOST_OFFClick
 *   (registro do metodo anonimo: "TGPU_Utils.NVIDIABOOST_OFFClick$ActRec"
 *    @ 0x136c1ad6; chave "NVIDIABOOST" @ 0x136c1e50 / 0x136c292c)
 *
 * NAO liga opcoes individuais do driver: importa um perfil pronto com o
 * NVIDIA Profile Inspector.  Strings que comprovam o mecanismo:
 *   "nvidiaProfileInspector.zip"                            @ 0x136bf0b8
 *   "nvidiaProfileInspector.exe"                            @ 0x136bf0fc
 *   "ReetFPS.nip"                                           @ 0x136bf140
 *   "...o encontrado dentro do ZIP: nvidiaProfileInspector.exe" @ 0x136bf178
 *   "...o encontrado dentro do ZIP: ReetFPS.nip"            @ 0x136bf208
 *   "-importProfile \""                                     @ 0x136beab0
 *   "Falha ao iniciar o import do perfil NVIDIA: "          @ 0x136beaf0
 * Mensagens de erro (UTF-16 com acentos):
 *   "Nao foi possivel aplicar o perfil NVIDIA.\r\n"         @ 0x136c1fa4
 *   "A otimizacao NVIDIA esta disponivel apenas para placas NVIDIA.\r\n"
 *   "Nenhum perfil NVIDIA foi importado."                   @ 0x136c21e4
 *
 * O metodo nao foi decompilado (regiao 0x136cxxxx sem funcoes definidas).
 * INFERIDO: a ordem dos passos abaixo; o conteudo de ReetFPS.nip (quais
 * opcoes do driver ele altera) nao esta visivel como texto no binario.
 */
void gpu_nvidiaboost_aplicar(void)
{
    /* 1. INFERIDO: confere se a placa e NVIDIA; senao mostra a mensagem
     *    @ 0x136c21e4 e sai.                                                */
    /* 2. Extrai nvidiaProfileInspector.exe e ReetFPS.nip do ZIP embutido;
     *    se faltar algum, mostra "...o encontrado dentro do ZIP: <nome>".   */
    /* 3. Executa: nvidiaProfileInspector.exe -importProfile "ReetFPS.nip".
     *    Falha ao iniciar -> "Falha ao iniciar o import do perfil NVIDIA: ".*/
    /* 4. Falha no import -> "Nao foi possivel aplicar o perfil NVIDIA."     */
}


/* ============================================================================
 *  FIM. Para o catalogo completo de comandos do otimizador ver:
 *      catalogo_comandos.md / catalogo_comandos.c
 *  Para o resumo de alto nivel do que o ReetFPS faz com o jogo ver:
 *      ponto_blank.md
 * ========================================================================== */
