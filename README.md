# ReetFPS.exe — reconstrução legível

Reconstrução, em C anotado, das partes principais do `ReetFPS.exe`, feita a
partir da decompilação do binário no Ghidra. O objetivo é **entender o que o
programa faz**, não recompilá-lo.

## O que é o ReetFPS

Um "otimizador de FPS" para o jogo **Point Blank**, escrito em **Delphi**
(RAD Studio / VCL), x86 32-bit. Ele faz duas coisas centrais:

1. **Login** contra um servidor (autenticação remota via HTTP).
2. **Otimização do Windows**: aplica um catálogo grande de ajustes executando
   comandos de shell elevados (`reg add`, `sc`, `bcdedit`, `netsh`, `powercfg`,
   remoção de appx, etc.).

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
| `reetfps.c` | Lógica reconstruída e comentada: fluxo de **login**, **resolvedor do PowerShell** e o **modelo do otimizador**. É o arquivo para ler primeiro. |
| `ponto_blank.c` | Rotinas que o ReetFPS executa **diretamente no processo do jogo**: descoberta da instalação, encerramento de processos, elevação de prioridade de CPU/GPU/MMCSS e monitoramento de crashes. |
| `catalogo_comandos.md` | Os **559 comandos** do otimizador, agrupados por efeito, em formato legível. |
| `catalogo_comandos.c` | Os mesmos 559 comandos como arrays de dados em C (`Tweak[]` por categoria). |
| `reetfps.h` | Tipo `Tweak` compartilhado. |
| `ponto_blank.md` | Resumo de alto nível do que o ReetFPS faz com o Point Blank (sem código). |

## Mapa das funções-chave (para abrir no Ghidra)

| Endereço | Papel | Reconstruído como |
|---|---|---|
| `0x1371598c` | Handler do clique no botão de login (`LoginButton_Panel2Click`) | `LoginButton_Click()` |
| `0x137161f4` | Thunk RTTI → `CALL 0x1371598c; RET` | — |
| `0x135401d0` | Escolhe o caminho do `powershell.exe` (Sysnative vs System32) | `resolver_caminho_powershell()` |
| `0x13700af0` | Entry point do perfil FLUIDEZMAX (`TReetGameModePanel`) | `pb_ativar_fluidezmax()` |
| `0x137008d0` | `TReetGameModePanel.ExecuteGameModeActions` — coleta itens e despacha | `executar_itens_habilitados()` |
| `0x135a50d8` | PowerThrottling + TimerResolutionPolicy no processo do jogo (Win11) | `pb_timer_resolution_policy_win11()` |
| `0x1357c9b0` | Inicia thread de manutenção do timer de 0.5ms | `pb_timer_resolution_ativar_manutencao()` |
| `0x1357ca34` | Para a thread e reverte o timer | `pb_timer_resolution_parar()` |
| `0x1357c5e0` | Revert: `NtSetTimerResolution(0,FALSE)` + `timeEndPeriod(1)` | `pb_timer_resolution_revogar()` |
| `0x1357c294` | Thunk de importação de `NtSetTimerResolution` (ntdll.dll) | — (thunk) |
| `0x13674090` | Scanner recursivo: varre diretório e deleta arquivos (limpeza inteligente) | `limpeza_varrer_diretorio()` |
| `0x13673ee0` | Deleta arquivo individual com fallback de tomada de posse | `limpeza_deletar_arquivo()` |
| `0x13673dc4` | Normaliza caminho para `\\?\` (suporte a caminhos longos) | `limpeza_normalizar_caminho()` |
| `0x1369407c` | Constrói instância de `TRPCrosshair` e registra no painel | `crosshair_criar()` |
| `0x13694ab8` | Define parâmetros geométricos do crosshair (com clamp + repaint) | `crosshair_configurar_parametros()` |
| `0x136951fc` | Renderiza o crosshair sobre a janela do Point Blank | `crosshair_desenhar()` |
| `0x13696e3c` | Aplica configurações do diálogo "PERSONALIZAR MIRA" | `crosshair_aplicar_do_dialogo()` |
| `0x1361508c` | Desenho do botão "LOGIN"/"ENTRANDO..." (UI) | — (apenas UI) |
| `0x1359474c` | Desenho do card "APLICAR PERFIL"/"PERFIL ATIVO" (UI) | — (apenas UI) |
| `0x136b1f48` | Handler que desativa o Game Bar dentro de ENTRADA INSTANTÂNEA | `pb_entrada_instantanea_desativar_gamebar()` |
| `0x135d5b10` | Início da tabela com 9 comandos `reg` do MMCSS "Low Latency" | `configurar_mmcss_low_latency()` |
| `0x135e9b30` | Início da tabela de comandos `reg` de TECLADO DE PRECISÃO (aplicar) | `pb_teclado_precisao_ativar()` |
| `0x135e9ddc` | Início da tabela de comandos `reg` de TECLADO DE PRECISÃO (restaurar) | `pb_teclado_precisao_restaurar()` |
| `0x136b35e4` | Desativa transparencia Aero + efeitos visuais (INTERFACE SEM DELAY) | `pb_interface_sem_delay_ativar()` |
| `0x135eb0a0` | Executa 7 comandos de responsividade de menu/janela (sub-rotina) | `configurar_responsividade_interface()` |
| `0x135f3848` | `VisualFXSetting = 0` — efeitos visuais no modo "melhor desempenho" | — (dado) |
| `0x135e8030` | `EnableTransparency = 0` — desativa transparencia do Aero | — (dado) |
| `0x13691854` | Construtor de `TRPFpsLimit` — cria o painel "TURBINAR FPS" | — (construtor VCL) |
| `0x1369407c` | Entry point do aplicador de FPS por preset (índices 1–6) | `pb_fps_definir_preset()` |
| `0x13693740` | Clamp do índice de preset: [1,6], default 3 se fora do range | — (auxiliar) |
| `0x13693e20` | Aplicador completo: atualiza UI + thread de perf + motor do jogo | `fps_aplicar_no_jogo()` |
| `0x13693fec` | Grava `FPS_SELECTION_INDEX` (ou `"UNLOCKEDFPS"`) no registro | `pb_fps_preset_salvar()` |
| `0x13574f7c` | Setter atômico do FPS cap: escreve string em `fps_obj+0xc` e repinta | — (setter) |
| `0x13687df8` | String estática `"FPS 486"` — valor máximo (modo SEM LIMITE) | — (dado) |
| `0x13693728` | String estática `"UNLOCKEDFPS"` — marcador de estado ilimitado | — (dado) |
| `0x136ec13c` | Init do painel MAPAS INSTANTÂNEOS: monta lista Superfetch_ON / LOADINGMAP / FULLSCREEN | `pb_mapas_instantaneos_ativar()` |
| `0x135d50b4` | Início dos 3 comandos `reg` LanmanWorkstation cache (LOADINGMAP) | `mapas_configurar_lanman_cache()` |
| `0x135d2ab4` | `sc stop SysMain` — desativa Superfetch/SysMain (MAPAS item 1) | `mapas_desativar_superfetch()` |
| `0x135f0438` | `EnableSuperfetch=0` + `EnablePrefetcher=0` via PrefetchParameters | `mapas_desativar_superfetch()` |
| `0x13582628` | RTTI da classe `TRPPBConfig` (`uRPPBConfig`) — gerencia o arquivo de config do PB | `pb_minimap_off_ativar()` |
| `0x13584ce4` | Chave INI `HUD_Effect` (tabela de leitura) — controla efeitos de HUD | — (dado) |
| `0x13584d08` | Chave INI `Enable_MissionIndicator` (tabela de leitura) — controla o mini-mapa | — (dado) |
| `0x13585878` | Chave INI `HUD_Effect` (tabela de escrita) — gravada com valor 0 | — (dado) |
| `0x1358589c` | Chave INI `Enable_MissionIndicator` (tabela de escrita) — gravada com valor 0 | — (dado) |
| `0x13600598` | Handler do item `OPTIMIZER_PB_MANAGER` — cria contexto TRPPBConfig e aplica | `pb_minimap_off_ativar()` |
| `0x135c52bc` | Enfileirador do ApplyConfig — verifica caminho do jogo e serializa o arquivo INI | `pbconfig_aplicar_config()` |
| `0x136f7b50` | String `OPTIMIZER_PB_MANAGER` na tabela de despacho de perfis | — (dado) |
| `0x13729580` | Label `"DESBLOQUEIO DE FPS"` (UTF-16LE) — título da feature | — (dado) |
| `0x137295a8` | Ativa o DESBLOQUEADOR DE FPS: vtable[0x188](fps_cap_obj, 1) | `pb_desbloqueador_fps_ativar()` |
| `0x1372987c` | Restaura o cap de FPS: vtable[0x188](fps_cap_obj, 0) | `pb_desbloqueador_fps_restaurar()` |
| `0x13727a5c` | Valida / exibe sequência de progresso do FPS Game Booster | `pb_impulsionar_pb_validar()` |
| `0x13728234` | Ativa o IMPULSIONAR POINTBLANK: vtable[0x188](booster_obj+0x500, 1) | `pb_impulsionar_pb_ativar()` |
| `0x13728578` | Restaura o booster: vtable[0x188](booster_obj+0x500, 0) | `pb_impulsionar_pb_restaurar()` |
| `0x13727a00` | Inicia o Point Blank pelo assistente (botão "INICIAR POINTBLANK") | `pb_impulsionar_iniciar_jogo()` |
| `0x136ea1e8` | String `" FPS Game Booster"` — label interna do painel IMPULSIONAR | — (dado) |
| `0x136ea420` | Chave de estado `"FirstAccessPointBlankBoosterApplied"` (HKCU\Keyboard Layout\ReetFPS) | — (dado) |
| `0x136ece9c` | String `"FULLSCREEN"` — chave de item de perfil / estado (HKCU\Keyboard Layout\ReetFPS) | `pb_fullscreen_ativar()` |
| `0x136ecfb4` | String `"Tela cheia otimizada"` — descrição do item de perfil | — (dado) |
| `0x13585938` | Tabela de escrita TRPPBConfig; `ScreenMode` é o primeiro campo (offset 0x00) | `pb_fullscreen_ativar()` |
| `0x13600598` | Handler `OPTIMIZER_PB_MANAGER` — cria contexto TRPPBConfig e aplica (compartilhado com §19) | `pb_fullscreen_ativar()` |
| `0x136c1ad6` | RTTI `TGPU_Utils` — painel AJUSTES DA GPU; métodos: `NVIDIABOOST_OFFClick`, `CheckDriverAndChipset`, `DriverRowClick` | `gpu_nvidiaboost_aplicar()` |
| `0x13734cd1` | RTTI `TGPURegistryWorker` — worker PDH: monitora utilização e memória dedicada da GPU | — (worker) |
| `0x135f7f54` | Dispatcher dos 9 comandos de registro ATIVAR (GPU Priority, HAGS, TCPNoDelay…) | `gpu_otimizacao_ativar()` |
| `0x135f94ac` | Dispatcher dos 7 comandos de registro DESLIGAR / restaurar padrões | `gpu_otimizacao_restaurar()` |
| `0x135d880c` | Início dos 16 comandos `Reg.exe` de latência D3 da GPU (`DefaultD3TransitionLatency*=1`) | `gpu_otimizacao_ativar()` |
| `0x135f8000` | Comando: `GPU Priority=8` em `Tasks\Games` | — (dado) |
| `0x135f897c` | Comando: `HwSchMode=2` — habilita HAGS | — (dado) |
| `0x135d70a0` | Comando: `Win32PrioritySeparation=38` — fatia de CPU para 1º plano | — (dado) |
| `0x136c1e50` | Item de perfil `NVIDIABOOST` — configura Painel NVIDIA para máx. desempenho | — (dado) |
| `0x136697f4` | Item `gpu_directx` — limpa DXCache / GLCache da NVIDIA | — (dado) |
| `0x13669bbc` | Item `gpu_nvidia` — otimizações NVIDIA | — (dado) |
| `0x13669ea0` | Item `gpu_amd`   — otimizações AMD | — (dado) |
| `0x1366a098` | Item `gpu_intel` — otimizações Intel | — (dado) |
| `0x136c56bc` | String "Os ajustes da GPU foram desligados…" (UI de desativação) | — (dado) |
| `0x136c57f8` | String "Restaurando ajustes da GPU" (progresso de restauração) | — (dado) |

Estado global do login:

| Endereço | Significado |
|---|---|
| `DAT_13810328` | flag "login em andamento" |
| `DAT_13819f04` | instância do formulário de login |

## Como a validação de login funciona (resumo)

O handler lê os dois campos de texto do formulário (usuário em `+0x474`, senha
em `+0x478`), guarda num registro de credenciais (usuário em `+0x10`, senha —
campo `PasswordValue` — em `+0x0c`) e faz **apenas uma checagem local: os dois
campos não podem estar vazios**. Se ok, desabilita a UI, mostra "ENTRANDO..." e
dispara o worker que envia as credenciais ao servidor. **A conferência da senha
é remota** — não há comparação de senha dentro do executável. Classes de rede
presentes no binário: `System.Net.URLClient`, `TCredentialsStorage`,
`TIdHTTP`/`TIdAuthentication`.

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
- Cobre as **partes úteis** (login, execução de ajustes, catálogo completo de
  comandos), não as 18 mil funções — a maior parte é runtime do Delphi (VCL/RTL)
  e código de desenho da interface, que não agregam ao entendimento.
- Para ir além em binários Delphi, a ferramenta **IDR (Interactive Delphi
  Reconstructor)** recupera nomes de formulários, métodos e propriedades melhor
  que o Ghidra.
