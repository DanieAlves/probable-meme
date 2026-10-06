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
 *    1) Tela de LOGIN: le usuario/senha, valida localmente so que nao estao
 *       vazios e envia para um servidor (autenticacao e REMOTA, via HTTP).
 *    2) OTIMIZADOR: aplica um catalogo grande de ajustes no Windows executando
 *       comandos de shell (reg add, sc, bcdedit, netsh, powercfg, remocao de
 *       appx, etc.). O catalogo completo esta em "catalogo_comandos.md" e as
 *       estruturas de dados estao em "catalogo_comandos.c".
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
 * como ponteiro opaco; "vazia" == ponteiro nulo (foi exatamente assim que a
 * validacao de login testou os campos). */
typedef void *DelphiStr;

/* Instancia de formulario/VCL. Acessamos campos por offset, como no binario. */
typedef uint8_t TForm;


/* ===========================================================================
 *  1) LOGIN
 * ===========================================================================
 *
 *  Handler original: LoginButton_Panel2Click
 *      - Nome publicado na RTTI em 0x137142f5.
 *      - O ponteiro RTTI (0x137161f4) e um thunk: "CALL 0x1371598c; RET".
 *      - A funcao real do clique e FUN_1371598c  ->  LoginButton_Click abaixo.
 *
 *  Estado global:
 *      DAT_13810328  ->  g_login_em_andamento  (flag: ja clicou em entrar)
 *      DAT_13819f04  ->  g_form_login          (instancia do formulario)
 *
 *  Campos do formulario de login (offsets observados no binario):
 *      +0x474  ->  edit do USUARIO   (TEdit.Text)
 *      +0x478  ->  edit da SENHA     (TEdit.Text)
 *      +0x488  ->  rotulo de status / mensagem
 *      +0x468  ->  botao/painel ocultado ao entrar
 *      +0x48c  ->  botao/painel ocultado ao entrar
 *      +0x490  ->  controle extra desabilitado ao entrar
 *
 *  Registro de credenciais (tipo em DAT_13715434, possui campo "PasswordValue"):
 *      +0x10  ->  texto do usuario
 *      +0x0c  ->  texto da senha  (PasswordValue)
 */

extern char       g_login_em_andamento;   /* DAT_13810328 */
extern TForm     *g_form_login;            /* DAT_13819f04 */

/* Getter de propriedade .Text de um controle VCL (FUN_132abfc0). */
extern void  vcl_get_text(void *controle, DelphiStr *destino);
/* Atribui uma UnicodeString a outra com contagem de referencia (FUN_1314bc9c). */
extern void  str_assign(DelphiStr *destino, DelphiStr origem);
/* Cria o registro de credenciais a partir do tipo em DAT_13715434 (FUN_13149aa8). */
extern void *cred_record_new(void);
/* Habilita/desabilita ou mostra/oculta um controle (metodo VCL em vtbl+0xA0). */
extern void  vcl_set_enabled_visible(void *controle, int ligado);
/* Mostra uma mensagem/toast na UI (FUN_134a8d98 + agendador FUN_1314c690). */
extern void  ui_mostrar_mensagem(void *closure, int cor);
/* Inicia o worker de autenticacao e dispara a requisicao HTTP ao servidor.
 * No binario: FUN_13498880(&PTR_LAB_1348d760, ...) cria o objeto, e em seguida
 * chama o metodo em (vtable + 0x24), que faz o login remoto de fato. */
extern void  auth_worker_iniciar_login(void *cred_record);

/*
 * LoginButton_Click  (original: FUN_1371598c @ 0x1371598c)
 *
 * Fluxo exato observado na decompilacao:
 */
void LoginButton_Click(void)
{
    void      *cred;
    DelphiStr  usuario = NULL;   /* local_18 no binario */
    DelphiStr  senha   = NULL;   /* local_1c no binario */

    /* Cria o registro de credenciais (tipo DAT_13715434). */
    cred = cred_record_new();                      /* FUN_13149aa8(&DAT_13715434,1) */

    /* Guarda: so prossegue se nao ha login em andamento e o form existe. */
    if (g_login_em_andamento != 0 || g_form_login == NULL)
        return;

    /* Le os dois campos de texto do formulario. */
    vcl_get_text(*(void **)((uint8_t *)g_form_login + 0x474), &usuario); /* USUARIO */
    str_assign((DelphiStr *)((uint8_t *)cred + 0x10), usuario);

    vcl_get_text(*(void **)((uint8_t *)g_form_login + 0x478), &senha);   /* SENHA   */
    str_assign((DelphiStr *)((uint8_t *)cred + 0x0c), senha);

    /* ------------------------------------------------------------------
     *  A VALIDACAO LOCAL E APENAS ESTA: os dois campos nao podem estar
     *  vazios (ponteiro de string != NULL). NAO ha conferencia de senha
     *  aqui - isso e feito pelo servidor.
     * ------------------------------------------------------------------ */
    bool usuario_preenchido = (*(void **)((uint8_t *)cred + 0x10) != NULL);
    bool senha_preenchida   = (*(void **)((uint8_t *)cred + 0x0c) != NULL);

    if (usuario_preenchido && senha_preenchida)
    {
        /* --- SUCESSO: campos ok, inicia autenticacao remota --- */
        g_login_em_andamento = 1;                       /* DAT_13810328 = 1 */

        /* Oculta/desabilita os controles de login enquanto conecta. */
        vcl_set_enabled_visible(*(void **)((uint8_t *)g_form_login + 0x48c), 0);
        vcl_set_enabled_visible(*(void **)((uint8_t *)g_form_login + 0x468), 0);

        /* Mostra o "ENTRANDO..." / spinner (ver FUN_1361508c, que desenha). */
        ui_mostrar_mensagem(/*closure*/ (void *)0x13715c68, 0xff);

        /* Dispara o worker que envia usuario+senha ao servidor por HTTP.
         * Classes de rede presentes no binario: System.Net.URLClient,
         * TCredentialsStorage, TIdHTTP/TIdAuthentication. */
        auth_worker_iniciar_login(cred);
    }
    else
    {
        /* --- ERRO: algum campo vazio --- */
        ui_mostrar_mensagem(/*closure*/ (void *)0x13715c3c, 0x11c);
    }
}


/* ===========================================================================
 *  2) RESOLVEDOR DO CAMINHO DO POWERSHELL
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
 *  Retorna, em *destino, o caminho escolhido (relativo a %WINDIR%).
 */
void resolver_caminho_powershell(DelphiStr *destino)
{
    DelphiStr sysnative = NULL;  /* local_8  */
    DelphiStr tmp       = NULL;  /* local_c  */

    /* FUN_135400c4(): verdadeiro quando o SO e 64-bit (WOW64 ativo p/ este exe). */
    if (so_e_64bits())
    {
        montar_caminho_windir(&sysnative, L"Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe");

        /* FUN_1316db0c(path, 1): o arquivo existe? */
        if (arquivo_existe(sysnative))
        {
            copiar_str(destino, L"Sysnative\\WindowsPowerShell\\v1.0\\powershell.exe");
            return;
        }
    }

    /* Fallback: PowerShell nativo. */
    copiar_str(destino, L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    (void)tmp;
}

/* Prototipos dos auxiliares usados acima (mapeamento 1:1 com o binario). */
extern bool so_e_64bits(void);                              /* FUN_135400c4 */
extern void montar_caminho_windir(DelphiStr *dst, const wchar_t *sufixo); /* FUN_1314c828 */
extern bool arquivo_existe(DelphiStr caminho);              /* FUN_1316db0c */
extern void copiar_str(DelphiStr *dst, const wchar_t *txt); /* FUN_1314c7d0 */


/* ===========================================================================
 *  3) OTIMIZADOR (visao geral)
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
 *  Modelo de execucao (reconstruido conceitualmente):
 */

typedef struct {
    const char *comando;    /* linha de shell executada */
    const char *categoria;  /* grupo logico (ver acima) */
} Tweak;

/* Aplica um ajuste: resolve o shell elevado e executa a linha de comando.
 * No binario isso passa pelo resolver_caminho_powershell() e por um
 * ShellExecute/CreateProcess com privilegio de administrador (o app avisa
 * "Execute o ReetFPS como administrador" quando falta elevacao). */
void aplicar_tweak(const Tweak *t)
{
    DelphiStr shell = NULL;
    resolver_caminho_powershell(&shell);
    executar_elevado(shell, t->comando);   /* roda a linha; saida silenciada (>nul 2>&1) */
}

extern void executar_elevado(DelphiStr shell, const char *linha_de_comando);

/* Aplica todos os ajustes de uma categoria (perfil "competitivo", game mode,
 * perfil de energia, etc.). As strings de UI correspondentes sao:
 *   "APLICAR PERFIL", "PERFIL ATIVO", "Perfil Competitivo aplicado com sucesso.",
 *   "Ativa o modo de jogo recomendado...", "Aplica o perfil de energia...". */
void aplicar_categoria(const Tweak *lista, int n)
{
    for (int i = 0; i < n; i++)
        aplicar_tweak(&lista[i]);
}

/* ============================================================================
 *  FIM. Para o catalogo completo de comandos, ver os arquivos companheiros.
 * ========================================================================== */
