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
    /* +0x34  */ bool       boost_foi_ativado;
    /* +0x36  */ bool       flag_0x36;           /* set quando GPU abaixo do esperado 2x */
    /* +0x3b  */ bool       boost_ativo;          /* estado final do priority boost */
    /* +0x3d  */ bool       d3dkmt_nao_ok;
    /* +0x3e  */ bool       d3dkmt_ok;

    /* +0x68  */ int        contagem_crashes_hoje;
    /* +0x6c  */ bool       boost_kernel_ativo;  /* resultado de GetProcessPriorityBoost */
    /* +0x78  */ bool       crashes_suprimidos;

    /* +0x7c  */ int        prioridade_d3dkmt_atual;
    /* +0x88  */ int        contador_erros_boost;

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

    /* +0x8fd */ bool       d3dkmt_disponivel;   /* ambos ponteiros D3DKMT != NULL */
    /* +0x900 */ int        contador_erros_d3dkmt;
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
 */
void pb_elevar_priority_boost(TPointBlankMantain *ctx, HANDLE hProcesso)
{
    /* Precisa da feature ativa (+0x8cc) e dos dois ponteiros de funcao. */
    if (!ctx->cpu_priority_habilitado
        || ctx->pfn_GetProcessPriorityBoost == NULL
        || ctx->pfn_SetProcessPriorityBoost == NULL)
        return;

    /* GetProcessPriorityBoost(hProcesso, &bDisableBoost)
     * bDisableBoost == TRUE  significa que o boost esta DESABILITADO.
     * bDisableBoost == FALSE significa que o boost esta HABILITADO (queremos isso). */
    BOOL bDisableBoost = 0;
    BOOL ok = ((BOOL(WINAPI*)(HANDLE, PBOOL))ctx->pfn_GetProcessPriorityBoost)(
                  hProcesso, &bDisableBoost);
    if (!ok) {
        DWORD err = GetLastError();
        log_erro_maintain(ctx, L"Maintain.GetProcessPriorityBoost", err);
        return;
    }

    /* Salva o estado do boost (TRUE = desabilitado, FALSE = habilitado). */
    ctx->boost_kernel_ativo = (bDisableBoost == FALSE); /* +0x6c */

    if (bDisableBoost == FALSE) {
        /* Boost ja esta habilitado -- nada a fazer. */
        return;
    }

    /* Boost esta desabilitado: habilita passando bDisable = FALSE. */
    ok = ((BOOL(WINAPI*)(HANDLE, BOOL))ctx->pfn_SetProcessPriorityBoost)(
             hProcesso, FALSE);
    if (!ok) {
        DWORD err = GetLastError();
        log_erro_maintain(ctx, L"Maintain.SetProcessPriorityBoost.Enable", err);
        return;
    }

    /* Confirma o resultado lendo de volta. */
    bDisableBoost = 0;
    ok = ((BOOL(WINAPI*)(HANDLE, PBOOL))ctx->pfn_GetProcessPriorityBoost)(
             hProcesso, &bDisableBoost);
    bool boost_agora_ativo = ok && (bDisableBoost == FALSE);

    ctx->boost_foi_ativado = true;                  /* +0x34 = 1 */
    ctx->boost_ativo       = boost_agora_ativo;     /* +0x3b     */
    ctx->contador_erros_boost++;                    /* +0x88     */
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
 *  O valor "2" no codigo corresponde a D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL
 *  (definido em d3dkmthk.h):
 *    0 = Idle, 1 = Below Normal, 2 = Normal, 3 = Above Normal, 4 = High, 5 = Realtime
 *  -- o ReetFPS tenta elevar para a classe 2 (acima da normal).
 *
 *  Strings de log internas:
 *    "D3DKMTSetProcessSchedulingPriorityClass.AboveNormal"
 */
void pb_elevar_prioridade_gpu(TPointBlankMantain *ctx,
                               HANDLE hProcesso,
                               void  *param_extra)
{
    /* Precisa de ambos os ponteiros D3DKMT. */
    if (ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass == NULL
        || ctx->pfn_D3DKMTSetProcessSchedulingPriorityClass == NULL) {
        ctx->d3dkmt_ok     = false; /* +0x3e = 0 */
        ctx->d3dkmt_nao_ok = false;
        return;
    }

    /* D3DKMT_SCHEDULINGPRIORITYCLASS target = Above Normal (2 no enum). */
    const int CLASSE_ALVO = 2;  /* D3DKMT_SCHEDULINGPRIORITYCLASS_ABOVE_NORMAL */
    int classe_atual = 0;

    /* Consulta a classe atual. */
    int hr = ((int(__stdcall*)(HANDLE, int*))
                  ctx->pfn_D3DKMTGetProcessSchedulingPriorityClass)(
                  hProcesso, &classe_atual);

    if (hr != 0) {
        /* Chamada falhou -- incrementa contador de erros (log so na 1a vez). */
        ctx->contador_erros_d3dkmt++;
        if (ctx->contador_erros_d3dkmt == 1)
            log_erro_maintain(ctx, L"D3DKMTSetProcessSchedulingPriorityClass.AboveNormal", hr);
    } else if (classe_atual < CLASSE_ALVO) {
        /* Precisa elevar. */
        hr = ((int(__stdcall*)(HANDLE, int))
                  ctx->pfn_D3DKMTSetProcessSchedulingPriorityClass)(
                  hProcesso, CLASSE_ALVO);

        if (hr != 0) {
            ctx->contador_erros_d3dkmt++;
            if (ctx->contador_erros_d3dkmt == 1)
                log_erro_maintain(ctx, L"D3DKMTSetProcessSchedulingPriorityClass.AboveNormal", hr);

            /* Marcando que a GPU esta abaixo do esperado 2 vezes: levanta flag. */
            if (ctx->contador_erros_d3dkmt > 2)
                ctx->flag_0x36 = true; /* +0x36 */
        }
    }

    /* Atualiza flags de estado. */
    ctx->d3dkmt_disponivel = (hr == 0);    /* +0x8fd */
    ctx->d3dkmt_nao_ok     = (hr != 0);    /* +0x3d  */
    ctx->d3dkmt_ok         = (hr == 0);    /* +0x3e  */
    ctx->prioridade_d3dkmt_atual = classe_atual; /* +0x7c */
}


/* ===========================================================================
 *  7. MMCSS -- PERFIL MULTIMEDIA "GAMES"
 * ===========================================================================
 *
 *  Evidencia no binario:
 *    Strings em 0x1357c32c: "AvSetMmThreadCharacteristicsW"
 *    Strings em 0x1357c34c: "AvSetMmThreadPriority"
 *    Strings em 0x1357c364: "AvRevertMmThreadCharacteristics"
 *    String  em 0x1357c584: "Games"  (nome da tarefa MMCSS)
 *    String  em 0x1357c5e0 (funcao): chama timeEndPeriod(1) ao reverter
 *
 *  O que o ReetFPS faz (reconstruido pelo padrao MMCSS + strings do binario):
 *
 *  MMCSS (Multimedia Class Scheduler Service) permite que um processo/thread
 *  peca ao Windows uma fatia maior de CPU usando um perfil de alta prioridade.
 *  O perfil "Games" e o mais indicado para jogos. Requer avrt.dll (Vista+).
 *
 *  Strings de log internas:
 *    "Maintain.GetProcessPriorityBoost"  (compartilhada com a secao 5)
 *
 *  OBSERVACAO: esta funcao nao foi decompilada diretamente (a funcao que
 *  carrega os ponteiros de avrt.dll nao foi localizada no Ghidra nesta
 *  sessao). A reconstrucao abaixo e baseada no padrao canonico de MMCSS +
 *  nas strings do binario e e fiel ao comportamento descrito em ponto_blank.md.
 */

/* Handle MMCSS global (guardado para poder reverter ao fechar o jogo). */
static HANDLE g_mmcss_task_handle = NULL;
static DWORD  g_mmcss_task_index  = 0;

typedef HANDLE (WINAPI *PFN_AvSetMmThreadCharacteristicsW)(LPCWSTR, LPDWORD);
typedef BOOL   (WINAPI *PFN_AvSetMmThreadPriority)(HANDLE, int);
typedef BOOL   (WINAPI *PFN_AvRevertMmThreadCharacteristics)(HANDLE);

/* AVRT_PRIORITY_HIGH = 1 (de avrt.h) */
#define AVRT_PRIORITY_HIGH 1

void pb_mmcss_configurar(void)
{
    HMODULE hAvrt = LoadLibraryW(L"avrt.dll");
    if (hAvrt == NULL) return;

    PFN_AvSetMmThreadCharacteristicsW pfnSet =
        (PFN_AvSetMmThreadCharacteristicsW)
        GetProcAddress(hAvrt, "AvSetMmThreadCharacteristicsW");
    PFN_AvSetMmThreadPriority pfnPri =
        (PFN_AvSetMmThreadPriority)
        GetProcAddress(hAvrt, "AvSetMmThreadPriority");

    if (pfnSet == NULL || pfnPri == NULL) return;

    /* Registra a thread/processo no perfil "Games" do MMCSS.
     * g_mmcss_task_index e preenchido pela API (indice interno). */
    g_mmcss_task_handle = pfnSet(L"Games", &g_mmcss_task_index);

    if (g_mmcss_task_handle != NULL) {
        /* Eleva a prioridade MMCSS para HIGH dentro do perfil "Games". */
        pfnPri(g_mmcss_task_handle, AVRT_PRIORITY_HIGH);
    }
}

/* Reverte o MMCSS quando o jogo fecha. Tambem chama timeEndPeriod(1)
 * para desfazer o ajuste de resolucao de timer (FUN_1357c5e0 @ 0x1357c5e0). */
void pb_mmcss_reverter(void)
{
    if (g_mmcss_task_handle == NULL) return;

    HMODULE hAvrt = GetModuleHandleW(L"avrt.dll");
    if (hAvrt != NULL) {
        PFN_AvRevertMmThreadCharacteristics pfnRev =
            (PFN_AvRevertMmThreadCharacteristics)
            GetProcAddress(hAvrt, "AvRevertMmThreadCharacteristics");
        if (pfnRev != NULL)
            pfnRev(g_mmcss_task_handle);
    }

    /* Desfaz a resolucao de timer de 1 ms (SetTimerResolution anterior). */
    timeEndPeriod(1);

    g_mmcss_task_handle = NULL;
    g_mmcss_task_index  = 0;
}


/* ===========================================================================
 *  8. VERIFICAR E EXIBIR CONTAGEM DE CRASHES DO DIA
 * ===========================================================================
 *
 *  Original: FUN_1359f8b0 @ 0x1359f8b0
 *
 *  Chamado pelo TPointBlankStabilityMonitor ao detectar que o jogo encerrou.
 *  Se a contagem de crashes hoje ultrapassar 2, exibe uma mensagem de aviso
 *  com botao "Veja como reparar" e gera uma chave de log diario.
 *
 *  Strings do binario:
 *    "%d encerramentos inesperados hoje. Veja como reparar."    (0x1359fa4c)
 *    "Detectamos %d encerramentos inesperados do PointBlank hoje. " (0x135a00e4)
 *    "pb-crash-YYYYMMDD..."  (chave do log diario, montada em runtime)
 *
 *  Condicoes para exibir o aviso:
 *    ctx->contagem_crashes_hoje > 2
 *    ctx->crashes_suprimidos == false  (+0x78)
 *    global DAT_138118c0 (flag "mostrar avisos") == false
 */
void pb_verificar_crashes(TPointBlankMantain *ctx)
{
    /* So exibe se: mais de 2 crashes hoje, nao suprimido, global ok. */
    if (ctx->contagem_crashes_hoje <= 2
        || ctx->crashes_suprimidos
        || g_suprimir_avisos_crash)
        return;

    /* Formata as duas mensagens de UI. */
    DelphiStr msg_botao = NULL;
    str_formatar(&msg_botao,
        L"%d encerramentos inesperados hoje. Veja como reparar.",
        ctx->contagem_crashes_hoje);

    DelphiStr msg_principal = NULL;
    str_formatar(&msg_principal,
        L"Detectamos %d encerramentos inesperados do PointBlank hoje. ",
        ctx->contagem_crashes_hoje);

    /* Monta a chave do log diario: "pb-crash-YYYYMMDD" + sufixo.
     * FUN_13171f58(L"yyyymmdd", ...) formata a data atual como string.
     * FUN_1314c828 concatena: "pb-crash-" + data_str. */
    DelphiStr data_str  = NULL;
    DelphiStr chave_log = NULL;
    obter_data_formatada(L"yyyymmdd", &data_str);         /* FUN_13171f58 */
    str_combinar_caminho(&chave_log, L"pb-crash-", data_str, /*separador*/NULL);

    /* FUN_134ae4cc(contexto_app, chave_log, 2) -- persiste/consulta o log
     * de crashes. Retorno indica se a contagem ja foi registrada hoje. */
    bool ja_logado = registrar_crash_no_log(g_contexto_app, chave_log, 2);

    /* Exibe a mensagem de aviso na UI (FUN_1316ec8c formata e agenda). */
    if (!ja_logado) {
        exibir_aviso_crash(msg_botao, msg_principal);
    }
}

extern bool  g_suprimir_avisos_crash;          /* DAT_138118c0 (offset +0xbc) */
extern void *g_contexto_app;                   /* PTR_DAT_13811668 */
extern void  str_formatar(DelphiStr *dst,
                          const wchar_t *fmt, ...); /* FUN_1316ec8c */
extern void  obter_data_formatada(const wchar_t *fmt,
                                  DelphiStr *dst);  /* FUN_13171f58 */
extern bool  registrar_crash_no_log(void *app,
                                    DelphiStr chave,
                                    int modo);      /* FUN_134ae4cc */
extern void  exibir_aviso_crash(DelphiStr msg_botao,
                                DelphiStr msg_principal); /* FUN_134ad1c8 area */


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
 *  9. PERFIL FLUIDEZMAX -- "CARREGAMENTO DE MAPA OTIMIZADO"
 * ===========================================================================
 *
 *  Enderecos-chave:
 *    0x13700af0  -- pb_ativar_fluidezmax() (entry point que ativa o perfil)
 *    0x137008d0  -- FUN_137008d0 = TReetGameModePanel.ExecuteGameModeActions
 *                  (coleta itens habilitados e despacha execucao em thread)
 *    0x136ecb00  -- tabela de dados do perfil FLUIDEZMAX (strings UTF-16LE)
 *    0x136ece50  -- string "Carregamento de mapa otimizado" (descricao do perfil)
 *    0x136ecc5c  -- string "FLUIDEZMAX" (chave do perfil)
 *
 *  O FLUIDEZMAX nao e uma unica funcao de "mapas instantaneos" -- e um PERFIL
 *  (preset) que ativa sete itens de otimizacao em conjunto. A sensacao de
 *  carregamento instantaneo e o efeito combinado de todos eles eliminando
 *  competicao por recursos durante o carregamento de mapa.
 *
 *  -----------------------------------------------------------------------
 *  ITENS DO PERFIL (tabela em 0x136ecb00, formato UTF-16LE triplas: chave,
 *  icone/nome-curto, descricao; cabecalho de entrada: b0 04 02 00 ff ff ff ff)
 *  -----------------------------------------------------------------------
 *
 *  #  Chave                  Icone/label     O que faz
 *  -- --------------------  --------------  ---------------------------------
 *  0  FLUIDEZMAX             -               CABECALHO do perfil; user-facing:
 *                                            "Carregamento de mapa otimizado"
 *  1  fullscreen             FULLSCREEN      Forca PB em fullscreen exclusivo.
 *                                            DWM e desativado -> menos pressao
 *                                            de GPU/VRAM durante loading.
 *  2  OTIMIZER_PB_MANAGER    -               Ativa o TPointBlankStabilityMonitor
 *                                            (o gerenciador automatico do jogo).
 *  3  smart                  Gestao          Liga modo "Gestao Inteligente": o
 *                            Inteligente     monitor aplica/reverte tweaks em
 *                                            sincronia com o ciclo de vida do PB.
 *  4  PRIORITYPB             rocket          Aplica as tres camadas de prioridade:
 *                                            CPU (pb_elevar_prioridade_cpu),
 *                                            D3DKMT (pb_elevar_prioridade_gpu),
 *                                            e Priority Boost (pb_elevar_priority_boost).
 *  5  INTERFACE              window          Configura o modo de janela/interface
 *                                            do PB conforme perfil recomendado.
 *  6  FPS_SELECTION_INDEX    speed           Le o indice de FPS recomendado do
 *                                            arquivo INI do PB (secao [Graphics],
 *                                            chave FPSType/FPSVal) e aplica o
 *                                            limite de FPS correto para o perfil.
 *  7  REETGAMEMODE           gamepad         Habilita o Windows Game Mode para
 *                                            a sessao, elevando a prioridade de
 *                                            I/O e CPU do PB no scheduler.
 *                                            (TReetGameModePanel @ 0x136f90d7)
 *
 *  -----------------------------------------------------------------------
 *  POR QUE OS MAPAS CARREGAM "INSTANTANEAMENTE"
 *  -----------------------------------------------------------------------
 *
 *  Nao ha uma chamada API de "preload de mapa". O efeito vem de:
 *
 *    a) Fullscreen exclusivo (item 1): o DWM para de compor a janela do PB,
 *       liberando GPU e VRAM que seriam gastos na composicao -- mais recursos
 *       disponiveis para carregar assets do mapa.
 *
 *    b) Prioridade ABOVE_NORMAL + D3DKMT + Priority Boost (item 4): o
 *       scheduler da CPU da mais tempo de CPU ao PB exatamente quando ele
 *       esta fazendo I/O intenso (descompactando assets, carregando texturas).
 *       Priority Boost amplifica o efeito quando threads de I/O saem de espera.
 *
 *    c) Windows Game Mode (item 7): o scheduler de I/O do Windows da maior
 *       prioridade ao PB para operacoes de leitura em disco -- mapas sao
 *       carregados de arquivos grandes e isso reduz a fila de I/O.
 *
 *    d) Gestao Inteligente (itens 2+3): o monitor aplica TODOS os tweaks do
 *       catalogo da categoria CPU_GPU_PRIORITY e POWER exatamente quando o
 *       PB esta subindo -- so nessa janela de tempo critica.
 *
 *  -----------------------------------------------------------------------
 *  ITENS SEPARADOS: LIMPEZA DE CACHE (nao fazem parte do FLUIDEZMAX, mas
 *  sao opcionais na mesma tela -- tabela em 0x1366c900)
 *  -----------------------------------------------------------------------
 *
 *  Chave                   Path/Alvo                            Aviso
 *  ----------------------  -----------------------------------  --------------------
 *  prefetch                %WINDIR%\Prefetch                    "Seguro, mas programas
 *                          (limpa *.pf -- arquivos de cache     podem abrir mais
 *                           do prefetcher do Windows)           devagar na 1a vez"
 *  driver_extract_cache    %SystemDrive%\NVIDIA\DisplayDriver\* "Seguro..."
 *                          (limpa cache de extracao de drivers
 *                           NVIDIA que ficam em disco)
 *
 *  A limpeza de Prefetch libera espaco em disco e remove dados obsoletos do
 *  prefetcher (que ja e desabilitado pelos tweaks do catalogo via reg add
 *  PrefetchParameters). Nao e a causa primaria do loading rapido.
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
 * cria um array de triplas (chave, icone, descricao) e dispara um
 * thread/dialog de progresso para executar cada item em sequencia.
 *
 * Traducao simplificada do decompilado (offsets confirmados):
 */
void executar_itens_habilitados(void *panel)
{
    /* Cria o registro de closure (ActRec) para a lambda de execucao.
     * FUN_13149aa8(&DAT_1370050c, 1) = construtor do tipo ActRec. */
    void *act_rec = criar_act_rec(&DAT_1370050c, /*ref=*/1);
    *(void **)((uint8_t *)act_rec + 0x18) = panel;  /* captura panel */

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

    /* Cria e exibe dialog de progresso (FUN_13206ee0 cria, FUN_13206c10 mostra). */
    void *dlg_prog = criar_dialog_progresso(&PTR_FUN_131d5998, /*sincronizado=*/1);
    *(void **)((uint8_t *)act_rec + 0x1c) = dlg_prog;

    /* FUN_136fade8() -- desativa algum estado global antes de comecar. */
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
extern void *criar_dialog_progresso(void *factory, int sync); /* FUN_13206ce8 */
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
 *    Camada 1 - NtSetTimerResolution(5000, TRUE, &atual)
 *               Solicita ao kernel a resolucao de 5000 unidades de 100ns =
 *               0.5ms. Funcao nao documentada exportada pela ntdll.dll.
 *               A thunk de importacao esta em 0x1357c294 (Ghidra reconhece
 *               pelo nome). O complemento Win32 "timeBeginPeriod(1)" (1ms)
 *               e chamado junto; o flag DAT_1380f238 registra se foi chamado
 *               para que o cleanup faca "timeEndPeriod(1)".
 *
 *    Camada 2 - Thread de manutencao ("ReetTimerPrecision", 0x1357c870)
 *               Outros processos podem chamar NtSetTimerResolution com valor
 *               maior e, quando eles saem, o Windows volta ao padrao. Para
 *               evitar isso, um thread de fundo re-aplica a resolucao em
 *               loop. O nome interno e "Maintain 0.5ms timer for low
 *               latency" (log string @ 0x1357c40c). O thread e criado em
 *               FUN_1357c9b0 e cancelado em FUN_1357ca34.
 *
 *    Camada 3 - SetProcessInformation / TimerResolutionPolicy (Win11 only)
 *               Em FUN_135a50d8, o ReetFPS chama SetProcessInformation no
 *               handle do jogo com ProcessPowerThrottling (class 4), mascara
 *               0x04 (PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION),
 *               valor 0 (CLEARED). Isso garante que o processo do jogo nao
 *               "ignore" a resolucao global -- ele responde ao 0.5ms.
 *               Na mesma funcao, a mascara 0x01 com valor 0 desativa o
 *               EcoQoS (throttling de execucao) no processo.
 *
 *  Cleanup (FUN_1357c5e0 @ 0x1357c5e0):
 *    NtSetTimerResolution(0, FALSE, &atual)  -- restaura resolucao padrao
 *    if (timeBeginPeriod chamado) timeEndPeriod(1)
 *
 *  Globais relevantes:
 *    DAT_1380f230  -- flag "thread de manutencao ativa"
 *    DAT_1380f238  -- flag "timeBeginPeriod(1) foi chamado"
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
         * O callback chama NtSetTimerResolution(5000, TRUE, &atual) periodicamente.
         * Nome interno: "ReetTimerPrecision" (DAT_1357c870). */
        g_timer_task = criar_task_thread(&PTR_FUN_1357c804, 1, 1); /* FUN_1321994c */
        configurar_task(g_timer_task, 0);                           /* FUN_13219fd8 */
        iniciar_dispatcher();                                       /* FUN_1321a804 */
    }

    g_timer_ativo = 1;  /* DAT_1380f230 = 1 */

    LeaveCriticalSection(&g_timer_cs);
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
 * pb_timer_resolution_policy_win11  (parte de FUN_135a50d8 @ 0x135a50d8)
 *
 * Configura o processo do jogo no Win11 para:
 *  (a) NAO ignorar a resolucao de timer global (IGNORE_TIMER_RESOLUTION=0)
 *  (b) NAO usar EcoQoS/throttling de execucao (EXECUTION_SPEED=0)
 *
 * Usa SetProcessInformation com ProcessPowerThrottling (class 4).
 * Log interno: "GetProcessInformation.TimerResolution.Pre"
 *              "SetProcessInformation.TimerResolutionPolicy"
 *
 * Apenas disponivel em Windows 11; a funcao verifica os ponteiros em
 * param_1+0x8a0 / param_1+0x8a4 (GetProcessInformation / SetProcessInformation)
 * e loga "ProcessPowerThrottling.ApiUnavailable" se nao existirem.
 */
void pb_timer_resolution_policy_win11(void *ctx, HANDLE hProcesso)
{
    /* A struct PROCESS_POWER_THROTTLING_STATE tem 12 bytes:
     *   ULONG Version       = 1
     *   ULONG ControlMask   = mascara dos campos que queremos controlar
     *   ULONG StateMask     = valor desejado para cada bit controlado     */
    PROCESS_POWER_THROTTLING_STATE throttle;

    /* --- Desativa EcoQoS (ControlMask=1, StateMask=0) --- */
    /* ControlMask=PROCESS_POWER_THROTTLING_EXECUTION_SPEED(1), StateMask=0 => DISABLE */
    throttle.Version     = 1;
    throttle.ControlMask = 0x01;   /* EXECUTION_SPEED */
    throttle.StateMask   = 0x00;   /* 0 = desabilitado */
    if (!SetProcessInformation(hProcesso,
                               ProcessPowerThrottling,
                               &throttle, sizeof(throttle))) {
        /* loga "SetProcessInformation.PowerThrottling" + GetLastError() */
    }

    /* --- Garante que o processo respeite a resolucao de timer global --- */
    /* ControlMask=PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION(4), StateMask=0 => honrar */
    throttle.Version     = 1;
    throttle.ControlMask = 0x04;   /* IGNORE_TIMER_RESOLUTION */
    throttle.StateMask   = 0x00;   /* 0 = NAO ignorar => respeitar o 0.5ms global */
    if (!SetProcessInformation(hProcesso,
                               ProcessPowerThrottling,
                               &throttle, sizeof(throttle))) {
        /* loga "SetProcessInformation.TimerResolutionPolicy" + GetLastError() */
        /* erros 0x57 (ERROR_INVALID_PARAMETER), 0x32 (ERROR_NOT_SUPPORTED),  */
        /* 0x78 (ERROR_SEM_TIMEOUT) sao silenciados -- API ausente em Win 10  */
    }
}

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
 *  seguranca: (a) nao apaga juncoes/symlinks, (b) usa prefixo \\?\ para
 *  caminhos longos, (c) reporta negados, em-uso e falhas separadamente.
 *
 *  FUNCOES MAPEADAS
 *    FUN_13674090  @  0x13674090  -- scanner recursivo de diretorio
 *    FUN_13673ee0  @  0x13673ee0  -- deleta arquivo individual
 *    FUN_13673dc4  @  0x13673dc4  -- normaliza caminho (adiciona \\?\)
 *    FUN_13674088  @  0x13674088  -- testa se entrada e juncao/symlink
 *
 *  ITENS DE LIMPEZA CONHECIDOS (tabela em 0x1366c900, verificada anteriormente)
 *    chave               alvo (expandido)
 *    prefetch            %WINDIR%\Prefetch\*.pf
 *    driver_extract_cache %SystemDrive%\NVIDIA\DisplayDriver\*
 *
 *  STRINGS DE STATUS (usadas no log interno)
 *    "Executando limpeza..."              @ 0x1365ef18
 *    "Limpeza coordenada: itens preservados (negado=%s, em uso=%s, protecao=%s)"
 *                                         @ 0x13673d30
 *    "Limpeza bloqueada em junction/symlink: "  @ 0x1367456c
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

/* Deleta um unico arquivo, rastreando o motivo da falha no stats.           */
void limpeza_deletar_arquivo(const DelphiStr caminho_curto)
{
    /* FUN_13673ee0 @ 0x13673ee0
     * param_4[-4] = ponteiro para TLimpezaStats da limpeza atual            */
    DelphiStr  caminho_longo = NULL;
    TLimpezaStats *s = limpeza_stats_get();

    limpeza_log_arquivo(caminho_curto);           /* FUN_13671600 -- log verbose */
    limpeza_normalizar_caminho(caminho_curto, &caminho_longo);

    if (!DeleteFileW((LPCWSTR)str_c(caminho_longo))) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) {
            /* Tenta tomar posse do arquivo (FUN_136712f0). */
            if (limpeza_tomar_posse(caminho_longo)) {
                s->deletado = 1;
                return;
            }
            s->negado_acesso = 1;
        }
        if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION) {
            s->arquivo_em_uso = 1;
        }
        s->negado_acesso |= (err == ERROR_ACCESS_DENIED);
        s->qtd_falhas++;
    } else {
        s->deletado = 1;
    }
}

/* Verifica se dwFileAttributes indica juncao ou symlink (FILE_ATTRIBUTE_REPARSE_POINT).
 * Retorna verdadeiro = e um reparse point, NAO deve ser recursado.          */
extern bool limpeza_e_juncao_symlink(DWORD dwAttribs); /* FUN_13674088 */

/* Scanner recursivo de diretorio. Itera com FindFirstFileW/FindNextFileW,
 * pula '.' e '..', e para cada entrada:
 *   - arquivo normal  -> chama limpeza_deletar_arquivo()
 *   - sub-diretorio   -> verifica juncao/symlink, se nao for, recursao
 * O parametro 'contexto' e o TLimpezaStats da limpeza atual.
 * Retorna true se tudo dentro do diretorio foi deletado com sucesso.        */
bool limpeza_varrer_diretorio(const DelphiStr dir_path)
{
    /* FUN_13674090 @ 0x13674090 */
    WIN32_FIND_DATAW fd;
    HANDLE hFind;
    DelphiStr  padrao = NULL;
    DelphiStr  entrada_path = NULL;
    bool  ok = true;
    TLimpezaStats *s = limpeza_stats_get();

    /* Adiciona \* ao caminho para o padrao de busca */
    str_concat(&padrao, dir_path, L"\\*");
    hFind = FindFirstFileW((LPCWSTR)str_c(padrao), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED)
            s->negado_acesso = 1;
        return true;  /* diretorio vazio ou sem permissao */
    }

    do {
        /* Pula entradas especiais */
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            goto proxima;

        str_concat(&entrada_path, dir_path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* Sub-diretorio */
            if (limpeza_e_juncao_symlink(fd.dwFileAttributes)) {
                /* E uma juncao/symlink -- nao recursamos
                 * (string: "Limpeza bloqueada em junction/symlink: ") */
                ok = false;
            } else {
                bool sub_ok = limpeza_varrer_diretorio(entrada_path);
                bool dir_deletado = limpeza_deletar_diretorio(entrada_path);
                if (dir_deletado) {
                    s->qtd_pastas++;
                }
                if (!sub_ok || !dir_deletado)
                    ok = false;
            }
        } else {
            /* Arquivo normal */
            bool del = limpeza_deletar_arquivo_retorna(entrada_path);
            if (del) {
                s->qtd_arquivos++;
                s->bytes_totais += fd.nFileSizeLow;
                s->bytes_totais += (uint64_t)fd.nFileSizeHigh << 32;
            } else {
                ok = false;
            }
        }
proxima:;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return ok;
}

/* Prototipos dos auxiliares internos da limpeza. */
extern void  limpeza_log_arquivo(DelphiStr caminho);         /* FUN_13671600 */
extern bool  limpeza_tomar_posse(DelphiStr caminho);         /* FUN_136712f0 */
extern bool  limpeza_deletar_diretorio(DelphiStr caminho);   /* FUN_13673fb4 */
extern bool  limpeza_deletar_arquivo_retorna(DelphiStr f);   /* chama FUN_13673ee0 e devolve bool */
extern void  str_concat(DelphiStr *dst, ...);
extern bool  str_starts_with(DelphiStr s, const wchar_t *prefix);
extern DelphiStr str_substr(DelphiStr s, int from);
extern const wchar_t *str_c(DelphiStr s);
extern void  str_clear(DelphiStr *p);


/* ===========================================================================
 *  12) TIMER RESOLUTION — SetProcessInformation / TimerResolutionPolicy (Win11)
 * ===========================================================================
 *
 *  Complemento da secao 10: enquanto a secao 10 cobre o NtSetTimerResolution
 *  global e a thread de manutencao, esta parte cobre o SetProcessInformation
 *  com ProcessPowerThrottling — que garante que o PROCESSO DO JOGO respeite
 *  a resolucao de 0.5ms definida globalmente.
 *
 *  O recurso "Timer Resolution" do ReetFPS usa a API de processo do Windows
 *  para desativar o throttling de CPU (ProcessPowerThrottling) e para
 *  requisitar timer de alta resolucao por processo
 *  (ProcessTimerResolutionPolicy, flag 4 = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION = 0).
 *
 *  Em outras palavras: com o flag DESATIVADO, o Windows honra timeBeginPeriod(1)
 *  para ESTE processo (ou usa o timer mais preciso disponivel) mesmo com
 *  o "ECO de timer" habilitado globalmente no Windows 11.
 *
 *  STRINGS DE UI E TELEMETRIA
 *    "Maintain 0.5ms timer for low latency"          @ 0x1357c40c  (tooltip)
 *    "ReetTimerPrecision"                            @ 0x1357c870  (nome do componente)
 *    "BUTTON_TIMER_RESOLUTION_ON"                    @ 0x137805cc  (evento UI)
 *    "BUTTON_TIMER_RESOLUTION_OFFClick&"             @ 0x13724f50  (evento UI)
 *
 *  STRINGS DE LOG INTERNO (FUN_135a50d8)
 *    "ProcessPowerThrottling.ApiUnavailable"          -- API nao disponivel (< Win10)
 *    "GetProcessInformation.PowerThrottling"          -- falha ao ler estado
 *    "SetProcessInformation.PowerThrottling"          -- falha ao desativar throttle
 *    "GetProcessInformation.TimerResolution.Pre"      -- falha ao ler timer atual
 *    "SetProcessInformation.TimerResolutionPolicy"    -- falha ao definir politica
 *
 *  FUNCAO PRINCIPAL: FUN_135a50d8 @ 0x135a50d8
 *    Recebe: param_1 = contexto TPointBlankStabilityMonitor
 *            param_2 = handle do processo do jogo (HANDLE hProc)
 *    Guarda resultados em:
 *      param_1 + 0x8cc  -- flag "timer resolution feature habilitado"
 *      param_1 + 0x8a0  -- ponteiro para vtable SetProcessInformation
 *      param_1 + 0x8a4  -- ponteiro para vtable GetProcessInformation
 *      param_1 + 0x3c   -- power_throttling_desabilitado (bool resultado)
 *      param_1 + 0x3f   -- timer_resolution_ativa (bool resultado)
 *      param_1 + 0x40   -- timer_policy_confirmada (bool resultado)
 *      param_1 + 0x41   -- timer_policy_set (bool resultado)
 *
 *  CONSTANTES DA API
 *    ProcessPowerThrottling (classe 9 em SetProcessInformation/GetProcessInformation):
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

void pb_configurar_timer_resolution(void *ctx, HANDLE hProc)
{
    /* FUN_135a50d8 @ 0x135a50d8
     *
     * A funcao so executa se a feature estiver marcada como ativa
     * (ctx + 0x8cc != 0) e as APIs estiverem disponiveis
     * (ctx + 0x8a0 e ctx + 0x8a4 != NULL).
     */
    if (*(char *)((uint8_t *)ctx + 0x8cc) == '\0')
        return;  /* feature desabilitada na UI */

    if (*(int *)((uint8_t *)ctx + 0x8a0) == 0 ||
        *(int *)((uint8_t *)ctx + 0x8a4) == 0) {
        /* API nao disponivel (Windows < 10 1703) */
        timer_log(ctx, L"ProcessPowerThrottling.ApiUnavailable", 0x78);
        return;
    }

    /* ---- Passo 1: desativar power throttling do processo ---- */
    REET_POWER_THROTTLING state;
    memset(&state, 0, sizeof(state));
    state.Version     = 1;
    state.ControlMask = 1;   /* PROCESS_POWER_THROTTLING_EXECUTION_SPEED */
    state.StateMask   = 0;   /* 0 = desabilitar throttling */

    int ok = vtable_SetProcessInformation(ctx, hProc, /*class*/9,
                                          &state, sizeof(state));
    if (!ok) {
        DWORD err = GetLastError();
        timer_log(ctx, L"SetProcessInformation.PowerThrottling", err);
    } else {
        *(uint8_t *)((uint8_t *)ctx + 0x3c) = 1; /* power_throttling_desabilitado */
        /* notifica o monitor de estado */
        timer_notificar_mudanca(ctx);
    }

    /* ---- Passo 2: requisitar timer de alta resolucao ---- */
    /* Le estado atual antes de alterar */
    REET_POWER_THROTTLING cur;
    memset(&cur, 0, sizeof(cur));
    cur.Version = 1;
    ok = vtable_GetProcessInformation(ctx, hProc, /*class*/9,
                                      &cur, sizeof(cur));
    if (!ok) {
        DWORD err = GetLastError();
        timer_log(ctx, L"GetProcessInformation.TimerResolution.Pre", err);
        return;
    }

    /* Se o flag 4 (ignore timer resolution) ja estiver desligado, nao precisa alterar */
    bool timer_ja_preciso = ((cur.ControlMask & 4) != 0) && ((cur.StateMask & 4) == 0);

    if (!timer_ja_preciso) {
        REET_POWER_THROTTLING timer_state;
        memset(&timer_state, 0, sizeof(timer_state));
        timer_state.Version     = 1;
        timer_state.ControlMask = 4;   /* PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION */
        timer_state.StateMask   = 0;   /* 0 = honrar timer de alta resolucao */

        ok = vtable_SetProcessInformation(ctx, hProc, /*class*/9,
                                          &timer_state, sizeof(timer_state));
        if (!ok) {
            DWORD err = GetLastError();
            /* erros 0x57/0x32/0x78 sao silenciados (API nao suporta) */
            if (err != ERROR_INVALID_PARAMETER && err != 0x32 && err != 0x78) {
                timer_log(ctx, L"SetProcessInformation.TimerResolutionPolicy", err);
            }
            *(uint8_t *)((uint8_t *)ctx + 0x8eb) = 0;
            *(uint8_t *)((uint8_t *)ctx + 0x8ea) = 0;
        } else {
            *(uint8_t *)((uint8_t *)ctx + 0x8eb) = 1;
            /* Confirma com GetProcessInformation */
            ok = vtable_GetProcessInformation(ctx, hProc, /*class*/9,
                                              &cur, sizeof(cur));
            if (ok && (cur.ControlMask & 4) != 0 && (cur.StateMask & 4) == 0) {
                *(uint8_t *)((uint8_t *)ctx + 0x8ea) = 1; /* timer_policy_confirmada */
                /* notifica o monitor de estado */
                timer_notificar_mudanca(ctx);
            }
        }
    } else {
        /* ja estava configurado */
        *(uint8_t *)((uint8_t *)ctx + 0x8eb) = 1;
        *(uint8_t *)((uint8_t *)ctx + 0x8ea) = 1;
    }

    /* Salva bitmask final nos campos de saida do contexto */
    *(uint32_t *)((uint8_t *)ctx + 0x74) = cur.ControlMask;
    *(uint32_t *)((uint8_t *)ctx + 0x78) = cur.StateMask;
    *(uint8_t *)((uint8_t *)ctx + 0x3f) =
        ((cur.ControlMask & 4) != 0 && (cur.StateMask & 4) == 0) ? 1 : 0;
    *(uint8_t *)((uint8_t *)ctx + 0x40) = *(uint8_t *)((uint8_t *)ctx + 0x8ea);
    *(uint8_t *)((uint8_t *)ctx + 0x41) = *(uint8_t *)((uint8_t *)ctx + 0x8eb);
}

/* Prototipos dos auxiliares de timer resolution. */
extern int  vtable_SetProcessInformation(void *ctx, HANDLE h, int cls, void *buf, uint32_t sz);
extern int  vtable_GetProcessInformation(void *ctx, HANDLE h, int cls, void *buf, uint32_t sz);
extern void timer_log(void *ctx, const wchar_t *msg, DWORD err);   /* FUN_135a4a18 */
extern void timer_notificar_mudanca(void *ctx);


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
 *  ESTRUTURA TRPCrosshair (offsets mapeados via decompilacao de FUN_136951fc)
 *    +0x318  quantidade_linhas     -- espessura / numero de linhas da mira
 *    +0x334  indice_forma          -- 0..N, indexa o array de formas:
 *                                    PTR_u_QUADRADO_CENTRAL_1380f738[]
 *    +0x338  sombra_ativa          -- bool: exibir sombra na mira
 *
 *  FORMAS DISPONIVEIS (array em PTR_u_QUADRADO_CENTRAL_1380f738 @ 0x1380f738)
 *    0  "QUADRADO CENTRAL"  -- quadrado no centro da tela
 *    1..N  outras formas (cruzes, pontos, etc. -- indice mapeado via IDA/Ghidra)
 *
 *  FUNCAO DE DESENHO: FUN_136951fc @ 0x136951fc
 *    Recebe: param_1 = ponteiro para TRPCrosshair
 *            param_2 = contexto de canvas (TCanvas ou similar)
 *    Fluxo:
 *      1. Le tamanho/posicao do canvas via FUN_13694bf0
 *      2. Define cor do background: 0xff04050b (RGBA: alpha=FF, quase preto)
 *      3. Obtem a forma selecionada: FUN_13191200(crosshair->+0x334, 1, 6)
 *         indexa PTR_u_QUADRADO_CENTRAL_1380f738
 *      4. Para cada "linha" da mira (local_2c em 0..1):
 *         - Se linha 0 E sombra_ativa (+0x338): desvia para loop adicional
 *         - Chama FUN_13695114() 4 vezes por linha (desenha os 4 segmentos)
 *      5. Usa FUN_13457c84 com cor 0xff14172e (navy) para o contorno
 *
 *  CONFIGURACAO E PERSISTENCIA
 *    A mira e salva/carregada via chaves "CROSSHAIR_SHADOW" e "crosshair1_space"
 *    no INI/registro do ReetFPS. A chave "CROSSHAIR_SHADOW" (@ 0x13696f1c)
 *    controla o checkbox de sombra.
 */

typedef struct {
    /* offsets confirmados via decompilacao de FUN_136951fc */
    /* ... outros campos VCL ... */
    int     quantidade_linhas; /* +0x318 -- espessura (numero de repetições por segmento) */
    /* ... */
    int     indice_forma;      /* +0x334 -- 0=QUADRADO CENTRAL, 1..N=outras */
    bool    sombra_ativa;      /* +0x338 -- exibir sombra */
} TRPCrosshair;

/* Renderiza a mira customizada no canvas fornecido.
 * Chamada a cada repaint da janela transparente sobreposta ao PB.            */
void crosshair_desenhar(TRPCrosshair *mira, void *canvas)
{
    /* FUN_136951fc @ 0x136951fc
     *
     * Pseudocodigo simplificado do decompilado:                              */
    float largura, altura;
    crosshair_obter_dimensoes(mira, &largura, &altura);

    /* Define cor de fundo (quase preto transparente) */
    canvas_set_color(canvas, 0xff04050b);   /* FUN_134576f8 */
    canvas_clear(canvas);

    /* Obtem nome da forma e o indice normalizado para o array */
    int idx_forma = crosshair_normalizar_indice(mira->indice_forma, /*min=*/1, /*max=*/6);
    const wchar_t *nome_forma = g_formas_crosshair[idx_forma];
                    /* g_formas_crosshair = PTR_u_QUADRADO_CENTRAL_1380f738  */

    /* Desenha o contorno/borda da mira com cor navy */
    canvas_draw_rect(canvas,
                     /*cor=*/0xff14172e,
                     /*x1=*/largura / 2, /*y1=*/altura / 2,
                     /*x2=*/largura / 2 + 84.0f, /*y2=*/altura / 2 + 84.0f);

    /* Itera duas vezes: passagem 0 = corpo, passagem 1 = sombra (se ativa) */
    for (int passo = 0; passo < 2; passo++) {
        if (passo == 0 && !mira->sombra_ativa)
            continue;          /* sombra desativada: so faz o corpo */

        /* Desenha cada segmento de acordo com quantidade_linhas */
        for (int s = 0; s < mira->quantidade_linhas; s++) {
            crosshair_desenhar_segmento(canvas);  /* FUN_13695114 -- N,S,E,W */
            crosshair_desenhar_segmento(canvas);
            crosshair_desenhar_segmento(canvas);
            crosshair_desenhar_segmento(canvas);
        }
    }
}

/* Prototipos dos auxiliares da mira. */
extern void    crosshair_obter_dimensoes(TRPCrosshair *m, float *w, float *h); /* FUN_13694bf0 */
extern int     crosshair_normalizar_indice(int idx, int min, int max);          /* FUN_13191200 */
extern void    canvas_set_color(void *canvas, uint32_t cor_rgba);               /* FUN_134576f8 */
extern void    canvas_clear(void *canvas);
extern void    canvas_draw_rect(void *canvas, uint32_t cor, float x1, float y1, float x2, float y2); /* FUN_13457c84 */
extern void    crosshair_desenhar_segmento(void *canvas);                       /* FUN_13695114 */
extern const wchar_t *g_formas_crosshair[];  /* PTR_u_QUADRADO_CENTRAL_1380f738 */


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
