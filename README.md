# ReetFPS.exe — reconstrução legível

Reconstrução, em C anotado, das partes principais do `ReetFPS.exe`, feita a
partir da decompilação do binário no Ghidra. O objetivo é **entender o que o
programa faz**, não recompilá-lo.

## O que é o ReetFPS

Um "otimizador de FPS" para o jogo **Point Blank**, escrito em **Delphi**
(RAD Studio / VCL), x86 32-bit. Sua função central é:

- **Otimização do Windows**: aplica um catálogo grande de ajustes executando
  comandos de shell elevados (`reg add`, `sc`, `bcdedit`, `netsh`, `powercfg`,
  remoção de appx, etc.).

> **Nota:** o fluxo de **login**/autenticação remota e as **validações de
> plano/licença** foram **retirados** desta reconstrução (projeto em
> homologação, caminho para open source). Esta documentação descreve o
> estado atual, sem login nem checagem de credenciais/licença.

Metadados do binário:

| | |
|---|---|
| Arquitetura | x86, 32-bit, little-endian |
| Compilador | Borland/Embarcadero Delphi |
| Image base | `0x13140000` |
| Funções | 18.086 |
| Símbolos | 56.972 |

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `reetfps.c` | Lógica reconstruída e comentada: **resolvedor do PowerShell** e o **modelo do otimizador**. É o arquivo para ler primeiro. |
| `ponto_blank.c` | 24 seções. Rotinas ligadas ao Point Blank (instalação, encerramento, prioridade de CPU/GPU, crashes, timer resolution), as funcionalidades da tela do programa (teclado, interface, FPS, mapas, mini-map, tela cheia, GPU, mira) e, na seção 24, a cadeia de carregamento `ReetFPS.exe → ReetFPS.dll → window.ime` (só descrição, sem código de injeção). O cabeçalho tem o índice e as convenções (`INFERIDO:` marca o que não foi comprovado). |
| `strings_decifradas.md` | Texto em claro das **988** strings cifradas do `ReetFPS.exe` (de 1011 chamadas ao decodificador), com a cifra explicada, o método de validação e a seção de uso de cada uma; mais as strings decifradas da `ReetFPS.dll`. |
| `catalogo_comandos.md` | Os **559 comandos** do otimizador, agrupados por efeito, em formato legível. |
| `catalogo_comandos.c` | Os mesmos 559 comandos como arrays de dados em C (`Tweak[]` por categoria). |
| `reetfps.h` | Tipo `Tweak` compartilhado. |
| `ponto_blank.md` | Resumo de alto nível do que o ReetFPS faz com o Point Blank (sem código). |

### Os outros arquivos da instalação (`C:\ReetFPS - Optimizer`)

| Arquivo | O que é |
|---|---|
| `ReetFPS.dll` | DLL C++ (MSVC) **sem exports**, com strings cifradas. Não contém as features: espera o `PointBlank.exe` e injeta nele um terceiro módulo, `window.ime`, pelo mecanismo de IME do Windows (seção 24 do `ponto_blank.c`). O `ReetFPS.exe` baixa uma biblioteca de `reetfps.com/update/lib_update.php?index=2` e a carrega da memória em outro processo; que seja esta DLL é inferido. |
| `window.ime` | O módulo que aplica as features do painel **dentro do jogo**. **Não existe no disco** (nem em System32/SysWOW64/Temp/AppData); provavelmente vem do servidor na inicialização. Por isso o efeito real de MINI-MAP OFF, tela cheia/F6, teclado de precisão, interface do lobby etc. não pôde ser verificado. |
| `ReetFix.exe` | Reparador/atualizador Delphi (`TFReetFix`). Uma cópia está **embutida no próprio `ReetFPS.exe`** no offset de arquivo `0x134297c` (cabeçalho `MZ`/`PE` conferido; os primeiros 64 KiB são idênticos ao `ReetFix.exe` em disco). |
| `libeay32.dll` / `ssleay32.dll` | OpenSSL usado pelo Indy (HTTPS). |
| `uninstall.exe` | Desinstalador. |

## Mapa das funções-chave (para abrir no Ghidra)

Gerado a partir do `ponto_blank.c` e do `reetfps.c` atuais. A coluna "§" é a
seção do `ponto_blank.c` (ou `reetfps.c`). Muitos endereços não têm função
definida no Ghidra; foram lidos pela desmontagem (prólogo `55 8B EC`).

**Auxiliares usados em várias seções**

| Endereço | Papel | Nome no C | § |
|---|---|---|---|
| `0x1358027c` | Card de notificação (16 parâmetros: título, corpo, duração + 13 na pilha) | `FUN_1358027c()` | todas |
| `0x134a8d98` | Decodificador de strings **ofuscadas** (não exibe nada); cifra em `0x134a8c90`, ver `strings_decifradas.md` | `decodificar_string()` | todas |
| `0x135d1fb8` | Despachante de comandos: monta um `.bat` com o array e executa em thread | `executar_lote()` | 14–16, 23 |
| `0x1369b158` | Grava par chave/valor no store JSON de configurações (ex.: `MINIMAP` = `ACTIVE`) | `config_gravar()` | 14, 16–22 |
| `0x1369ae6c` | Remove chave do store JSON | `config_remover()` | 14, 16–22 |
| `0x1369beb0` | Restaura o estado do painel: para cada chave ativa no JSON, chama de novo o handler (sem card) | — | 19 |
| `0x1354db34` / `0x1354cb2c` | Envia o JSON de configurações ao servidor (`setConfig=` / `connect=` … `&content=`) | — | cabeçalho |
| `0x132abec4` | `TControl.SetVisible` | `vcl_set_visible()` | todas |

**Otimizador (`reetfps.c`)**

| Endereço | Papel | Nome no C |
|---|---|---|
| `0x135401d0` | Caminho absoluto do `powershell.exe` (Sysnative vs System32) | `resolver_caminho_powershell()` |

**Point Blank e funcionalidades (`ponto_blank.c`)**

| Endereço | Papel | Nome no C | § |
|---|---|---|---|
| `0x136f010c` | Procura a instalação do PB (caminhos fixos + drives C..Z) | `pb_encontrar_instalacao()` | 1 |
| `0x135a0d84` | `taskkill /F /T` via `CreateProcessW` oculto | `pb_encerrar_processos()` | 2 |
| `0x135a3cec` | Resolve ponteiros de API (kernel32/psapi/gdi32) | `pb_init_ponteiros_api()` | 3 |
| `0x135a4dc4` | `SetPriorityClass(ABOVE_NORMAL)` no processo do PB | `pb_elevar_prioridade_cpu()` | 4 |
| `0x135a6bdc` | Religa o priority boost dinâmico | `pb_elevar_priority_boost()` | 5 |
| `0x135a56ec` | D3DKMT: classe ≤ NORMAL → ABOVE_NORMAL (3) | `pb_elevar_prioridade_gpu()` | 6 |
| `0x1357c4d0` | MMCSS ("Pro Audio"/"Games"/"Playback") na thread de timer do **ReetFPS** | `pb_mmcss_configurar()` | 7 |
| `0x1359f8b0` | Notificação diária de crashes (`pb-crash-yyyymmdd`) | `pb_verificar_crashes()` | 8 |
| `0x13700af0` | Ativa o perfil FLUIDEZMAX | `pb_ativar_fluidezmax()` | 9 |
| `0x137008d0` | `TReetGameModePanel.ExecuteGameModeActions` | `executar_itens_habilitados()` | 9 |
| `0x136ec13c` | Monta a lista de recomendações (grupos Windows e PointBlank) | — | 9, 18 |
| `0x136f78d0` | Despachante: chave da recomendação → botão `*_OFFClick` do `TGameBooster` | — | 9 |
| `0x1357c9b0` | Inicia a thread "ReetTimerPrecision" (`TTPWatchdog`) | `pb_timer_resolution_ativar_manutencao()` | 10 |
| `0x1357c5a4` | `NtSetTimerResolution(5000)`; `timeBeginPeriod(1)` só como fallback | `pb_timer_resolution_aplicar()` | 10 |
| `0x1357c5e0` | Revoga o timer (e `timeEndPeriod` se o fallback foi usado) | `pb_timer_resolution_revogar()` | 10 |
| `0x1357ca34` | Para a thread e revoga o timer | `pb_timer_resolution_parar()` | 10 |
| `0x13673dc4` | Normaliza caminho para `\\?\` | `limpeza_normalizar_caminho()` | 11 |
| `0x13673ee0` | Apaga um arquivo (com tomada de posse) | `limpeza_deletar_arquivo()` | 11 |
| `0x13674090` | Varredura recursiva da limpeza (remove só o link em junções) | `limpeza_varrer_diretorio()` | 11 |
| `0x135a50d8` | `SetProcessInformation` (classe 4: EcoQoS + TimerResolutionPolicy) no PB | `pb_configurar_timer_resolution()` | 12 |
| `0x136951fc` | Desenha a pré-visualização da mira (6 cores, sombra) | `crosshair_desenhar()` | 13 |
| `0x13694ab8` | Setter dos parâmetros da mira (clamp + repaint) | `crosshair_configurar_parametros()` | 13 |
| `0x13696e3c` | Liga/desliga a sobreposição da mira (`vtable[0x188]` em `mgr+0x550`) | — | 13 |
| `0x136b54d4` / `0x136b5704` | Teclado Turbo (`OpTeclado_ON`): ativar / restaurar | `pb_teclado_precisao_ativar()` / `_restaurar()` | 14 |
| `0x1372daf8` | Card "Teclado de Precisão" (`KEYBOARD` = `ACTIVE`; só a última tecla de um par oposto vale) | — | 14 |
| `0x135e9af4` / `0x135e9d98` | Blocos de 3 e 4 `reg add` de Keyboard Response | `teclado_bloco_ativar()` / `_restaurar()` | 14 |
| `0x136b0798` | "Ajustes de desempenho" (lote de 35 comandos em `0x135f22f0`) | `pb_ajustes_desempenho_ativar()` | 15 |
| `0x136b1f48` | Card do Game Bar | `pb_gamebar_desativar()` | 15 |
| `0x136b383c` / `0x136b35fc` | Transparência: desliga / religa (toasts trocados no próprio binário) | `pb_interface_transparencia_desligar()` / `_religar()` | 16 |
| `0x1372d4b8` | `INTERFACEDELAY_OFFClick`: card "INTERFACE SEM DELAY" (`INTERFACE`/`SET_INTERFACE` = `ACTIVE`; promete lobby do PB sem delays) | — | 16 |
| `0x1369407c` | Aplica o preset de FPS (1..6) e grava `FPS_SELECTION_INDEX` no JSON | `pb_fps_definir_preset()` | 17 |
| `0x13693740` | Clamp do preset: fora de [1,6] → 3 | `fps_clamp_preset()` | 17 |
| `0x13693fec` | Grava o índice no JSON (`IntToStr` + `FUN_1369b158`) | `fps_preset_salvar()` | 17 |
| `0x13728d20` | `MAPLOADING_OFFClick` (item LOADINGMAP) | `pb_loadingmap_ativar()` | 18 |
| `0x13729094` / `0x13729410` | `MINIMAP_OFFClick` / `MINIMAP_ONClick` | `pb_minimap_off_ativar()` / `_desfazer()` | 19 |
| `0x13582610` | TypeInfo de `TRPPBConfig` (lê/grava `EnvSet\env_settings.ini`) | — | 19 |
| `0x135845f4` / `0x13585130` | `TRPPBConfig`: carregar / salvar o `.ini` | `pbconfig_carregar()` / `pbconfig_salvar()` | 19 |
| `0x13600598` | **Reparo do sistema** (não tem relação com `TRPPBConfig`) | — | 19 |
| `0x137294d8` / `0x13729524` | `FPSUNLOCKED_OFFClick` / `FPSUNLOCKED_ONClick` | `pb_desbloqueador_fps_ativar()` / `_restaurar()` | 20 |
| `0x1372e888` / `0x1372ebb0` | `PRIORITYPB_OFFClick` / `PRIORITYPB_ONClick` | `pb_impulsionar_pb_ativar()` / `_restaurar()` | 21 |
| `0x13727a5c` | Reset das miras personalizadas (remove as 15 chaves `crosshair*`) e de 20 globais | `pb_resetar_miras()` | 21 |
| `0x1372dfb0` / `0x1372e304` | `TELACHEIA_OFFClick` / `TELACHEIA_ONClick` (`FULLSCREEN` = `ACTIVE`; card manda usar **F6** no jogo) | `pb_fullscreen_ativar()` / — | 22 |
| `0x1372e3d0` | Card Borderless (`BORDER_LESS`), exclusivo com a tela cheia | — | 22 |
| `0x135f7f88` / `0x135f8a60` | Lotes de 10 comandos da GPU: ativar / restaurar | `gpu_otimizacao_ativar()` / `_restaurar()` | 23 |
| `0x136c1ad6` | ActRec de `TGPU_Utils.NVIDIABOOST_OFFClick` (importa `ReetFPS.nip`) | `gpu_nvidiaboost_aplicar()` | 23 |
| `0x13734cd1` | `TGPURegistryWorker` (contadores PDH de uso/memória da GPU) | — | 23 |
| `0x136e7958` | `cLoadLibrary.Initialize`: tarefa que baixa `lib_update.php?index=2` e carrega a biblioteca da memória em outro processo | — | 24 |
| `0x1359474c` | Desenho do card "APLICAR PERFIL"/"PERFIL ATIVO" | — (apenas UI) | — |

**`ReetFPS.dll` (programa separado no Ghidra, image base `0x10000000`)** — ver seção 24

| Endereço | Papel |
|---|---|
| `0x10006c3d` → `0x100053a0` | `entry` → `DllMain`; em `DLL_PROCESS_ATTACH` chama `0x10004dd0` |
| `0x10004dd0` | Inicia a thread principal (`_beginthread(0x100041d0)`) |
| `0x100041d0` | Thread: espera `PointBlank.exe` (a cada 1 s), injeta `window.ime`, confere o módulo |
| `0x10003c00` | Autenticação temporária: `HKCU\Control Panel\Desktop\Colors\WindowMsg` == PID × 1482301 |
| `0x10001be0` | Caminho do stub: diretório System + `\window.ime` |
| `0x100024c0` | Injeção por IME (instala layout "window" e pede troca de idioma à janela do jogo) |
| `0x10002100` | Limpeza: `UnloadKeyboardLayout` + apaga o valor em `HKCU\Keyboard Layout\Preload` |
| `0x10001270` | Confere com `Module32First/Next` se `window.ime` está carregado no jogo |

## O que o otimizador faz (por categoria)

| Categoria | Comandos | Efeito |
|---|---:|---|
| SERVICES | 98 | desativa serviços (DiagTrack, SysMain, WSearch, Fax, diagnosticshub…) |
| CPU_GPU_PRIORITY | 39 | prioridade de CPU/GPU para jogos, `Win32PrioritySeparation`, cache |
| UI_RESPONSIVENESS | 33 | `MenuShowDelay`, animações do DWM, efeitos visuais |
| POWER | 28 | `powercfg` (planos de energia, PERFBOOST, throttle, timeouts) |
| GAMEDVR_GAMEBAR | 22 | desativa GameDVR/GameBar e captura |
| NETWORK | 10 | parâmetros Tcpip, `TCPNoDelay`, RSS, `netsh` |
| PRIVACY_TELEMETRY | 10 | `AllowTelemetry=0`, DataCollection |
| APPX_REMOVE | 8 | remove appx (Cortana, etc.) |
| BOOT_BCDEDIT | 6 | `bcdedit` (useplatformtick, hypervisor, numproc…) |
| EVENTLOG | 3 | limita o tamanho dos logs de eventos |
| OTHER | 302 | mouse/teclado, menu de contexto do ReetFPS, demais `reg add`; inclui pares aplicar/reverter |

> Muitos comandos vêm em **pares aplicar/reverter** (ex.: `MenuShowDelay` com 0 e
> com 400), o que indica funções de "aplicar perfil" e "restaurar padrão".

## Limites desta reconstrução (honestidade)

- **Não é o código-fonte Delphi original** — ele não existe mais no `.exe`.
  Isto é uma leitura em C do que o binário faz.
- **Não compila.** Nomes e comentários são inferidos da análise; os endereços
  originais ficam nos comentários para conferência no Ghidra.
- Cobre as **partes úteis** (execução de ajustes, catálogo completo de
  comandos), não as 18 mil funções — a maior parte é runtime do Delphi (VCL/RTL)
  e código de desenho da interface, que não agregam ao entendimento.
- **As features do painel não são verificáveis por inteiro.** Os botões do
  painel só gravam chaves (`MINIMAP=ACTIVE` etc.) num JSON sincronizado com o
  servidor; o código que age dentro do jogo está no `window.ime`, que não
  está em disco. A seção 24 do `ponto_blank.c` descreve até onde a análise
  estática chega.
- Para ir além em binários Delphi, a ferramenta **IDR (Interactive Delphi
  Reconstructor)** recupera nomes de formulários, métodos e propriedades melhor
  que o Ghidra.
