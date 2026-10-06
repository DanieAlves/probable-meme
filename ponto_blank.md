# ReetFPS × Point Blank — o que o programa faz referente ao jogo

Adendo ao `README.md`, ao `reetfps.c` e ao `ponto_blank.c`. Documenta, em
alto nível, o que o ReetFPS faz especificamente com o Point Blank e com as
funcionalidades da sua tela — além dos 559 ajustes de Windows do catálogo.

A documentação aqui descreve **comportamento observado** no binário via
decompilação e desmontagem (nomes de classes e métodos publicados na RTTI,
strings de UI, imports da PE, labels de telemetria interna do próprio
programa). O que não foi comprovado está marcado como **INFERIDO**. Muitos
textos do programa são **strings ofuscadas** (decodificadas em tempo de
execução por `FUN_134a8d98`); elas foram decifradas e estão listadas, com a
cifra e o método de validação, em `strings_decifradas.md`. As rotinas
sensíveis não têm reconstrução passo a passo (ver "Nota sobre reconstrução",
no fim).

---

## 1. O que o ReetFPS NÃO faz com o jogo (e o que faz por outro caminho)

Para alinhar expectativas:

- **Não altera binários do Point Blank em disco.** Não há no binário rotinas
  de patching de `.exe`/`.dll`/`.pak`/arquivos de recurso do jogo. Do
  launcher, ele só *localiza* e *inicia* o `PBLauncher.exe`.
- **O `ReetFPS.exe` em si não escreve no processo do jogo**, a não ser os
  ajustes de prioridade de CPU/GPU e de energia descritos em 2.5 e 2.9. Os
  botões da tela FPS Game Booster só gravam chaves de configuração.
- **Mas existe código que roda DENTRO do jogo.** A `ReetFPS.dll` espera o
  `PointBlank.exe` abrir e injeta nele um módulo chamado `window.ime`, usando
  o mecanismo de métodos de entrada (IME) do Windows. É esse módulo — que
  **não está no disco** — que aplicaria as features do painel. Ver 2.21.

Ressalva sobre a configuração do jogo: o binário contém a classe
`TRPPBConfig`, que **lê e grava** `<pasta do jogo>\EnvSet\env_settings.ini`
(`FUN_135845f4` / `FUN_13585130`). Nenhum dos handlers analisados a chama,
e quem dispara a gravação **não foi rastreado**. Por isso não dá para afirmar
nem que o ReetFPS altera, nem que não altera esse arquivo.

## 2. O que o ReetFPS FAZ com o jogo

### 2.1. Descobre onde o jogo está instalado

Function `FUN_136f010c` lê o registro e os caminhos padrão de instalação:

| Fonte | Chave / caminho |
|---|---|
| Registro (64-bit) | `HKLM\SOFTWARE\Zepetto\PointBlank` |
| Registro (32-bit sob WOW64) | `HKLM\SOFTWARE\WOW6432Node\Zepetto\PointBlank` |
| Fallback 1 | `C:\Program Files (x86)\Zepetto\PointBlank\` |
| Fallback 2 | `C:\Program Files\Zepetto\PointBlank\` |
| Fallback 3 | `C:\Zepetto\PointBlank\` |

Mensagens de erro associadas (strings do binário):

- `"não foi possível localizar o Point Blank. Abra o PBLauncher manualmente uma vez com o ReetFPS aberto e tente novamente."`
- `"não foi possível iniciar o Point Blank. Feche o PBLauncher, abra o ReetFPS como administrador e tente novamente."`

### 2.2. Inicia o jogo pelo launcher oficial

O ReetFPS **não** inicia o `.exe` do jogo diretamente; ele sempre passa pelo
`PBLauncher.exe` (32 ocorrências dessa string no binário). Isso é consistente
com a arquitetura do Point Blank, em que o launcher gerencia atualização e
proteção antes de abrir o cliente.

Um botão com o rótulo `"ABRIR O PBLAUNCHER"` existe na interface para o caso
de o início automático falhar. Na tela FPS Game Booster, o botão de iniciar o
jogo é `BUTTON_STARTPBClick` (`0x13730064`, não reconstruído).

### 2.3. Encerra os processos do jogo quando pede

Function `FUN_135a0d84` monta a linha `taskkill.exe /F /T /IM "<nome.exe>"`
e dispara. Uma segunda rota em `0x135cafd4` monta `cmd /c taskkill /im "..."`.
Strings de UI que acompanham:

- `"Os processos do PointBlank foram encerrados. Pode abrir o jogo de novo."`
- `"O jogo fechou, mas o processo continua aberto. Abra para encerrar."`
- `"Processo %s encerrado"`

### 2.4. Monitora estabilidade do jogo (telemetria interna do ReetFPS)

Classes RTTI presentes no binário (todas internas ao ReetFPS, não ao jogo):

- `TPointBlankStabilityMonitor` → acompanha o jogo em execução.
- `TPointBlankDailyState` → estado diário (quantas sessões, quantos crashes).
- `TPointBlankExitInfo` → captura código de saída do processo do jogo.
- `TPointBlankWaitThread` → thread que espera o jogo terminar.
- `TPointBlankCrashVerificationTask` / `TPointBlankCrashVerifierThread`
  / `TPointBlankCrashVerificationResult` → verifica se o encerramento foi
  um crash e classifica.

Com mais de 2 encerramentos no dia, `FUN_1359f8b0` publica **uma**
notificação por dia (chave `"pb-crash-"` + `yyyymmdd`, título
`"O PointBlank fechou várias vezes"`), com os textos:

- `"%d encerramentos inesperados hoje. Veja como reparar."`
- `"Detectamos %d encerramentos inesperados do PointBlank hoje. Abra esta notificação para ver as recomendações de reparação."`

### 2.5. Eleva a prioridade do processo do jogo

Funções do módulo de manutenção (`ponto_blank.c` §3–§6), todas sobre o
processo do Point Blank:

| Função | Endereço | O que faz |
|---|---|---|
| `pb_elevar_prioridade_cpu` | `0x135a4dc4` | Guarda a classe atual; se não for `ABOVE_NORMAL`/`HIGH`/`REALTIME`, aplica `ABOVE_NORMAL` |
| `pb_elevar_priority_boost` | `0x135a6bdc` | Se o boost dinâmico estiver desligado, religa (`SetProcessPriorityBoost(h, FALSE)`) |
| `pb_elevar_prioridade_gpu` | `0x135a56ec` | `D3DKMTSetProcessSchedulingPriorityClass`: se a classe atual for ≤ `NORMAL` (2), pede `ABOVE_NORMAL` (3) |
| `pb_configurar_timer_resolution` | `0x135a50d8` | `SetProcessInformation` classe 4 (`ProcessPowerThrottling`): desliga EcoQoS e a política de timer (ver 2.9) |

Strings de log: `"GetPriorityClass"`, `"SetPriorityClass.AboveNormal"`,
`"Maintain.GetProcessPriorityBoost"`, `"Maintain.SetProcessPriorityBoost.Enable"`,
`"D3DKMTSetProcessSchedulingPriorityClass.AboveNormal"`.

**O MMCSS não entra aqui.** `AvSetMmThreadCharacteristicsW` ("Pro Audio" →
"Games" → "Playback") e `AvSetMmThreadPriority` são aplicados à thread
`ReetTimerPrecision` **do próprio ReetFPS** (`0x1357c4d0`, chamada pelo
Execute em `0x1357c88c`), não ao processo do jogo.

### 2.6. "Pausar/retomar" o Windows Update enquanto você joga

Strings:

- `"Removendo a pausa do Windows Update..."`
- `"Não foi possível gravar a pausa. Execute o ReetFPS como administrador."`
- `"Não foi possível remover a pausa. Execute o ReetFPS como administrador."`

O ReetFPS adia o Windows Update para evitar que ele consuma CPU/disco durante
a partida. **INFERIDO:** implementado via registros de política do WU (parte do
catálogo `OTHER` em `catalogo_comandos.md`).

### 2.7. Gerenciamento inteligente (ativado por usuário)

String:

- `"Gestão Inteligente ativada. O ReetFPS passa a gerenciar o desempenho do Point Blank automaticamente."`

Quando ligado, o ReetFPS roda o `TPointBlankStabilityMonitor` em segundo plano:
detecta o processo do jogo subir, aplica a prioridade (2.5) e, no
encerramento do jogo, reverte. **INFERIDO:** que também aplique o perfil de
energia do catálogo nesse momento.

### 2.8. Lista de recomendações, FLUIDEZMAX e a tela FPS Game Booster

`FUN_136ec13c` monta a **lista de recomendações** em dois grupos. Cada item tem
chave, rótulo, ícone e descrição (os rótulos abaixo são textos da UI):

| Grupo | Chave | Rótulo | Ícone |
|---|---|---|---|
| Windows | `Energia_ON` | "Windows Turbo +FPS" | bolt |
| Windows | `Hibernate_ON`, `Cortana_ON`, `TarefaTelemetria_ON`, `Superfetch_ON`, `ADMENU_ON`, `OpMouse_ON` | (desativar hibernação, Cortana, telemetria, Superfetch, …; "Otimizar mouse") | — |
| PointBlank | `FLUIDEZMAX` | "Fluidez máxima" | bolt |
| PointBlank | `LOADINGMAP` | "Carregamento de mapa otimizado" | clock |
| PointBlank | `FULLSCREEN` | "Tela cheia otimizada" | fullscreen |
| PointBlank | `OPTIMIZER_PB_MANAGER` | "Otimização Inteligente" | smart |
| PointBlank | `PRIORITYPB` | "Prioridade do PointBlank" | rocket |
| PointBlank | `INTERFACE` | "Interface otimizada" | window |
| PointBlank | `FPS_SELECTION_INDEX` | "FPS recomendado" | speed |
| PointBlank | `REETGAMEMODE` | "Game Mode ReetFPS" | gamepad |

As chaves do grupo PointBlank passam pelo **despachante** em `0x136f78d0`, que
"clica" no botão `*_OFF` do card correspondente da tela **FPS Game Booster**
(form `TGameBooster`). Os nomes vêm da tabela de métodos publicados do form:

| Chave | Handler | Método publicado | Ver |
|---|---|---|---|
| `FLUIDEZMAX` / `FLUIDEZMAXIMA` | `0x1372a03c` | `GRAPHIC_OFFClick` | — (não analisado) |
| `FULLSCREEN` | `0x1372dfb0` | `TELACHEIA_OFFClick` | 2.18 |
| `OPTIMIZER_PB_MANAGER` | `0x13729094` | `MINIMAP_OFFClick` | 2.15 |
| `FPS_SELECTION_INDEX` | `0x137294d8` | `FPSUNLOCKED_OFFClick` | 2.16 |
| `PRIORITYPB` | `0x1372e888` | `PRIORITYPB_OFFClick` | 2.17 |
| `LOADINGMAP` | `0x13728d20` | `MAPLOADING_OFFClick` | 2.14 |
| `INTERFACE` | `0x1372d4b8` | `INTERFACEDELAY_OFFClick` | 2.12 |
| `REETGAMEMODE` | `0x1372c00c` | `ReetFPSSettingsPanel1Categories3Items2ToggleOn` | — (não analisado) |

O binário é assim: a chave `OPTIMIZER_PB_MANAGER` cai no botão `MINIMAP_OFF`.
O motivo do nome não é conhecido.

**Padrão dos cards.** Os handlers `*_OFFClick` dos cards (`INTERFACEDELAY`,
`MAPLOADING`, `MINIMAP`, `TELACHEIA`, `KEYBOARD`, `PRIORITYPB`…) fazem a mesma
coisa:

1. gravam no store JSON de configurações (`FUN_1369b158`) o par
   `<CHAVE> = "ACTIVE"` — por exemplo `MINIMAP`, `LOADINGMAP`, `FULLSCREEN`;
2. trocam os botões do card;
3. chamam `vtable[0x188]` de um controle do overlay (**INFERIDO**: setter
   `Checked`);
4. mostram um card de notificação.

> **Nota:** o binário original ainda tinha, antes do passo 1, uma **validação
> de plano/licença** (sem plano liberado, mostrava "O plano Basic não oferece
> suporte… adquirir o plano Advanced.", `FUN_135fcd18`). Essa checagem foi
> **retirada** desta reconstrução (projeto em homologação, caminho para open
> source): os cards agora funcionam sem gate de licença.

Os `*_ONClick` removem a chave. Ao abrir, `FUN_1369beb0` percorre o JSON e
reaplica cada card ativo (sem mostrar o card). O JSON tem o cabeçalho
`"ReetFPS_CFG": "Configuration file for ReetFPS"` e é **sincronizado com o
servidor** (`setConfig=…&content=<json>` para `reetfps.com/update/server.php`).

Os handlers **não executam comandos nem tocam o jogo**. **INFERIDO:** quem lê
essas chaves e aplica o efeito é o módulo `window.ime` (2.21).

**FLUIDEZMAX** em si (`pb_ativar_fluidezmax`, `0x13700af0`) marca o perfil
como ativo e chama `TReetGameModePanel.ExecuteGameModeActions` (`0x137008d0`).
Essa função copia os itens habilitados de `panel+0x2e0` para um array e os
despacha numa task. **INFERIDO:** que essa lista seja a mesma das recomendações
(os offsets diferem).

### 2.9. Timer Resolution — reduzir latência para 0.5 ms

#### Camada 1 — NtSetTimerResolution (global)

| | |
|---|---|
| API | `NtSetTimerResolution` (ntdll.dll, não documentada), thunk `0x1357c294` |
| Valor | 5000 × 100 ns = **0.5 ms** |
| Fallback | `timeBeginPeriod(1)` **só** se `NtSetTimerResolution` falhar (flag `DAT_1380f238`) |
| Rotina | `0x1357c5a4` (`pb_timer_resolution_aplicar`) |

#### Camada 2 — Thread de manutenção ("ReetTimerPrecision")

| Função | Endereço | Papel |
|---|---|---|
| `pb_timer_resolution_ativar_manutencao` | `0x1357c9b0` | Cria a thread (`TTPWatchdog`, VMT `0x1357c804`) |
| `pb_timer_resolution_parar` | `0x1357ca34` | Para a thread e revoga o timer |
| `pb_timer_resolution_revogar` | `0x1357c5e0` | `NtSetTimerResolution(0, FALSE)`; `timeEndPeriod(1)` só se o fallback foi usado |

Nome interno da thread: `"ReetTimerPrecision"` (`0x1357c870`). Log:
`"Maintain 0.5ms timer for low latency"` (`0x1357c40c`). A thread registra a si
mesma no MMCSS (2.5). **INFERIDO:** que ela reaplique o timer periodicamente.

#### Camada 3 — SetProcessInformation (Windows 11)

`FUN_135a50d8` usa `SetProcessInformation(ProcessPowerThrottling = 4)` no
processo do Point Blank. Cada passo só roda se a flag de configuração
correspondente estiver ligada e se uma leitura prévia (`GetProcessInformation`)
mostrar que ainda é necessário:

| Campo | Valor | Efeito |
|---|---|---|
| `ControlMask` bit EcoQoS | `StateMask` = 0 | Desliga o throttling de execução |
| `ControlMask` bit IGNORE_TIMER_RESOLUTION | `StateMask` = 0 | O processo passa a respeitar o timer global |

### 2.10. Limpeza Inteligente — apaga caches com segurança

Itens (tabela em `0x1366c900`):

| Chave | Alvo |
|---|---|
| `prefetch` | `"%WINDIR%\Prefetch"` (`0x1366ca58`) — conteúdo do diretório |
| `driver_extract_cache` | `%SystemDrive%\NVIDIA\DisplayDriver\*` |

Proteções da varredura (`limpeza_varrer_diretorio`, `0x13674090`):

- Caminhos longos com o prefixo `\\?\` (`FUN_13673dc4`), inclusive no padrão de busca.
- Junções/symlinks (`FILE_ATTRIBUTE_REPARSE_POINT`): remove **só o link**
  (`FUN_13673fb4`), sem entrar nele. Falha apenas se essa remoção falhar.
- Cancelamento verificado no início e a cada entrada.
- Apagar arquivo com tentativa de tomar posse (`FUN_13673ee0`, `FUN_136712f0`).
- Falhas classificadas em negado (`ERROR_ACCESS_DENIED`), em uso
  (`ERROR_SHARING_VIOLATION`) e outras.

Estatísticas (`FUN_13153234`): `+0x5c` negado, `+0x5d` em uso, `+0x5e`
deletado, `+0x60` falhas, `+0x70` arquivos, `+0x78` pastas e `+0x80` bytes.

### 2.11. Miras Customizadas

Classes: `TRPCrosshair` (`0x136945ef`) e o formulário `UDialogCrosshair`
(`0x13696e21`).

| Offset | Campo |
|---|---|
| `+0x310` | comprimento de cada braço |
| `+0x314` | espaçamento do centro ao braço |
| `+0x318` | lado do quadrado central (0 = sem quadrado) |
| `+0x334` | **índice de cor** 1..6 (tabela em `0x1380f738`) |
| `+0x338` | sombra ativa |

Cores (`0x1380f738`): vermelho, verde, violeta, azul, amarelo, branco.

`crosshair_desenhar` (`0x136951fc`) faz duas passadas: a passada 0 é a sombra,
em `0xe6000000`, só com `+0x338`; a passada 1 é o corpo. Cada passada desenha
o contorno do quadrado central e os 4 braços. **INFERIDO:** que esta função
pinte a **pré-visualização**; a sobreposição no jogo é ligada/desligada por
`FUN_13696e3c`. O setter dos parâmetros é `FUN_13694ab8`, com clamp e repaint.

### 2.12. Interface Sem Delay

Os textos decifrados resolvem a dúvida: **"Interface Sem Delay" é o card
`INTERFACEDELAY`** (`0x1372d4b8`), e ele fala do **lobby do jogo**, não do
Windows. Ele grava `INTERFACE = ACTIVE` e `SET_INTERFACE = ACTIVE`, remove
`SET_INTERFACE2` e mostra:

> "A otimização da interface foi ativada com sucesso."
> "Agora, a navegação entre as interfaces do lobby do Point Blank está mais
> rápida e sem delays!"

Nada no `ReetFPS.exe` mexe no lobby; **INFERIDO** que o efeito venha do
`window.ime` (2.21).

Separado disso há um **toggle de transparência do Windows** (`0x136b383c`
desliga e `0x136b35fc` religa, estado `Transparency_ON`): cada um roda 6
`reg add` (transparência, OLED taskbar, miniaturas do DWM, ColorPrevalence).
Os **toasts estão trocados no próprio ReetFPS**: quem desliga a transparência
mostra "ativada".

`VisualFXSetting`: no Windows, 0 = deixar o Windows escolher, 1 = melhor
aparência e 2 = melhor desempenho. O ReetFPS usa 2 no lote "Ajustes de
desempenho" (2.13).

### 2.13. Entrada Instantânea

O binário **não** contém o rótulo "ENTRADA INSTANTÂNEA"; a associação é
**INFERIDA** pelo texto dos toasts.

- **"Ajustes de desempenho aplicados!"** (handler `0x136b0798`): lote de 35
  comandos (`0x135f22f0`) com `VisualFXSetting=2`, `powercfg` (SCHEME_MIN e
  PERFBOOST), parada de serviços (DiagTrack, SysMain, DoSvc…), `MenuShowDelay`,
  timeouts, apps em segundo plano e Windows Update.
- **Game Bar** (`0x136b1f48`): no Windows 7 (retorno 7 da classificação de
  versão) mostra "Essa otimização não é necessária no Windows 7!"; nos demais
  grava `GameBar_ON` e mostra "Game Bar desativada!".
- A tarefa MMCSS **"Low Latency"** (Clock Rate 10000 = 1 ms, GPU Priority 8,
  …) existe só como entrada de um lote de 232 comandos (`TweaksAll`), não como
  função própria.

### 2.14. Mapas Instantâneos

É o item `LOADINGMAP` ("Carregamento de mapa otimizado") → card `MAPLOADING`
(`0x13728d20`). Segue o padrão dos cards (2.8): grava `LOADINGMAP = ACTIVE` e
mostra "A otimização do Loading dos mapas foi ativada com sucesso. Aproveite o
carregamento instantâneo!". A ligação com caches do LanmanWorkstation ou
SysMain, feita em versões anteriores, **não foi comprovada** e foi retirada;
**INFERIDO** que o efeito, se houver, venha do `window.ime` (2.21).

### 2.15. Mini-Map Off

É o card `MINIMAP` da tela FPS Game Booster: `MINIMAP_OFFClick` (`0x13729094`)
e `MINIMAP_ONClick` (`0x13729410`), com os botões em `Self+0x590` e `+0x58c`.

- Ligar grava `MINIMAP = ACTIVE` no JSON, troca os botões, chama
  `vtable[0x188](mgr+0x510, 1)` e mostra o card "O Minimapa foi desativado com
  sucesso! / Prepare-se para um aumento significativo no desempenho do jogo. /
  Aproveite uma experiência mais fluida e responsiva!".
- Desligar remove a chave `MINIMAP`.

Nenhum dos dois escreve no `env_settings.ini`. A chave
`[Game] Enable_MissionIndicator` do `TRPPBConfig` existe, mas **não há prova**
de que este card a use. **INFERIDO:** o minimapa é escondido dentro do jogo
pelo `window.ime` (2.21).

### 2.16. FPS Ilimitado e Desbloqueador de FPS

- **FPS Ilimitado** = form `TUNLOCK_FPS` ("UNLOCKEDFPS" é o nome da *unit*,
  não um valor). Ele escolhe um preset de 1 a 6 (`FUN_1369407c`; fora do
  intervalo vale 3) e grava `FPS_SELECTION_INDEX` no **store JSON** de
  configurações, não no registro nem no INI.
  - Os presets decifrados são **250, 360, 500, 777, 999 e "MAX"**, com as
    legendas "FPS Desbloqueado • 250" … "FPS Desbloqueado • Ilimitado"
    (**INFERIDO**: índice 1..6 nessa ordem; o padrão 500 coincide com o
    índice padrão 3). "FPS Ilimitado" é, portanto, o preset 6.
  - "FPS 486" é só um rótulo de pré-visualização.
  - **Onde o FPS chega ao jogo não foi localizado** no `.exe`. `FPSType`/`FPSVal`
    existem no `TRPPBConfig`, mas nada os liga ao índice escolhido;
    **INFERIDO** que seja o `window.ime` (2.21).
- **Desbloqueador** = botões `FPSUNLOCKED_OFF/ON` (`0x137294d8` / `0x13729524`).
  - Ligar abre um formulário (**INFERIDO**: o de presets).
  - Desligar zera o controle do preset, **remove `FPS_SELECTION_INDEX`** e
    oculta os rótulos.

### 2.17. Impulsionar PointBlank

Corresponde aos botões `PRIORITYPB_OFF/ON` (`0x1372e888` / `0x1372ebb0`),
também chamados pelo item "Prioridade do PointBlank". Não há a palavra
"impulsionar" no binário, mas o card decifrado deixa claro: "O jogo foi
configurado com prioridade máxima! / Isso pode proporcionar um aumento no
desempenho e nos FPS."

- Ligar define a flag global `_DAT_138103f8 = -1`, grava `PRIORITYPB = ACTIVE`
  e mostra o card. Desligar zera a flag e remove a chave.
- Quem lê a flag não foi localizado.
- `FUN_13727a5c` **não** é um reset da tela: as 15 chaves que ela apaga são
  `crosshair1_sizeline` … `crosshair4_color`, ou seja, é o **reset das miras
  personalizadas** (2.11).

### 2.18. Full Screen

É o item `FULLSCREEN` → `TELACHEIA_OFFClick` (`0x1372dfb0`), seguindo o padrão
dos cards: grava `FULLSCREEN = ACTIVE` e mostra **"Tela cheia ativada! / Use F6
para alternar entre tela cheia e janela no Point Blank."** Antes, o handler
seleciona a célula (1,3) da grade e desliga a opção concorrente
**`BORDER_LESS`** (card "O modo Borderless foi ativado com sucesso…"): as duas
são mutuamente exclusivas. O handler **não** toca o `TRPPBConfig` nem o
`ScreenMode` (`+0x4f8`).

O que a string do F6 permite afirmar: a troca entre tela cheia e janela é
feita por uma tecla **dentro do jogo**. O `.exe` não importa `RegisterHotKey`
e nada nele liga o F6 ao `ScreenMode`; **INFERIDO** que o atalho seja tratado
pelo `window.ime` (2.21).

### 2.19. Otimização GPU

- **Lote de ativação** (`0x135f7f88`, 10 comandos): `GPU Priority=8` e
  `Priority=6` em `Tasks\Games`, `SystemResponsiveness=0`, GameDVR/AppCapture
  desligados, `TcpAckFrequency=1`, `TCPNoDelay=1` e `HwSchMode=2` (HAGS). Os
  dois de rede estão na tabela da GPU.
- **Restauração**: espelho em `0x135f8a60`.
- **NVIDIABOOST**: importa o perfil `ReetFPS.nip` com
  `nvidiaProfileInspector.exe -importProfile`. O conteúdo do `.nip` não está
  visível.
- `TGPURegistryWorker` lê os contadores PDH de uso e memória dedicada da GPU.

### 2.20. Teclado de Precisão ("Teclado Turbo")

Os handlers ficam em `0x136b54d4` (ativar) e `0x136b5704` (restaurar). Cada um
grava um estado (valor 1 ou 0) via TRegistry, troca os botões e roda um bloco
de `reg add` em `HKCU\Control Panel\Accessibility\Keyboard Response`:

| Bloco | Comandos |
|---|---|
| Ativar | `Flags=0` (FilterKeys desligado), `AutoRepeatDelay=250`, `AutoRepeatRate=20` |
| Restaurar | `AutoRepeatDelay=300`, `AutoRepeatRate=45`, `BounceTime=0`, `Flags=2` |

O toast é "Teclado Turbo ativado!" e o estado gravado é `OpTeclado_ON` em
`HKCU\Keyboard Layout\ReetFPS`. **Não há** checagem de teclado HID antes dos
comandos, e o MouseKeys pertence a outro lote, de comandos de mouse.

O recurso anunciado como **"TECLADO DE PRECISÃO"**, porém, é outro: o card
`KEYBOARD` da tela FPS Game Booster (`0x1372daf8`), que grava
`KEYBOARD = ACTIVE` e diz:

> "Teclado de Precisão Ativado!"
> "Pressionar teclas opostas como "W/S" e "A/D" agora evita conflitos."
> "Apenas a última tecla pressionada é reconhecida, garantindo movimentos
> mais fluidos e precisos para uma jogabilidade suave."

Ou seja: entre duas teclas opostas, vale a última pressionada (o
comportamento conhecido como SOCD). Nada no `.exe` ligado a essa chave mexe no
teclado; **INFERIDO** que o `window.ime` faça isso dentro do jogo (2.21).

### 2.21. Como as features chegam ao jogo: `ReetFPS.exe` → `ReetFPS.dll` → `window.ime`

Em linguagem simples (detalhes e endereços na seção 24 do `ponto_blank.c`):

1. **O `ReetFPS.exe` baixa uma biblioteca do servidor.** Uma tarefa em segundo
   plano (`cLoadLibrary.Initialize`) busca
   `https://reetfps.com/update/lib_update.php?index=2`, abre um processo com
   acesso total e carrega a biblioteca direto na memória dele, sem gravar
   arquivo. **INFERIDO:** que essa biblioteca seja a `ReetFPS.dll` da pasta de
   instalação.
2. **A `ReetFPS.dll` espera o jogo.** A cada segundo ela procura o
   `PointBlank.exe` e a janela dele.
3. **Antes de agir, confere uma senha temporária.** Lê
   `HKCU\Control Panel\Desktop\Colors\WindowMsg` e compara com o PID do
   processo × 1482301; o valor não existe no registro fora de execução.
4. **Injeta o `window.ime` no jogo.** Registra `%System%\window.ime` como um
   layout de teclado, pede à janela do jogo para trocar de idioma — o que faz o
   Windows carregar o IME **dentro** do Point Blank — e depois desfaz o layout e
   apaga o rastro em `HKCU\Keyboard Layout\Preload`. Por fim confere se o
   módulo está carregado.
5. **O `window.ime` não está no disco.** Ele não existe em System32,
   SysWOW64, Temp nem AppData. **INFERIDO:** vem do servidor na inicialização.

Consequência: o que cada feature do painel faz dentro do jogo (minimapa,
F6, teclado, lobby, FPS, carregamento de mapa) **não pode ser verificado** com
os arquivos disponíveis. O que está comprovado é o contrato visível — as
chaves gravadas no JSON (2.22) — e o mecanismo de entrega acima.

Também foram vistos dois mecanismos que dificultam a análise: a DLL restaura
o início da função de instalação de IME antes de chamá-la, e a biblioteca é
carregada da memória, sem arquivo.

### 2.22. Chaves do painel (decifradas)

O que cada botão grava no JSON de configurações (`<CHAVE> = "ACTIVE"`, salvo
indicação):

| Feature anunciada | Card / handler | Chave(s) |
|---|---|---|
| Mini-Map Off | `MINIMAP` (`0x13729094`) | `MINIMAP` |
| Mapas Instantâneos | `MAPLOADING` (`0x13728d20`) | `LOADINGMAP` |
| Full Screen | `TELACHEIA` (`0x1372dfb0`) | `FULLSCREEN` (remove `BORDER_LESS`) |
| Interface Sem Delay | `INTERFACEDELAY` (`0x1372d4b8`) | `INTERFACE`, `SET_INTERFACE` (remove `SET_INTERFACE2`) |
| Teclado de Precisão | card `KEYBOARD` (`0x1372daf8`) | `KEYBOARD` |
| Impulsionar PointBlank | `PRIORITYPB` (`0x1372e888`) | `PRIORITYPB` |
| Fluidez Máxima | `0x1372a03c` | `FLUIDEZMAX` |
| FPS Ilimitado / Desbloqueador | form `TUNLOCK_FPS` / `FPSUNLOCKED` | `FPS_SELECTION_INDEX` = índice 1..6 |
| Miras Customizadas | form da mira | `crosshair1_…` a `crosshair4_…`, `current_crosshair`, `crosshair_status` |
| Outros cards | `COUNTERPING`, `REETSTATS`, `HUDPLAYERS`, `FPSCOUNTER`, `ENABLE_RANK`, `ENABLE_INTERACTIVE`, `BORDER_LESS`, `REETGAMEMODE`, `WEAPON_LEFTY`, `SENSI` | mesmo nome do card |

Os textos completos dos cards estão em `strings_decifradas.md`.

### 2.23. Nota de segurança: o "segredo" `TROQUE_ESSE_SEGREDO…`

O `.exe` contém, cifrado, o texto `TROQUE_ESSE_SEGREDO_GRANDE_AQUI_64_CHARS_MINIMO`
(chamada do decodificador em `0x135965d8`, blob `0x13596e44`). Ele **não** é
um segredo esquecido em uso. A rotina que assina as requisições à API (campos
`username`, `mode`, `ts`, `nonce`, `sig`; User-Agent `ReetFPS-Client/1.0`)
compara o segredo configurado com **vazio** e com esse texto, e **se for
igual a qualquer um dos dois aborta** com o código `0x899994` antes de
assinar. Ou seja, o placeholder serve de **trava contra um build sem o segredo
real**. De onde vem o segredo real não foi rastreado. Uma pesquisa anterior
afirmou que o segredo-padrão "não foi trocado" — isso estava **errado**.

Dois pontos de privacidade, estes sim comprovados: as configurações do
usuário (o JSON acima) são **enviadas ao servidor** junto com o
`sessionID`, e a licença é **vinculada ao hardware** ("Sua licença está
vinculada a outro computador…", "Este dispositivo já está vinculado a outra
conta…").

---

## 3. Resumo em uma linha

> Com o processo do jogo, o ReetFPS **acha a instalação, inicia via
> PBLauncher, eleva a prioridade de CPU/GPU e desliga o EcoQoS, monitora
> crashes e o encerra por `taskkill` sob demanda**. Os ajustes de Windows
> (catálogo de 559 comandos e lotes como "Ajustes de desempenho", "Teclado
> Turbo", transparência e GPU) rodam no sistema. As features da tela FPS Game
> Booster só gravam chaves (`MINIMAP=ACTIVE` etc.) num JSON sincronizado com o
> servidor; o efeito dentro do jogo vem de um módulo `window.ime`, injetado no
> Point Blank pela `ReetFPS.dll` e **ausente do disco**.

## 4. Nota sobre reconstrução

Nos imports do executável existem APIs de manipulação de memória de outro
processo (`OpenProcess`, `ReadProcessMemory`, `WriteProcessMemory`,
`VirtualAllocEx`, `CreateRemoteThread`) e um grupo de rotinas em
`0x136e5000`–`0x136e7400` que as utiliza. A análise identifica esse grupo
como um **carregador PE em memória** — comportamento dual-use (aparece em
proteções anti-cheat/DRM e em outras categorias de software). Ele é o
primeiro elo da cadeia descrita em 2.21. Tanto ele quanto a injeção por IME
da `ReetFPS.dll` são **descritos** (seção 24 do `ponto_blank.c`), mas
deliberadamente não reconstruídos como código.
