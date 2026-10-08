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
  **não está no disco** — que aplicaria as features do painel. Ver 2.22.

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
| `FLUIDEZMAX` / `FLUIDEZMAXIMA` | `0x1372a03c` | `GRAPHIC_OFFClick` | 2.27 (chave `GRAPHIC` no módulo) |
| `FULLSCREEN` | `0x1372dfb0` | `TELACHEIA_OFFClick` | 2.19 |
| `OPTIMIZER_PB_MANAGER` | `0x13729094` | `MINIMAP_OFFClick` | 2.16 |
| `FPS_SELECTION_INDEX` | `0x137294d8` | `FPSUNLOCKED_OFFClick` | 2.17 |
| `PRIORITYPB` | `0x1372e888` | `PRIORITYPB_OFFClick` | 2.18 |
| `LOADINGMAP` | `0x13728d20` | `MAPLOADING_OFFClick` | 2.15 |
| `INTERFACE` | `0x1372d4b8` | `INTERFACEDELAY_OFFClick` | 2.12 |
| `REETGAMEMODE` | `0x1372c00c` | `ReetFPSSettingsPanel1Categories3Items2ToggleOn` | — (não analisado) |

O binário é assim: a chave `OPTIMIZER_PB_MANAGER` cai no botão `MINIMAP_OFF`.
O motivo do nome não é conhecido.

**Padrão dos cards.** Os handlers `*_OFFClick` dos cards (`INTERFACEDELAY`,
`MAPLOADING`, `MINIMAP`, `TELACHEIA`, `KEYBOARD`, `PRIORITYPB`…) fazem a mesma
coisa:

1. verificam o **plano da licença** — sem plano liberado mostram "O plano
   Basic não oferece suporte para esse serviço específico… é necessário
   adquirir o plano Advanced." (`FUN_135fcd18`). Versões anteriores desta
   documentação diziam "verificam se o jogo está aberto", o que estava errado;
2. gravam no store JSON de configurações (`FUN_1369b158`) o par
   `<CHAVE> = "ACTIVE"` — por exemplo `MINIMAP`, `LOADINGMAP`, `FULLSCREEN`;
3. trocam os botões do card;
4. chamam `vtable[0x188]` de um controle do overlay (**INFERIDO**: setter
   `Checked`);
5. mostram um card de notificação.

Os `*_ONClick` removem a chave. Ao abrir, `FUN_1369beb0` percorre o JSON e
reaplica cada card ativo (sem mostrar o card). O JSON tem o cabeçalho
`"ReetFPS_CFG": "Configuration file for ReetFPS"` e é **sincronizado com o
servidor** (`setConfig=…&content=<json>` para `reetfps.com/update/server.php`).

Os handlers **não executam comandos nem tocam o jogo**. **INFERIDO:** quem lê
essas chaves e aplica o efeito é o módulo `window.ime` (2.22).

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
`window.ime` (2.22).

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

### 2.14. Fonte Personalizada

Quatro chaves no store JSON controlam a feature (seção 25 do `ponto_blank.c`):

| Chave | Endereço do blob | Papel |
|---|---|---|
| `FONTE_PERSONALIZADA` | `0x1372ade9` | estado master (ACTIVE) |
| `FONTE_PERSONALIZADA_INDEX` | `0x1372ae24` | índice da fonte selecionada no painel |
| `FONTE_PERSONALIZADA_VALUE` | `0x1372ae5f` | nome da fonte (ex.: `bahnschrift`) |
| `FONTE_PERSONALIZADA_INITIALIZED` | `0x1372ae9a` | marca se já foi aplicada |

O handler (próximo a `0x1372adxx`, nome de método RTTI não localizado) segue o padrão dos outros cards: grava as 4 chaves no JSON. **O efeito no jogo não passa pelo handler:** a observação ao vivo (2.25) mostrou `Locale\Brazil\Font.ini` sendo escrito com `bahnschrift` às 07:46 — entre a abertura do ReetFPS (07:46) e a do jogo (07:49). Isso indica que uma **rotina de pré-lançamento** lê `FONTE_PERSONALIZADA_VALUE` do JSON e grava o `Font.ini` antes de abrir o PB. O arquivo é a fonte da interface do próprio jogo, não do overlay do ReetFPS.

Aparece na restauração do painel (`FUN_1369beb0 @ 0x1369c999`) ao lado de `COUNTERPING`, `MINIMAP` etc. — confirma que é um card do `TGameBooster`.

### 2.15. Mapas Instantâneos

É o item `LOADINGMAP` ("Carregamento de mapa otimizado") → card `MAPLOADING`
(`0x13728d20`). Segue o padrão dos cards (2.8): grava `LOADINGMAP = ACTIVE` e
mostra "A otimização do Loading dos mapas foi ativada com sucesso. Aproveite o
carregamento instantâneo!". A ligação com caches do LanmanWorkstation ou
SysMain, feita em versões anteriores, **não foi comprovada** e foi retirada;
**INFERIDO** que o efeito, se houver, venha do módulo injetado (2.22).

### 2.16. Mini-Map Off

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
pelo `window.ime` (2.22).

### 2.17. FPS Ilimitado e Desbloqueador de FPS

- **FPS Ilimitado** = form `TUNLOCK_FPS` ("UNLOCKEDFPS" é o nome da *unit*,
  não um valor). Ele escolhe um preset de 1 a 6 (`FUN_1369407c`; fora do
  intervalo vale 3) e grava `FPS_SELECTION_INDEX` no **store JSON** de
  configurações, não no registro nem no INI.
  - Os presets decifrados são **250, 360, 500, 777, 999 e "MAX"**, com as
    legendas "FPS Desbloqueado • 250" … "FPS Desbloqueado • Ilimitado"
    (**INFERIDO**: índice 1..6 nessa ordem; o padrão 500 coincide com o
    índice padrão 3). "FPS Ilimitado" é, portanto, o preset 6.
  - "FPS 486" é só um rótulo de pré-visualização.
  - **Onde o FPS chega ao jogo** não está no `.exe` — e isso se confirmou.
    `FPSType`/`FPSVal` existem no `TRPPBConfig`, mas nada os liga ao índice
    escolhido. **Resolvido em 2.27:** é o módulo injetado que aplica, com a
    chave `FPS_SELECTION_INDEX`, escrevendo um dword em `base + 0x500141`.
    A tabela dá **9999 para qualquer índice de 1 a 6** e 360 fora da faixa —
    ou seja, a distinção entre os seis presets não chega ao jogo por aí.
- **Desbloqueador** = botões `FPSUNLOCKED_OFF/ON` (`0x137294d8` / `0x13729524`).
  - Ligar abre um formulário (**INFERIDO**: o de presets).
  - Desligar zera o controle do preset, **remove `FPS_SELECTION_INDEX`** e
    oculta os rótulos.

### 2.18. Impulsionar PointBlank

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

### 2.19. Full Screen

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
pelo `window.ime` (2.22).

### 2.20. Otimização GPU

- **Lote de ativação** (`0x135f7f88`, 10 comandos): `GPU Priority=8` e
  `Priority=6` em `Tasks\Games`, `SystemResponsiveness=0`, GameDVR/AppCapture
  desligados, `TcpAckFrequency=1`, `TCPNoDelay=1` e `HwSchMode=2` (HAGS). Os
  dois de rede estão na tabela da GPU.
- **Restauração**: espelho em `0x135f8a60`.
- **NVIDIABOOST**: importa o perfil `ReetFPS.nip` com
  `nvidiaProfileInspector.exe -importProfile`. O conteúdo do `.nip` não está
  visível.
- `TGPURegistryWorker` lê os contadores PDH de uso e memória dedicada da GPU.

### 2.21. Teclado de Precisão ("Teclado Turbo")

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
teclado; **INFERIDO** que o `window.ime` faça isso dentro do jogo (2.22).

### 2.22. Como as features chegam ao jogo: `ReetFPS.exe` → `ReetFPS.dll` → `window.ime`

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
   SysWOW64, Temp nem AppData. **INFERIDO:** vem do servidor após o login.

Consequência: o que cada feature do painel faz dentro do jogo (minimapa,
F6, teclado, lobby, FPS, carregamento de mapa) **não pode ser verificado** com
os arquivos disponíveis. O que está comprovado é o contrato visível — as
chaves gravadas no JSON (2.23) — e o mecanismo de entrega acima.

Atualização: o módulo que roda dentro do jogo foi depois capturado da memória
e analisado. A **entrega** está em 2.25 e o **mecanismo de hook** em 2.26. O
que cada feature faz individualmente continua não atribuído.

Também foram vistos dois mecanismos que dificultam a análise: a DLL restaura
o início da função de instalação de IME antes de chamá-la, e a biblioteca é
carregada da memória, sem arquivo.

### 2.23. Chaves do painel (decifradas)

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

### 2.24. Nota de segurança: o "segredo" `TROQUE_ESSE_SEGREDO…`

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

### 2.25. Observação ao vivo (2026-10-07, ReetFPS logado + Point Blank aberto)

Inspeção sem privilégio de administrador, só leitura (processos, disco,
registro, logs). ReetFPS (PID 15040) e dois `PointBlank.exe` rodando, todos
elevados — por isso a lista de módulos do jogo **não** pôde ser lida.

**Confirmado sobre a cadeia de injeção (2.22):**

- `window.ime` continua **ausente** de System32/SysWOW64 com o jogo já aberto,
  `HKCU\Control Panel\Desktop\Colors\WindowMsg` **não existe** e não há layout
  com `Ime File` registrado. Isso bate com a limpeza descrita (o arquivo e as
  marcas só existem durante a injeção). Não prova que a injeção ocorreu.
- O `BC.log` do jogo (07:49:12–07:49:28) não menciona IME/ReetFPS; ele só
  cobre o carregamento. A pasta do jogo tem `CHEAT_BLOCKER\CB.pass`.
- Conexões: ReetFPS → `45.89.30.252:443`; jogo → `201.77.235.220:39190`.

**Novo em relação à análise estática (a versão instalada hoje é mais nova):**

- `ReetHub.exe` (3,5 MB) e `WebView2Loader.dll` na pasta de instalação.
  `%LOCALAPPDATA%\ReetFPS\ReetHub\ReetHubEdgeHost.ini` mostra que é um
  navegador embutido (WebView2) para `https://reethub.com` (voz/comunidade),
  com URL de conexão contendo um token (`/connect/rhc_…`).
- Chaves novas em `HKCU\Keyboard Layout\ReetFPS` ausentes de
  `strings_decifradas.md`: `GAMEAUDIO_*` (atalhos de volume do jogo: mudo
  VK 123 = F12, `+` VK 187, `-` VK 189, passo 5), `REETHUB_PTT_BIND` (VK 75 =
  K), `REETHUB_MIC_BIND` (F11), `REETHUB_SOM_BIND` (F10),
  `REETHUB_VOICE_MODE = voice_activity`, `GAMEPLAY_BIND_ACTIVE`,
  `WEAPON_LEFTY_BIND/STATE`, `SENSI = 0.100`, `FluidezMode = fluidez_maxima`,
  `ReetMonitor` (JSON com cpu/ram/ping/gpu), `GPU_*`, `Param0..3`, `MSG_ID`.
  **INFERIDO:** os atalhos de áudio/voz/sensibilidade são lidos pelo módulo
  dentro do jogo, já que o jogo é que tem o foco do teclado.
- **Reparo do sistema** (o `0x13600598` de §19): gera
  `%TEMP%\REETFPS_system_repair_*.cmd`, que roda DISM CheckHealth, ScanHealth,
  RestoreHealth e `sfc /scannow`, com arquivos de status/cancelamento.
- **Limpeza de cache de GPU**: `ReetFPS.gpu-cache-clean.log` lista
  `D3DSCache`, `NVIDIA\DXCache`, `NVIDIA\GLCache` e `NVIDIA\ComputeCache`;
  arquivos em uso são agendados para exclusão no boot.
- `Locale\Brazil\Font.ini` do jogo foi alterado às 07:46 (entre a abertura do
  ReetFPS e a do jogo) e contém `bahnschrift`. **INFERIDO:** feito pelo ReetFPS.

**Credenciais armazenadas de forma reversível (privacidade):** o valor
`Telemetry` em `HKCU\Keyboard Layout\ReetFPS` é um JSON cifrado só com **XOR
pela chave fixa `REETFPS`** (repetida). Em claro ele contém `STATUS`,
`REETFPS_LOGIN`, **`REETFPS_PASSWORD` (a senha da conta em texto)**,
`BUTTON_TIMER_RESOLUTION_ON`, `ENABLE_INTERACTIVE`, `ENABLE_RANK`,
`SESSION_ID` e `MSG_BOX` (que, decifrado com a mesma chave, é o aviso "O novo
ReetFPS chegou!…"). Qualquer programa rodando como o usuário lê a senha.
`Param1` guarda o login em claro; `Param2` provavelmente a senha com outra
codificação (não verificado).

**`%TEMP%` esvaziado na abertura do jogo:** ao reabrir o Point Blank
(08:20:19), o `%TEMP%` do usuário ficou com 3 itens. Sumiram inclusive os
`REETFPS_system_repair_*` do próprio ReetFPS e a pasta de trabalho de outro
programa. **INFERIDO:** é a Limpeza (2.10) disparada junto com o jogo; o
horário bate, mas a autoria não foi comprovada.

**Tentativa de capturar o `window.ime` (resultado negativo):**

- Um `FileSystemWatcher` em System32 e SysWOW64 (`captura_ime\`) ficou ativo
  durante **duas** reaberturas do jogo (08:24:55 e 08:32:52). **Nenhum**
  arquivo `.ime` foi criado, alterado ou renomeado. O log registraria o evento
  mesmo se a cópia falhasse.
- Os módulos foram listados como administrador, num PowerShell 32-bit
  (`captura_ime\modulos.txt`):
  - `PointBlank.exe` (2 processos, 160 e 119 módulos): só módulos do Windows,
    da NVIDIA, do próprio jogo (PhysX, CEF, fmod, CrashTrace) e o anti-cheat
    `CHEAT_BLOCKER\CB.cbm`. **Sem `window.ime` e sem `ReetFPS.dll`.**
  - `ReetFPS.exe`: só o próprio exe, o OpenSSL e DLLs do Windows/Defender.
    Ele **não** carrega a `ReetFPS.dll` como módulo. A base é `0x12840000`,
    diferente da `0x13140000` analisada (ASLR ou build novo).
  - Nenhum processo 32-bit acessível tem `ReetFPS.dll` ou `window.ime` na
    lista de módulos.
- Conclusão: nesta versão e nesta conta, o caminho
  `ReetFPS.dll → window.ime por IME` **não foi observado**. Restam duas
  hipóteses: (a) o código é mapeado manualmente na memória, o que não aparece
  em listas de módulos; ou (b) as features dentro do jogo não estão sendo
  entregues (plano, servidor ou versão).
- **Teste prático do usuário: todas as funções do painel funcionam no jogo**
  (conta com acesso completo). Isso descarta (b). Como o `env_settings.ini`
  não muda desde 21/09/2026, as funções também não passam por ele.
- Hipóteses que sobram: (a) código mapeado na memória do jogo; ou (c) o
  `ReetFPS.exe` aplica as funções **de fora** do jogo. Por (c): hook global
  de teclado para o SOCD e os atalhos; API de sessões de áudio para o volume;
  estilo/posição da janela para F6/borderless; e `WriteProcessMemory`, que o
  exe importa, para minimapa/FPS/lobby. **Não verificado.**
- **Teclado de Precisão fora do jogo (Bloco de Notas):** com A segurado,
  apertar e soltar D não fez o A voltar a repetir, que é o comportamento
  normal do Windows. O SOCD **não** é global: ou roda dentro do jogo, ou só
  age com o jogo em foco.
- **Handles do `ReetFPS.exe`** (`handle64 -a`, admin, 09:07,
  `captura_ime\handles.txt`):
  - **3 handles de processo** para o `PointBlank.exe` principal (PID 20388,
    o de 160 módulos). Nenhum para o segundo processo (22372).
  - **222 handles de thread por processo do jogo**, para **todos os 8**
    processos `PointBlank` abertos desde 07:49, inclusive os já encerrados.
    No jogo atual, os 222 são de threads distintas e **nenhuma ainda está
    viva**; nenhuma das 90 threads vivas está aberta. Ou seja: o ReetFPS abre
    as threads que o jogo cria no início e **nunca fecha os handles**
    (vazamento de ~222 handles por abertura do jogo).
  - Abrir threads do jogo logo na partida é compatível tanto com ajuste de
    prioridade por thread quanto com execução de código por sequestro de
    thread. As **permissões** desses handles decidem entre os dois
    (`captura_ime\permissoes_handles.ps1`).
  - Também têm handle de processo para o jogo: 3 `svchost` e o `audiodg`
    (normal).
- **Permissões** (`handle64 -a -g -v`, 09:38, `captura_ime\permissoes.txt`):
  - Os 3 handles de processo para o jogo: `SYNCHRONIZE|QUERY_LIMITED_INFORMATION`,
    `QUERY_LIMITED_INFORMATION` e `SYNCHRONIZE|SET_INFORMATION|QUERY_LIMITED_INFORMATION`.
    **Nenhum tem `VM_READ`/`VM_WRITE`/`VM_OPERATION`/`CREATE_THREAD`**: com
    estes handles o ReetFPS não lê nem escreve a memória do jogo. Eles servem
    exatamente ao que §2.5/§2.9 descrevem: esperar o fim do processo
    (`SYNCHRONIZE`), prioridade e `SetProcessInformation` (`SET_INFORMATION`).
  - Os 1776 handles de thread do jogo (8 aberturas × 222) têm
    **`THREAD_ALL_ACCESS`**, que inclui suspender e trocar o contexto da
    thread. Isso é compatível com sequestro de thread, mas também com o
    hábito comum de abrir threads "com tudo" só para ajustar prioridade. Os
    handles **não mostram o uso**.
  - Limite: é uma foto tirada minutos depois da abertura. Um handle com
    escrita de memória usado só na inicialização e fechado em seguida não
    apareceria (`captura_ime\monitorar_abertura.ps1` cobre esse caso).
- **Abertura do jogo monitorada** (fotos a cada ~355 ms, 09:42:13–09:44:44,
  `captura_ime\abertura.txt`). Os dois processos do jogo nasceram às
  09:42:32 (PID 13704) e 09:42:41 (PID 15308):

  | Hora | Evento no `ReetFPS.exe` |
  |---|---|
  | 09:42:33.95 | ~1,9 s após o jogo nascer: abre **`PROCESS_ALL_ACCESS`** para o PID 13704, mais os handles de consulta/prioridade já conhecidos |
  | 09:42:34.94–36.68 | abre **222 threads** do jogo, todas com `THREAD_ALL_ACCESS`, em ~1,7 s |
  | até 09:42:36.44 | **fecha** o handle `PROCESS_ALL_ACCESS` (vida de ~2,5 s) |
  | 09:42:42.61 | ~1,6 s após o 2º processo: o mesmo padrão no PID 15308 (`PROCESS_ALL_ACCESS`, +222 threads, fechado até 09:42:45.43) |

  `PROCESS_ALL_ACCESS` inclui `VM_OPERATION`, `VM_WRITE` e `CREATE_THREAD`.
  A sequência (acesso total ao processo + abrir todas as threads + fechar o
  handle de processo logo em seguida, mantendo os de thread) é o perfil de
  uma **carga de código na memória do jogo durante a inicialização**. Isso bate
  com o que a análise estática viu em `cLoadLibrary.Initialize` (2.22, item
  1): "abre um processo com acesso total e carrega a biblioteca direto na
  memória dele". **Conclusão provável:** esse "outro processo" é o próprio
  `PointBlank.exe`, e é por ali que as funções do painel chegam ao jogo. O
  caminho `ReetFPS.dll → window.ime` não é usado nesta versão.
  **Ainda INFERIDO:** os handles mostram a capacidade e o momento, não a
  escrita em si. A confirmação seria achar no jogo uma região de memória
  privada executável que não pertence a nenhum módulo.
- **Mapa de memória do jogo** (`captura_ime\mapa_memoria.ps1`, admin, só
  `VirtualQueryEx` com `PROCESS_QUERY_INFORMATION`, sem ler conteúdo). Foram
  duas coletas: `memoria_com_reetfps.txt` (11:14, jogo aberto às 09:42 com o
  ReetFPS) e `memoria_sem_reetfps.txt` (11:20, jogo aberto às 11:15 só pelo
  PBLauncher, sem nenhum `ReetFPS.exe`).

  | | Com ReetFPS (PID 13704 / 15308) | Sem ReetFPS (PID 15696 / 13364) |
  |---|---|---|
  | Módulos com arquivo | os mesmos (jogo, CEF, PhysX, NVIDIA, Windows, `CB.cbm`) | os mesmos |
  | Alocações de código sem arquivo | 987 / 943 | 43 / 7 |
  | Alocação grande de código sem arquivo | **`0x20E10000` / `0x20FD0000`: 1.818.624 bytes, `PRIVATE`, alocada `RWX`, layout `RWX:1.765.376 R:4.096 RWX:49.152`** | **nenhuma** |
  | Outras alocações acima de 4 KB | 2 blocos `RW` grandes com uma página `RWX`; 2 de 8 KB `RX` | 1 bloco `RW` de ~1 MB com uma página `RWX` (o mesmo padrão existe com o ReetFPS) |

  - O bloco de **~1,73 MB de código sem arquivo**, com o **mesmo tamanho e o
    mesmo layout nos dois processos do jogo**, só existe com o ReetFPS. Os
    dois processos são justamente os que receberam o `PROCESS_ALL_ACCESS` e as
    222 threads na abertura. O formato é de imagem PE mapeada à mão: uma área
    única alocada `RWX`, com uma página trocada para só leitura no meio. Um
    JIT de navegador ou de script não teria o mesmo tamanho exato nos dois
    processos. **Conclusão: o ReetFPS carrega um módulo de ~1,8 MB na memória
    do `PointBlank.exe` na abertura, sem arquivo em disco (cLoadLibrary,
    2.22 item 1).** É por aí que as funções do painel chegam ao jogo nesta
    versão. Quanto ao `window.ime` (2.22 itens 2–3), ou deixou de ser usado,
    ou passou a ser carregado também da memória.
  - As centenas de páginas soltas de 4 KB `RWX`/`RX` a mais na coleta com o
    ReetFPS combinam com trampolins de hook, mas **não foram atribuídas**.
    O jogo da coleta com o ReetFPS estava aberto havia ~1h30, contra ~5 min
    na outra, e o tempo de uso também pode gerar páginas assim (anti-cheat,
    CEF).
  - **Não verificado:** que o bloco seja a biblioteca de
    `lib_update.php?index=2`. O conteúdo não foi lido, de propósito.
- **Leitura do bloco** (`captura_ime\ler_bloco.ps1`, admin, `PROCESS_VM_READ`,
  12:04, PIDs 13636 e 20108, jogo aberto às 12:02). O bloco foi salvo em
  `captura_ime\bloco_PID13636_0x20010000.bin` e `bloco_PID20108_0x201D0000.bin`.
  Cabeçalho PE verificado em ambos — idênticos:

  | Campo | Valor |
  |---|---|
  | Assinatura | `MZ` / `PE\0\0` — PE válido |
  | Arquitetura | x86 (32-bit) |
  | Subsystem | 3 — DLL |
  | ImageBase | **`0x10000000`** |
  | SizeOfImage | `0x001BC000` (1.818.624 bytes) |
  | Timestamp | **`0x6ABFEDBA` — 02/10/2026 14:45:30** (5 dias antes da coleta) |
  | Seções | `.text` `.rdata` `.data` `.fptable` `.rsrc` `.reloc` |

  - **ImageBase `0x10000000` é exatamente a da `ReetFPS.dll` analisada
    estaticamente** (README.md, linha 131). É a mesma DLL, versão nova
    compilada em 02/10/2026. O carregador a rebasa para o endereço livre no
    jogo (`0x20010000` / `0x201D0000`) porque `0x10000000` já está ocupado.
  - A seção `.fptable` (não-padrão, RVA `0x1AF000`, 512 bytes, `RW`) não
    existia na versão analisada antes — provavelmente uma tabela de ponteiros
    de função usada internamente.
  - O timestamp confirma que o servidor entrega versões atualizadas
    (`lib_update.php?index=2`): a DLL em disco é mais antiga que a carregada.
  - **Conclusão definitiva:** o ReetFPS injeta a `ReetFPS.dll` diretamente na
    memória do `PointBlank.exe`, sem gravá-la em disco, via `cLoadLibrary`
    (2.22 item 1). O caminho `window.ime` (2.22 itens 2–3) **não é usado
    nesta versão**. O bloco foi lido sem disparar o anti-cheat.
- **Strings e assets da DLL nova** (`captura_ime\extrair_strings.ps1` +
  análise em PS, 14:51). Binário: `bloco_PID20108_0x201D0000.bin`.

  **Cifra de strings:** a versão nova mudou o esquema da versão estática
  (bytes atribuídos byte a byte na pilha). Agora as strings cifradas ficam
  armazenadas em `.rdata`, com **o byte-chave imediatamente antes da string
  XOR'd** (padrão: `[key] [enc_1] [enc_2] … [enc_n] [00]`). Confirmado:
  byte `0x1D` em `rva=0x139A40`, seguido de `PointBlank.exe` XOR `0x1D`.

  **Strings encontradas:**

  | Método | RVA | Resultado |
  |---|---|---|
  | KNOWN_XOR1 (`chave_antes=True`) | `0x139A41` `.rdata` | `PointBlank.exe` (key `0x1D`) |
  | KNOWN_RAW | `0x1ACB9E` gap entre `.data` e `.fptable` | caminho completo `C:\Zepetto\PointBlank\PointBlank.exe` (em estrutura com timestamps) |

  - As strings da versão estática `window.ime`, `WindowMsg`, `1482301`
    (multiplicador do PID) e `Control Panel\Desktop\Colors\` **não foram
    encontradas** — nem em claro nem em nenhuma chave XOR1. Confirma que o
    caminho de IME foi completamente removido nesta versão.
  - A maioria do `.rdata` (0x100000–0x12FFFF) é MSVC CRT linkado
    estaticamente (mensagens de erro, tabelas de locale, nomes de funções
    matemáticas, mangling de C++). Isso indica compilador MSVC, consistente
    com as seções padrão e o timestamp.

  **Análise do interior da DLL (`captura_ime\buscar_features.ps1`, 2026-10-07 15:56):**

  A busca no `.bin` revelou a arquitetura interna da DLL nova:

  | Achado | RVA | Significado |
  |---|---|---|
  | `Dear ImGui 1.92.0 WIP (19193)` | `0x10B348` | Framework de overlay — toda a UI dentro do jogo é ImGui |
  | HLSL vertex + pixel shader | `0x1370C8` | Fullscreen-triangle D3D11 para renderização de texturas |
  | `D3D11CreateDeviceAndSwapChain` | IAT | Suporte D3D11 (PointBlank pode usar D3D11) |
  | `d3d9.dll` + `D3DXCreateTextureFromFileInMemoryEx` | IAT | Carrega os 86 PNGs como texturas D3D9 |
  | Lista de ~45 mapas PB | `0x136D80` | Midnight Zone, Training Camp, Kick Point… — para exibir minimap por mapa |
  | `WinHTTP/1.0 POST` + `Content-Type: application/json` | `0x136760` (WIDE) | HTTP próprio da DLL para buscar configuração do servidor |
  | nlohmann/json 3.12.0 | RTTI `.data` | Parser JSON próprio (hash `961c151d`) |
  | `NOTIFY_GLOBAL` / `NOTIFY_USER` | `0x137260` | Tipos de notificação IPC com o `ReetFPS.exe` |
  | `ImmSetCandidateWindow` / `ImmSetCompositionWindow` | IAT | Vestigial do caminho antigo via `window.ime` |
  | `\Processor(_Total)\% Processor Time` | WIDE | Contador PDH para CPU (usado no FPS Counter) |

  - **Por que `LOADINGMAP`, `INTERFACE`, `SET_INTERFACE` não aparecem na DLL:**
    ~~a DLL não lê o JSON de configuração do disco. Ela tem seu próprio cliente
    HTTP (`WinHTTP/1.0 POST` com JSON) e busca a configuração ativa diretamente
    do servidor com o token da sessão.~~ **CORRIGIDO em 2.27:** a primeira parte
    está certa (a DLL não lê o JSON do disco), mas a explicação estava errada.
    As chaves *existem* na DLL — estão cifradas, e por isso a busca por texto
    não as achou. A configuração chega por **mensagens de janela** vindas do
    `ReetFPS.exe`, não por HTTP. O `NOTIFY_GLOBAL`/`NOTIFY_USER` é
    provavelmente o mecanismo pelo qual o `ReetFPS.exe` notifica a DLL quando
    o usuário ativa/desativa uma feature no painel.
  - **Rendering pipeline confirmado:** a DLL injeta um loop ImGui dentro do
    PointBlank usando os hooks de D3D9 ou D3D11 (`Present`/`EndScene`). As
    texturas PNG da seção `.data` são carregadas com
    `D3DXCreateTextureFromFileInMemoryEx` e renderizadas pelo ImGui como
    sprites do overlay (minimap, avatares, ícones de features).
  - ~~As chaves `LOADINGMAP` e `INTERFACE` são usadas **apenas no
    `ReetFPS.exe`** para gravar o JSON sincronizado com o servidor. O módulo
    injetado lê a config via HTTP, de forma independente.~~ **REFUTADO em
    2.27:** `LOADINGMAP` e `INTERFACE` são chaves do **próprio módulo
    injetado** (cifradas no binário), lidas do painel por `FindWindowExA` +
    `SendMessageA`.

  **Assets PNG embutidos (`captura_ime\extrair_pngs.ps1`, `pngs\`):**
  - **86 imagens PNG** (assinatura `\x89PNG`) extraídas do binário. 85 em
    `.data` (RVA `0x150D08`–`0x1AA08B`) + 1 em `.rdata` (RVA `0x120538`).
    Total ~395 KB. Indice completo em `pngs\indice.txt`.
  - A presença de PNG em `.data` / `.rdata` (não em `.rsrc`) indica que a
    DLL carrega as imagens diretamente da própria memória, sem passar pelo
    sistema de recursos do Windows.
  - **Conteúdo identificado por grupo de resolução:**

    | Resolução | Qtd. | Conteúdo observado |
    |---|---|---|
    | 321×71 | 1 | Fundo branco (banner/cabeçalho do overlay) — `.rdata` |
    | 80×80 | ~70 | **Retratos de operadores** (personagens jogáveis do Point Blank, fundos neutros escuros) — usados como avatares de jogadores no minimapa |
    | 96×96 | 5 | PNG_078+083: círculo preto com "+" branco (**crosshair**; dois estados, ex: normal/hover); PNG_038: raio (**boost/speed**); PNG_008+047: transparentes (estados inativos) |
    | 100×100 | 4 | Ícones das features do painel: PNG_019 caveira (**kill tracker**), PNG_034 mira+silhuetas (**radar/squad**), PNG_062 cabeça+mira (**headshot indicator**), PNG_084 gráfico de barras+seta (**FPS stats**) |
    | 30×30 | 5 | PNG_033: texto "**K/D**" (kill/death label); PNG_014+022+032+042: transparentes |
    | 42×44 | 1 | **Logo ReetFPS** (letras "R" em rosa/magenta) — `pngs\PNG_069.png` |

  - Os ~70 retratos de operadores cobrem os skins disponíveis no ponto do
    build (02/10/2026) e são o componente visual central do minimapa injetado.
    Os quatro ícones 100×100 correspondem visualmente às features anunciadas
    no painel (minimapa, contador de KD, FPS stats, radar de equipe).

### 2.26. Como o módulo injetado engancha no jogo (Ghidra, 2026-10-08)

Análise do `captura_ime\bloco_PID20108_0x201D0000_GHIDRA.bin` (a cópia com
cabeçalhos PE corrigidos por `fixar_pe_para_ghidra.ps1`) no Ghidra 12.1.3 via
GhidraMCP headless. As 7 seções lidas batem exatamente com o `bloco_info.txt`.

**Preparação necessária:** importado em `0x10000000` e depois **rebaseado para
`0x201D0000`**. Sem o rebase, as chamadas indiretas ficam ilegíveis — o dump foi
capturado com as relocações já aplicadas para a base real. A mesma função antes
e depois mostra o efeito:

```c
// base 0x10000000                  // base 0x201D0000
(*_DAT_202d00e8)(&local_10);        GetSystemTimeAsFileTime(&local_10);
uVar1 = (*_DAT_202d0214)();         DVar1 = GetCurrentThreadId();
```

Depois do rebase a análise automática achou **5.601 funções** (eram 2.737 na
base errada). Os endereços abaixo são da base `0x201D0000`, com o RVA ao lado
porque é o que se mantém entre carregamentos.

#### O mecanismo: breakpoints de hardware + VEH

Três funções vizinhas formam o conjunto:

| Função | RVA | Papel |
|---|---|---|
| `FUN_20227da0` | `0x57DA0` | Inicializador: registra o VEH e chama o armador |
| `FUN_20227c70` | `0x57C70` | Arma os 4 breakpoints de hardware nas threads |
| `FUN_20226e10` | `0x56E10` | O handler VEH que recebe as exceções |

O inicializador faz `AddVectoredExceptionHandler(0, FUN_20226e10)` e chama
`FUN_20227c70`. O armador, decompilado:

```c
hObject = CreateToolhelp32Snapshot(4, 0);          // 4 = TH32CS_SNAPTHREAD
Thread32First(hObject, local_2f0);
do {
  if ((local_2e4 == GetCurrentProcessId()) &&       // só o próprio processo
      (local_2e8 != GetCurrentThreadId()) &&        // menos a própria thread
      (hThread = OpenThread(0x1a, 0, local_2e8), hThread != 0)) {
    local_2d4.ContextFlags = 0x10010;               // CONTEXT_DEBUG_REGISTERS
    SuspendThread(hThread);
    if (GetThreadContext(hThread, &local_2d4) != 0) {
      local_2d4.Dr0 = param_1;  local_2d4.Dr1 = param_2;
      local_2d4.Dr2 = param_3;  local_2d4.Dr3 = param_4;
      local_2d4.Dr7 = 0x55;
      SetThreadContext(hThread, &local_2d4);
    }
    ResumeThread(hThread);  CloseHandle(hThread);
  }
} while (Thread32Next(hObject, local_2f0) != 0);
```

Leitura dos valores: `OpenThread(0x1a)` =
`SUSPEND_RESUME|GET_CONTEXT|SET_CONTEXT` (sem acesso de escrita de memória);
`ContextFlags 0x10010` = `CONTEXT_DEBUG_REGISTERS|CONTEXT_i386`; e
**`Dr7 = 0x55`** = `0b01010101`, que liga os quatro breakpoints em modo local
(L0–L3), com os campos R/W e LEN zerados — ou seja **execução, 1 byte**.

**Consequência: o módulo não escreve nenhum byte no código do jogo.** Não há
patch inline, trampolim nem patch de IAT. Ele usa os quatro registradores de
debug do processador e intercepta pelo VEH. Isso é coerente com a ausência
total de `WriteProcessMemory`, `CreateRemoteThread` e `OpenProcess` nos 221
imports do módulo — todo o conjunto é **dentro do próprio processo**.

**INFERIDO:** a escolha por breakpoint de hardware é o jeito conhecido de
enganchar sem alterar bytes, o que passa por verificação de integridade que
compara o código com o disco (o jogo tem `CHEAT_BLOCKER\CB.cbm`). O ganho é
exatamente esse; nenhuma string ou lógica no módulo menciona anti-cheat.

#### Os quatro alvos

O handler compara `ExceptionAddress` (campo `+0xc` do `EXCEPTION_RECORD`)
contra quatro pares base+RVA distintos:

| Global da base | RVA do global | RVA alvo |
|---|---|---|
| `DAT_2037e440` | `0x1AE440` | `0x170490` |
| `DAT_2037e448` | `0x1AE448` | `0x152150` |
| `DAT_2037e41c` | `0x1AE41C` | `0x20C991` |
| `DAT_2037e484` | `0x1AE484` | `0x27141` |

Os RVAs alvo **não existem como constantes no binário**: são montados byte a
byte na pilha como texto ASCII (`"0x170490"`) e convertidos em número em tempo
de execução por `FUN_20214990` (RVA `0x44990`). É um terceiro esquema de
ofuscação, somado aos dois já descritos em 2.25 — e explica por que uma busca
por constantes não acha os alvos. O inicializador também tem um laço de
decifragem **XOR `0x6b`** sobre 0x80 bytes na pilha, imediatamente antes de
registrar o VEH.

São quatro globais de base **diferentes**, o que indica alvos em até quatro
módulos distintos, resolvidos em tempo de execução. A resolução usa DbgHelp:
`SymInitialize` + `SymLoadModuleEx` em `FUN_202439f0` (RVA `0x739F0`) e
`SymFromName` em `FUN_20243960` (RVA `0x73960`) — ou seja, **por nome de
símbolo, não por offset fixo**, o que sobrevive a atualização do jogo.

**Limite importante — os hooks não estavam armados na captura:** os quatro
globais de base estão **todos a zero** no dump. O bloco foi lido às 12:04 com
o jogo aberto às 12:02, cerca de dois minutos antes. Então o mecanismo está
provado **pelo código**, mas não estava ativo naquele instante, e **não é
possível dizer quais funções os quatro RVAs endereçam** — faltam as bases. A
região em volta (`0x1AE400`–`0x1AE4A0`) é uma estrutura com contadores
pequenos, quase toda zerada; fica no mesmo vão entre `.data` e `.fptable` onde
2.25 achou o caminho do `PointBlank.exe`.

#### Isso responde duas perguntas que ficaram abertas

- **A hipótese (a) de 2.25 está confirmada por conteúdo**, não só por formato:
  o bloco é um PE válido com código coerente, mapeado à mão na memória
  privada executável do jogo. O `ler_bloco.ps1` filtra justamente
  `MEM_PRIVATE` com proteção executável, e o bloco não aparece em
  `modulos.txt`.
- **O uso dos handles `THREAD_ALL_ACCESS`** (2.25) deixa de ser ambíguo *para
  o lado de dentro*: `FUN_20227c70` abre as threads para gravar registradores
  de debug, não para ajustar prioridade nem para injetar código. **Atenção à
  distinção:** essa função roda **dentro** do jogo e só mexe nas threads do
  próprio processo (ela compara com `GetCurrentProcessId`). Os 1776 handles de
  thread vistos de fora pertencem ao `ReetFPS.exe`, e **nada aqui prova** que
  sejam usados do mesmo jeito.

#### Outros dados estruturais

| Item | Resultado |
|---|---|
| Exports nomeados | **nenhum** — só o `entry` (RVA `0x9FC50`), que é o `DllMain` |
| `DllMain` | CRT puro da MSVC: `__security_init_cookie` (cookie `0xbb40e64e`, RVA `0xA0020`) e o despachante do CRT |
| IAT | **bound**, com endereços reais de runtime (KERNEL32 em `0x7606xxxx`, USER32 em `0x7718xxxx`) — confirma dump de processo vivo |
| Detector de malware do Ghidra | 0 achados |
| Anti-análise | 547 achados, todos `INT3` do CRT contados em duplicado (`INT 3` + `INT 0x2d` no mesmo endereço). Ruído, não ofuscação — o binário não é empacotado |

Sem export nomeado, o carregador em memória (seção 4) só pode chamar o módulo
pelo entry point, que é o comportamento esperado de mapeamento manual.

**Os 221 imports por DLL:**

| DLL | Qtd. | DLL | Qtd. |
|---|---|---|---|
| KERNEL32 | 143 | WS2_32 | 4 |
| USER32 | 27 | WEVTAPI | 4 |
| WINHTTP | 11 | IPHLPAPI | 3 |
| DBGHELP | 7 | D3DX9_43 | 2 |
| ADVAPI32 | 5 | SHELL32 / OLE32 / URLMON / D3D9 / D3D11 / D3DCOMPILER_43 | 1 cada |
| PDH | 5 | IMM32 | 4 |

As entradas de D3D (5 no total) e o PDH confirmam o overlay e o contador de
CPU já descritos em 2.25. `WEVTAPI` (log de eventos do Windows) e `IPHLPAPI`
são novos nesta leitura e **não foram atribuídos a nenhuma feature**.

**Chamadores das APIs sensíveis** (localizados pelo slot da IAT, já que os
imports são símbolos externos):

| API | Slot IAT | Chamadores |
|---|---|---|
| `SetThreadContext` | `0x202d0050` | `FUN_2020d8e0`, `FUN_20227c70` |
| `SuspendThread` | `0x202d01a8` | `FUN_2020da60`, `FUN_20227c70` |
| `SetWindowLongA` | `0x202d02d8` | `FUN_20225cd0`, `FUN_20240080` |
| `CallWindowProcA` | `0x202d02a4` | `FUN_202118c0` |
| `WinHttpSendRequest` | `0x202d02fc` | `FUN_20213670` |
| `URLDownloadToFileW` | `0x202d039c` | `FUN_20243420` |
| `ImmSetCandidateWindow` | `0x202d0020` | `FUN_201ec320` |
| `RegSetValueExA` | `0x202d000c` | `FUN_20212070`, `FUN_202560a0` |

O par `SetWindowLongA` + `CallWindowProcA` é subclassing da janela (hook de
`WndProc`), o caminho esperado para atalhos de teclado com o jogo em foco —
o que combina com a observação de 2.25 de que o SOCD **não** é global.

**Não verificado:** se o handler VEH altera o contexto para mudar o
comportamento do jogo. Ele tem 66 KB decompilados, quase todos de montagem de
strings ofuscadas na pilha, e não foi percorrido inteiro. O que está provado é
o despacho por endereço de falha.

### 2.27. Como cada feature do painel chega ao jogo (Ghidra, 2026-10-08)

Continuação da 2.26, no mesmo binário. Esta seção responde o que 2.22 e 2.25
tinham deixado aberto: o caminho completo de uma feature, do clique no painel
até o efeito dentro do jogo. Também **corrige** duas afirmações de 2.25.

#### Etapa 1 — a configuração chega por janelas escondidas

Não é HTTP e não é o registro. O módulo lê cada setting por **mensagem de
janela**, enviada ao processo do painel:

| Função | RVA | O que faz |
|---|---|---|
| `FUN_202355d0` | `0x655D0` | `FindWindowA(NULL, "Painel_ReetFPS")` — acha a janela do painel |
| `FUN_202356a0` | `0x656A0` | `FindWindowExA(painel, NULL, NULL, <chave>)` — acha a **filha cujo título é o nome da chave** |
| `FUN_20235740` | `0x65740` | `SendMessageA(h, 0xF0, 0, 0)` — `BM_GETCHECK`, lê **booleano** |
| `FUN_202358e0` | `0x658E0` | `SendMessageA(h, 0x400, 0, 0)` — `WM_USER`, lê **inteiro** |

Ou seja: o `ReetFPS.exe` mantém uma janela oculta chamada `Painel_ReetFPS` com
**um controle filho por setting**, e o *título de cada filha é o nome da
chave*. O módulo dentro do jogo lê os valores perguntando às janelas. É IPC
entre processos sem memória compartilhada, pipe, socket nem privilégio
especial — e explica por que o módulo importa `FindWindowA`, `FindWindowExA` e
`SendMessageA` (2.26).

Isso **corrige** a explicação de 2.25 de que a configuração viria do servidor
por HTTP. O cliente `WinHTTP` existe no módulo, mas não é por ele que os
settings do painel entram.

#### Etapa 2 — as 35 chaves do próprio módulo

O dispatcher é `FUN_20235980` (RVA `0x65980`): 38 blocos no mesmo formato —
monta o nome da chave cifrado na pilha, faz XOR com o primeiro byte, chama o
par localizar-janela/ler-valor e guarda num global. As chaves, decifradas:

```
FPSENABLE              DISPLAY_FPS            DISPLAY_RAM
DISPLAY_CPU            DISPLAY_GPU            DISPLAY_PING
DISPLAY_TIME           DISPLAY_HORZ           FPS_COUNTER_POSITION
CROSS                  CROSSHAIR_SIZE_LINE    CROSSHAIR_SPACE
CROSSHAIR_SQUAR        CROSSHAIR_COLOR        CROSSHAIR_THICKNESS
CROSSHAIR_SHADOW       BORDER_LESS            RESOLUTION
OTIMIZED_RESOLUTION_X  OTIMIZED_RESOLUTION_Y  FPS_SELECTION_INDEX
GRAPHIC                INTERFACE              INTERFACE2
LOADINGMAP             REMOVE_MINIMAP         HUD_STATS
HUD_PLAYERS            HUD_PLAYERS_DEATH      HUD_PLAYERS_HP
HUD_PLAYERS_CARD       ENABLE_RANK            ENABLE_INTERACTIVE
WEAPON_EFFECT          WEAPON_ALIGMENT        ULTRA_SENSI
```

São **diferentes** das chaves do JSON do `.exe` listadas em 2.23: aqui há
`FPSENABLE`, `DISPLAY_*`, `RESOLUTION`, `GRAPHIC`, `ULTRA_SENSI`,
`WEAPON_EFFECT`, `OTIMIZED_RESOLUTION_X/Y`, que não aparecem no painel. Os
nomes estavam cifrados, e é só por isso que a busca por texto de 2.25 não os
encontrou — a conclusão de que eram "usadas apenas no `ReetFPS.exe`" estava
errada.

#### Etapa 3 — duas classes de feature, com mecanismos distintos

Cruzando cada global de configuração com a função que o **lê**:

| Leitor | RVA | Chaves que consome | Classe |
|---|---|---|---|
| `FUN_20232e80` | `0x62E80` | todos os `DISPLAY_*`, todos os `CROSSHAIR_*`, `CROSS` | **overlay** |
| `FUN_20215460` | `0x45460` | `DISPLAY_RAM/CPU/GPU/PING/TIME` | **overlay** (telemetria PDH) |
| `FUN_202315a0` | `0x615A0` | `FPS_COUNTER_POSITION` | **overlay** (layout) |
| `FUN_20225730` | `0x55730` | `FPS_SELECTION_INDEX` | **patch** |
| `FUN_20225bc0` | `0x55BC0` | `LOADINGMAP`, `REMOVE_MINIMAP`, `INTERFACE`, `CROSS` | **patch** |
| `FUN_20225cd0` | `0x55CD0` | `RESOLUTION`, `OTIMIZED_RESOLUTION_X/Y` | **patch** + `SetWindowLongA` |
| `FUN_20225030` / `FUN_20225170` | `0x55030` / `0x55170` | `GRAPHIC` | **detour** (calculam o multiplicador; `FUN_20225410` o aplica) |
| `FUN_20239ac0` | `0x69AC0` | `ENABLE_RANK`, `ENABLE_INTERACTIVE` | — |

**Classe overlay:** as features de exibição (contador de FPS, CPU/RAM/GPU/ping,
mira, HUD) são lidas pelo loop de render ImGui e **desenhadas por cima**. O
jogo não é tocado. Isso fecha o raciocínio de 2.25 sobre o pipeline ImGui: é
por isso que não existe patch para essas features.

**Classe patch:** escreve direto na memória do jogo. O primitivo
(`FUN_202460c0` para inteiro, `FUN_20246230` para float, RVA `0x760C0` e
`0x76230`):

```c
if (target != 0 && DAT_2037d2f8 <= target &&
    VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &old)) {
  *target = value;
  VirtualProtect(target, 8, old, &tmp);     // restaura a proteção
}
```

`DAT_2037d2f8` é um piso de endereço válido, uma checagem de sanidade. Há uma
família de escritores em RVA `0x75CC0`–`0x76300` (byte, word, dword, float,
blocos), todos usando o mesmo `VirtualProtect` — que, note-se, é **resolvido
por nome a partir de string cifrada** (a entrada `VirtualProtect` em
`RVA 0x139910`).

O endereço alvo é calculado assim — exemplo de `FUN_20225640` (RVA `0x55640`),
que é o caso limpo, sem ramificação:

```c
// "0x500141" montado byte a byte na pilha, em ASCII
sscanf("0x500141", "%x")  →  off
FUN_202460c0(valor, base_global + secao_global + off, 0);
```

**O offset nunca existe como constante no binário.** É texto ASCII, convertido
em número em tempo de execução. É o quarto esquema de ofuscação do módulo,
somado aos três de 2.25/2.26, e explica por que varreduras por constantes não
acham os alvos dos patches.

Offsets de patch decifrados, todos atribuídos: **`0x500141`** (FPS),
**`0x212153`** (`INTERFACE`), **`0x100840`** (`CROSS`), **`0x59134`**
(`REMOVE_MINIMAP`), **`0x10854`** (`GRAPHIC` / Fluidez Máxima) e
**`0x18092`** (remoção de hook, não é feature). Os dois últimos estão na
seção "O quarto mecanismo", abaixo.

#### As features pedidas, uma a uma

**FPS Ilimitado / Desbloqueador — rastreado ponta a ponta.**
`FPS_SELECTION_INDEX` → `DAT_20353438` (só aceito se estiver entre 1 e 6) →
`FUN_20225730`, que indexa uma tabela e chama `FUN_20225640(valor)`, que
escreve o valor em `base + 0x500141`. A tabela está em `RVA 0x139820`; lida nos
bytes crus:

```
00 00 00 00 | 0F 27 00 00 | 0F 27 00 00 | 0F 27 00 00
   →  [0, 9999, 9999, 9999, 9999, 9999, 9999]
```

Default, quando o índice está fora de 1..6: `0x168` = **360**.

Ou seja: **as seis opções do painel escrevem todas o mesmo 9999**. A diferença
entre os seis itens da lista (2.23 descreve `FPS_SELECTION_INDEX` = índice
1..6) não chega ao jogo por este caminho. Só o caso inválido difere, com 360.
**Não verificado:** se existe um segundo caminho que diferencie as seis
opções; neste não há.

**Carregamento Instantâneo (`LOADINGMAP`) — resolvido.** Ver a tabela de
patches abaixo: escreve `0xC98B` em 16 bits, que são os bytes `8B C9` =
instrução `MOV ECX,ECX`. É um **no-op de 2 bytes**: o patch neutraliza uma
instrução no caminho de carregamento de mapa.

#### Os cinco patches do `FUN_20225730`, resolvidos

Os cinco ramos reusam os mesmos slots de pilha, então reconstrução linear
contamina as strings (uma primeira tentativa produziu `"99975494351E-38"`, a
fusão de `"9999"` com `"1.175494351E-38"`). A solução foi **simular o assembly
registo a registo**, reiniciando o estado em cada fronteira de bloco: o
compilador faz o XOR em registos (`MOV AH,0x14; MOV CL,0x6c; XOR CL,AH` →
`CL=0x78='x'`), e os `PUSH` deslocam o `ESP`, então os offsets dos `LEA` são
relativos a um `ESP` móvel. Alguns bytes são XOR'd **duas vezes** com a mesma
chave, o que se anula — são engodo.

Ordem dos argumentos, lida dos `PUSH` do chamador `FUN_20225bc0` (o
`ADD ESP,0x1c` = 7 dwords = 4 pushes + os 3 slots de um `SUB ESP,0xc` fecha a
contagem):

| Local no callee | Feature | Global |
|---|---|---|
| `EDX` (fastcall) | `FPS_SELECTION_INDEX` | `DAT_2037d27c` |
| `EBP+0x8` | `CROSS` | `DAT_2037d2e4` |
| `EBP+0x18` | `INTERFACE` | `DAT_2037d224` |
| `EBP+0x1c` | `LOADINGMAP` | `DAT_2037e470` |
| `EBP+0x20` | `REMOVE_MINIMAP` | `DAT_2037d2f4` |

A tabela de patches resultante:

| Feature | Escritor | Largura | Valor escrito | Endereço |
|---|---|---|---|---|
| `CROSS` | `FUN_20245f30` | 8 bits | `0xEB` se ligado, `0x74` se desligado | `DAT_2037e42c + 0x100840` |
| `INTERFACE` | `FUN_20246230` | float | `999.0` se ligado, `1.175494351E-38` (`FLT_MIN`) se desligado | `DAT_2037d298 + 0x212153` |
| `LOADINGMAP` | `FUN_20246300` | 16 bits | `0xC98B` | ponteiro em `DAT_2037e468` |
| `REMOVE_MINIMAP` | `FUN_20245f30` | 8 bits | `4` | `iRam2037e43c + 0x59134` |
| `FPS_SELECTION_INDEX` | `FUN_202460c0` | 32 bits | `9999` (ou `360`) | `DAT_2037e450 + iRam2037d300 + 0x500141` |

Três desses patches são **patch de código**, não de dado:

- **`CROSS`** troca um único byte de opcode entre `0xEB` (`JMP` curto,
  incondicional) e `0x74` (`JZ` curto, condicional). Ligar a mira força um
  salto que normalmente é condicional. As duas codificações foram conferidas
  contra o próprio binário (`JMP` curto aparece como `eb XX` e `JZ` curto como
  `74 XX`, ambos de 2 bytes), não assumidas de tabela.
- **`LOADINGMAP`** escreve `0xC98B`, que em little-endian são os bytes
  `8B C9` — a instrução `MOV ECX,ECX`, um no-op de 2 bytes. Anula uma
  instrução de 2 bytes no carregamento de mapa.
- **`INTERFACE`** troca um float por `FLT_MIN` (≈ 0) quando desligado, e por
  `999.0` quando ligado. Trocar um limite por `FLT_MIN` é o padrão de
  "remover o teto" em código que compara tempo de frame. **Não verificado:**
  o significado do campo, e portanto qual dos dois estados é o "sem atraso".

`LOADINGMAP` e `REMOVE_MINIMAP` são **one-shot**: cada um tem um global de
"já aplicado" (`DAT_2037e6c4` e `DAT_2037e674`) que impede reaplicação. O
`REMOVE_MINIMAP` ainda espera ~1 s (`GetTickCount64`) antes de aplicar.

Os aplicadores pequenos guardam o offset do mesmo jeito que o resto —
`FUN_20220b40` (RVA `0x50B40`) monta `"0x100840"` e `FUN_20225580`
(RVA `0x55580`) monta `"0x212153"`, ambos byte a byte com XOR em registo,
depois `sscanf("%x")`.

As larguras vêm das assinaturas dos escritores, todas confirmadas:
`FUN_20245f30` (RVA `0x75F30`) escreve `undefined1` = 8 bits,
`FUN_20246300` (RVA `0x76300`) escreve `undefined2` = 16 bits,
`FUN_202460c0` 32 bits e `FUN_20246230` float a partir de `XMM1`.

**Fluidez Máxima — é a chave `GRAPHIC`, e sim existe no módulo.**
~~Não há chave `FLUIDEZMAX` entre as 35... não aplicam patch no jogo.~~
**CORRIGIDO:** uma primeira leitura concluiu que `GRAPHIC` não fazia patch
porque seus dois leitores diretos (`FUN_20225030`, `FUN_20225170`) não chamam
escritor nenhum. Eles não chamam mesmo — mas **calculam um multiplicador**
que um terceiro caminho escreve no jogo. Ver "O quarto mecanismo" abaixo.

A ligação entre o nome do painel e o nome do módulo está provada por duas
fontes independentes: a tabela de métodos publicados do `.exe` (seção 1 deste
documento) mapeia `FLUIDEZMAX` → **`GRAPHIC_OFFClick`**, e a chave do módulo
é **`GRAPHIC`**. É a mesma feature nas duas pontas.

#### O quarto mecanismo: detour inline com stub escondido no cabeçalho do jogo

Isto resolve os dois offsets que tinham ficado sem feature atribuída.

**`0x10854` → Fluidez Máxima (`GRAPHIC`).** O aplicador é `FUN_20225410`
(RVA `0x55410`), chamado **sem guarda** a cada frame pelo corpo do loop
`FUN_2023fdc0`, com três blocos one-shot próprios:

1. Monta **14 bytes** de código num buffer e os escreve byte a byte (os
   imediatos estão no assembly: `0x4b100ff3`, `0x590ff308`, `0x4004000d`,
   `0xc300`);
2. chama `FUN_20214670(DAT_2037e434 + 0x10854, DAT_2037d2f8 + 0x800)`;
3. mantém atualizado um float em `DAT_2037d2f8 + 0x400`.

`FUN_20214670` não é um escritor de valor — é um **instalador de detour**:

```c
VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
*target = 0xE8;                                  // CALL rel32
*(int *)(target + 1) = (dest - (int)target) - 5; // deslocamento relativo
VirtualProtect(target, 5, old, &tmp);
```

Ou seja: escreve um `CALL` de 5 bytes no jogo, em `base + 0x10854`, apontando
para o stub de 14 bytes. O stub, desmontado:

```asm
F3 0F 10 4B 08              MOVSS XMM1, dword ptr [EBX+0x08]
F3 0F 59 0D 00 04 40 00     MULSS XMM1, dword ptr [0x00400400]
C3                          RET
```

Multiplica um float do jogo por um multiplicador. **E aqui está o detalhe
bonito:** o stub referencia o endereço absoluto `0x00400400`, enquanto o
módulo escreve o multiplicador em `DAT_2037d2f8 + 0x400`. Logo
`DAT_2037d2f8 = 0x00400000` — a **base do `PointBlank.exe`** (confere com o
`modulos.txt` de 2.25). Confirmado lendo o global no dump: vale exatamente
`0x00400000`.

Isso significa que **o stub e o seu parâmetro ficam na folga do cabeçalho PE
do próprio jogo** (`0x400800` e `0x400400`) — espaço morto que existe em
qualquer executável, portanto nada é alocado e nada aparece como região nova
no mapa de memória.

O multiplicador vem de `DAT_203403e8`, governado por `GRAPHIC`:

| Global | Valor no dump | Papel |
|---|---|---|
| `DAT_203403e8` | **1.0** | multiplicador corrente (neutro em repouso) |
| `DAT_2036c128` | **1.038** | alvo, aplicado por `FUN_20225170` |
| `DAT_20373c48` | **1.1** | máximo configurado |
| `DAT_2036c124` | 1.0 | máximo efetivo no instante da captura |

`FUN_20225030` (RVA `0x55030`) sobe o multiplicador **+0.005 a cada ~200 ms**
(`GetTickCount64`) enquanto `GRAPHIC == 1` e uma condição de estado do jogo
vale, até o máximo; fora disso **zera de volta para 1.0**. Os valores
configuráveis são presos ao intervalo `[1.0, 1.23]`, com fallback 1.1 e 1.038.
A subida gradual em vez de um salto é o que se esperaria de algo pensado para
não aparecer como mudança instantânea.

**Não verificado:** *qual* float é multiplicado. O stub lê `[EBX+0x08]`, e o
alvo do hook não pôde ser resolvido porque `DAT_2037e434` está **zerado no
dump** — a mesma situação das quatro bases dos breakpoints (2.26). Um
multiplicador que sobe devagar até ~1.04–1.1 é compatível com suavidade de
animação ou com velocidade, mas o campo em si não foi identificado.

**`0x18092` → não é feature: é a *remoção* de um hook.** O aplicador é
`FUN_20225240` (RVA `0x55240`), chamado por `FUN_20240080`. Ele escreve quatro
vezes os mesmos 5 bytes — dword `0x8BF0453B` seguido de byte `0xC0`:

```asm
3B 45 F0    CMP EAX, dword ptr [EBP-0x10]
8B C0       MOV EAX,EAX          ; no-op de 2 bytes, enchimento
```

São **5 bytes**, exatamente o tamanho de um `CALL rel32`. Portanto não instala
nada: **restaura** a instrução original de 3 bytes mais 2 bytes de enchimento
por cima de um detour, desfazendo-o. As quatro strings de offset decifradas
são **todas `"0x18092"`**, cada uma com uma chave diferente (`0x0F`, `0x5A`,
`0x71`, `0x76`) — a cifra é por local de uso. São dois alvos
(`DAT_2037d2d0` e `DAT_2037e474`), cada um corrigido em `+0` (dword) e `+4`
(byte), e ambos os globais são zerados no fim.

**Este caminho está dormente na captura:** `DAT_2037d2d0` e `DAT_2037e474`
valem **0**, e nada no módulo os escreve (são apenas lidos, por esta função).
Como os escritores exigem `DAT_2037d2f8 <= alvo` e `0x400000 <= 0x18092` é
falso, os quatro patches seriam descartados. **INFERIDO:** é o lado de
desinstalação de um hook cujo instalador não está neste build ou não foi
localizado.

Logo após essa chamada, `FUN_20240080` escreve `0xC3` (`RET`) num endereço
guardado em `DAT_2037d45c`, o que **neutraliza uma função do jogo** fazendo-a
retornar de imediato. Mais um padrão de patch, ainda **sem atribuição**.

As codificações `MOVSS`/`MULSS`/`CMP`/`JMP`/`JZ` usadas acima foram todas
conferidas contra instruções reais do próprio binário, não assumidas de tabela.

#### Relação com os breakpoints de hardware da 2.26

São **três mecanismos independentes** convivendo no mesmo módulo:

- **patch direto de valor** (`VirtualProtect` → escrever → restaurar), usado
  pela maioria das features de configuração desta seção — inclusive os casos
  em que o "valor" é um opcode (`CROSS`, `LOADINGMAP`);
- **detour inline**, um `CALL` de 5 bytes para um stub que o módulo esconde na
  folga do cabeçalho PE do jogo (`GRAPHIC` / Fluidez Máxima);
- **breakpoint de hardware + VEH** (2.26), quatro alvos, que **não escreve
  byte nenhum** no código do jogo.

**INFERIDO:** a divisão provável é por natureza do alvo — trocar um valor ou um
opcode é patch direto; precisar do *valor de um registrador em tempo de
execução* (como o float em `[EBX+0x08]` da Fluidez Máxima) pede um detour, que
roda com o contexto do jogo vivo; e interceptar a execução sem alterar byte
nenhum pede o breakpoint. **Não verificado:** quais features usam os quatro
breakpoints, já que as quatro bases de módulo estavam zeradas na captura
(2.26) — a mesma razão que impede resolver o alvo do detour `0x10854`.

### 2.28. Reconstrução C das funções-chave (2026-10-08)

Com base nas decompilações do Ghidra (seções 2.26–2.27) e nos padrões dos
handlers existentes, as seguintes funções foram adicionadas ao `ponto_blank.c`:

#### Do lado `ReetFPS.exe` (seção 9, 16 e 25)

| Função nova | Endereço | Feature |
|---|---|---|
| `pb_graphic_ativar` | GRAPHIC_OFFClick `0x1372a03c` | Fluidez Máxima — grava `FLUIDEZMAX=ACTIVE` e notifica overlay |
| `pb_graphic_restaurar` | GRAPHIC_ONClick `0x1372a37c` | Desfaz Fluidez Máxima |
| `pb_interfacedelay_ativar` | INTERFACEDELAY_OFFClick `0x1372d4b8` | Interface Sem Delay — grava `INTERFACE`, `SET_INTERFACE`, remove `SET_INTERFACE2` |
| `pb_interfacedelay_restaurar` | INTERFACEDELAY_ONClick `0x1372d948` | Remove as três chaves |
| `pb_fonte_personalizada_escrever_ini` | rotina de pré-lançamento (INFERIDO) | Lê `FONTE_PERSONALIZADA_VALUE` e grava `Locale\Brazil\Font.ini` |

Os blobs de string de `pb_graphic_*` estão marcados como INFERIDO (a função
`0x1372a03c` não foi decompilada no Ghidra). Os demais são baseados nos
endereços de blob listados na descrição do handler da seção 2.12.

#### Do lado do módulo injetado (seção 26 do `ponto_blank.c`)

Reconstrução em C das funções do módulo capturado do processo do jogo:

| Função nova | RVA no módulo | O que faz |
|---|---|---|
| `mod_ipc_find_painel` | `0x655D0` | `FindWindowA("Painel_ReetFPS")` |
| `mod_ipc_find_chave` | `0x656A0` | `FindWindowExA` pelo título da chave |
| `mod_ipc_ler_bool` | `0x65740` | `BM_GETCHECK (0xF0)` → bool |
| `mod_ipc_ler_int` | `0x658E0` | `WM_USER (0x400)` → int |
| `mod_settings_ler` | `0x65980` | Lê todas as 35 chaves do painel |
| `mod_patch_fps` | `0x55640` | Escreve FPS em `base_pb + 0x500141` |
| `mod_patch_interface` | `0x55580` | Escreve float em `base_pb + 0x212153` |
| `mod_patch_cross` | `0x50B40` | Alterna byte `0xEB`/`0x74` em `base_pb + 0x100840` |
| `mod_patch_loadingmap` | parte de `0x55730` | Escreve `0xC98B` (no-op), one-shot |
| `mod_patch_minimap` | parte de `0x55730` | Escreve byte `4` em `base_pb + 0x59134`, one-shot com delay |
| `mod_patches_aplicar` | `0x55BC0` / `0x55730` | Dispatcher de todos os patches por frame |
| `mod_graphic_instalar_detour` | `0x55410` | Copia stub de 14 bytes e instala `CALL rel32` |
| `mod_graphic_ramp` | `0x55030` | Sobe multiplicador +0.005/200 ms até máximo |
| `mod_graphic_aplicar` | `0x55410` | Instala detour + atualiza multiplicador em `base_pb + 0x400` |
| `TModOverlayState` + `k_mod_cores_crosshair` | `0x62E80` area | Tipos do overlay ImGui (mira, FPS, HUD) |

A tabela `k_mod_cores_crosshair[7]` documenta as seis cores ARGB da mira
(`0xffff0000` … `0xffffffff`) extraídas do bloco if/else de `FUN_20235980`.

A cifra "Painel_ReetFPS" (XOR 0x68, 14 bytes) foi decifrada byte a byte a
partir dos literais de pilha de `FUN_202355d0` e incluída no corpo de
`mod_ipc_find_painel`.

---

## 3. Resumo em uma linha

> Com o processo do jogo, o ReetFPS **acha a instalação, inicia via
> PBLauncher, eleva a prioridade de CPU/GPU e desliga o EcoQoS, monitora
> crashes e o encerra por `taskkill` sob demanda**. Os ajustes de Windows
> (catálogo de 559 comandos e lotes como "Ajustes de desempenho", "Teclado
> Turbo", transparência e GPU) rodam no sistema. As features da tela FPS Game
> Booster só gravam chaves (`MINIMAP=ACTIVE` etc.) num JSON sincronizado com o
> servidor; o efeito dentro do jogo vem de código injetado no Point Blank. Na
> versão analisada estaticamente, isso era um `window.ime` carregado pela
> `ReetFPS.dll`. Na versão observada ao vivo (2.25), é a **`ReetFPS.dll`
> compilada em 02/10/2026** injetada diretamente na memória do
> `PointBlank.exe` via `cLoadLibrary`, **sem arquivo em disco**. Dentro do
> jogo, esse módulo engancha por **breakpoint de hardware com handler VEH**
> (2.26): quatro alvos resolvidos por nome de símbolo, **sem escrever um byte
> no código do jogo**. Ele recebe os settings do painel por **mensagem de
> janela** (`FindWindowExA` + `SendMessageA` numa janela oculta
> `Painel_ReetFPS`, não por HTTP) e os aplica de duas formas (2.27): as
> features de exibição são **desenhadas por cima** pelo ImGui, sem tocar no
> jogo; as demais são **patch direto na memória** do Point Blank
> (`VirtualProtect` → escrever → restaurar), em endereços cujos offsets ficam
> guardados como texto ASCII cifrado e convertidos em tempo de execução. A
> Fluidez Máxima vai além: instala um **detour** de 5 bytes para um stub de
> código que o módulo esconde na folga do cabeçalho PE do próprio jogo, e que
> multiplica um valor por um fator que sobe devagar de 1.0 até ~1.1.

## 4. Nota sobre reconstrução

Nos imports do executável existem APIs de manipulação de memória de outro
processo (`OpenProcess`, `ReadProcessMemory`, `WriteProcessMemory`,
`VirtualAllocEx`, `CreateRemoteThread`) e um grupo de rotinas em
`0x136e5000`–`0x136e7400` que as utiliza. A análise identifica esse grupo
como um **carregador PE em memória** — comportamento dual-use (aparece em
proteções anti-cheat/DRM e em outras categorias de software). Ele é o
primeiro elo da cadeia descrita em 2.22. Tanto ele quanto a injeção por IME
da `ReetFPS.dll` são **descritos** (seção 24 do `ponto_blank.c`), mas
deliberadamente não reconstruídos como código.
