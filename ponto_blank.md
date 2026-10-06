# ReetFPS × Point Blank — o que o programa faz referente ao jogo

Adendo ao `README.md` e ao `reetfps.c`. Documenta, em alto nível, o que o
ReetFPS faz especificamente com o Point Blank — além dos 559 ajustes de
Windows do catálogo.

A documentação aqui descreve **comportamento observado** no binário via
decompilação (nomes de classes RTTI, strings de UI, imports da PE, labels
de telemetria interna do próprio programa). **Não há reconstrução passo a
passo das rotinas sensíveis** (ver "Nota sobre reconstrução", no fim).

---

## 1. O que o ReetFPS NÃO faz com o jogo

Para alinhar expectativas:

- **Não altera arquivos do Point Blank em disco.** Não há no binário rotinas
  de patching de `.exe`/`.dll`/`.pak`/arquivos de recurso do jogo, nem
  strings de nomes de arquivos do jogo além do `PBLauncher.exe` (que ele
  só *localiza* e *inicia*).
- **Não modifica configuração interna do jogo** (não escreve em `.ini` do
  jogo, não altera arquivos de perfil, não mexe em pastas como `Shader/` — ao
  contrário, orienta o jogador a abrir o PBLauncher e usar o botão "Check"
  quando essa pasta estiver inconsistente).

Essas duas negativas são importantes: todo o impacto do ReetFPS no FPS vem de
**ajustes do Windows** aplicados *em volta* do jogo, não de alterações
*dentro* do jogo.

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
de o início automático falhar.

### 2.3. Encerra os processos do jogo quando pede

Function `FUN_135a0d84` monta a linha `taskkill.exe /F /T /IM "<nome.exe>"`
e dispara. Uma segunda rota em `0x135cafd4` monta `cmd /c taskkill /im "..."`.
Strings de UI que acompanham:

- `"Os processos do PointBlank foram encerrados. Pode abrir o jogo de novo."`
- `"O jogo fechou, mas o processo continua aberto. Abra para encerrar."`
- `"Processo %s encerrado"`

É o botão de "fechar jogo travado" / "resetar sessão". Nada mais exótico.

### 2.4. Monitora estabilidade do jogo (telemetria interna do ReetFPS)

Classes RTTI presentes no binário (todas internas ao ReetFPS, não ao jogo):

- `TPointBlankStabilityMonitor` → acompanha o jogo em execução.
- `TPointBlankDailyState` → estado diário (quantas sessões, quantos crashes).
- `TPointBlankExitInfo` → captura código de saída do processo do jogo.
- `TPointBlankWaitThread` → thread que espera o jogo terminar.
- `TPointBlankCrashVerificationTask` / `TPointBlankCrashVerifierThread`
  / `TPointBlankCrashVerificationResult` → verifica se o encerramento foi
  um crash e classifica.

String correspondente da UI:

- `"Detectamos %d encerramentos inesperados do PointBlank hoje."`
- `"%d encerramentos inesperados hoje. Veja como reparar."`

Isso é instrumentação própria do ReetFPS — ele conta quantas vezes o jogo
caiu e sugere passos de reparo (checar pasta `Shader/`, usar `Check` no
PBLauncher). Não é ingerência no jogo.

### 2.5. Eleva a prioridade do processo do jogo

Function `FUN_135a4dc4` (reconstruída integralmente em `reetfps.c`):

- Chama `GetPriorityClass(hProcesso)` → guarda a prioridade original em
  `+0x8d0` do contexto.
- Se a prioridade **não** for `REALTIME` (`0x100`), `HIGH` (`0x80`) nem
  `ABOVE_NORMAL` (`0x8000`), chama `SetPriorityClass(hProcesso, ABOVE_NORMAL)`.
- Em caso de erro, loga `"GetPriorityClass"` / `"SetPriorityClass.AboveNormal"`
  com `GetLastError()`.

Esse é o único "ajuste direto no processo do jogo" na faixa de rotinas que
pude confirmar com segurança: eleva o `PriorityClass` e guarda o valor
anterior para restaurar.

Rotinas análogas no mesmo módulo (`uMaintain`, strings
`"Maintain.GetProcessPriorityBoost"`, `"Maintain.SetProcessPriorityBoost.Enable"`):

- `Set/GetProcessPriorityBoost` — habilita o boost dinâmico de prioridade da
  Windows API no processo do jogo.
- `AvSetMmThreadPriority` (via MMCSS) — eleva prioridade de thread em um
  perfil multimídia (classe "Games" do MMCSS).
- `D3DKMTSetProcessSchedulingPriorityClass` — eleva a classe de agendamento
  da GPU (DWM/GPU scheduler) para o processo.

### 2.6. "Pausar/retomar" o Windows Update enquanto você joga

Strings:

- `"Removendo a pausa do Windows Update..."`
- `"Não foi possível gravar a pausa. Execute o ReetFPS como administrador."`
- `"Não foi possível remover a pausa. Execute o ReetFPS como administrador."`

Isso é independente do jogo em si: o ReetFPS adia o Windows Update para
evitar que ele consuma CPU/disco durante a partida. Implementado via
registros de política do WU (parte do catálogo `OTHER` em
`catalogo_comandos.md`).

### 2.7. Gerenciamento inteligente (ativado por usuário)

String:

- `"Gestão Inteligente ativada. O ReetFPS passa a gerenciar o desempenho do Point Blank automaticamente."`

Quando ligado, o ReetFPS roda o `TPointBlankStabilityMonitor` em segundo plano:
detecta o processo do jogo subir, aplica o perfil de energia / prioridade,
e no encerramento do jogo reverte. É a razão de existirem os pares
"aplicar/reverter" no catálogo de comandos.

### 2.8. Perfil FLUIDEZMAX — "Carregamento de mapa otimizado"

O botão FLUIDEZMAX na tela de Game Mode ativa um **perfil de sete itens** que
trabalham em conjunto. Não há uma API de "preload de mapa" — o efeito de
carregamento rápido é o resultado combinado de eliminar competição por
recursos.

#### Itens do perfil (tabela em 0x136ecb00)

| # | Chave | Label UI | Efeito |
|---|---|---|---|
| 0 | `FLUIDEZMAX` | — | **Cabeçalho do perfil**; nome exibido: "Carregamento de mapa otimizado" |
| 1 | `fullscreen` | FULLSCREEN | Força o PB em **fullscreen exclusivo** — DWM desativado → menos pressão de GPU/VRAM durante loading |
| 2 | `OTIMIZER_PB_MANAGER` | — | Ativa o `TPointBlankStabilityMonitor` (gerenciador automático) |
| 3 | `smart` | Gestão Inteligente | Liga modo automático: monitor aplica/reverte tweaks no ciclo de vida do jogo |
| 4 | `PRIORITYPB` | rocket | Aplica as três camadas de prioridade: CPU (`ABOVE_NORMAL`), D3DKMT GPU scheduler, e Priority Boost |
| 5 | `INTERFACE` | window | Configura o modo de janela/interface do PB conforme perfil recomendado |
| 6 | `FPS_SELECTION_INDEX` | speed | Lê o índice de FPS do INI do PB (`[Graphics] FPSType/FPSVal`) e aplica o cap correto |
| 7 | `REETGAMEMODE` | gamepad | Habilita **Windows Game Mode** → maior prioridade de I/O e CPU no scheduler do Windows |

#### Por que os mapas carregam "instantaneamente"

| Mecanismo | Por que ajuda |
|---|---|
| Fullscreen exclusivo | DWM para de compor → GPU e VRAM livres para assets do mapa |
| Prioridade CPU `ABOVE_NORMAL` + Priority Boost | Mais tempo de CPU durante I/O intenso de loading |
| D3DKMT GPU scheduler | GPU processa as texturas com mais prioridade |
| Windows Game Mode | I/O scheduler prioriza as leituras de disco do PB |
| Gestão Inteligente | Todos os 559 tweaks da categoria CPU_GPU_PRIORITY e POWER ativados exatamente quando o PB está subindo |

#### Itens separados: limpeza de cache (opcionais, não são parte do FLUIDEZMAX)

Há uma tabela separada de "limpadores" em `0x1366c900` que aparecem como
opções na mesma tela mas **não fazem parte do FLUIDEZMAX**:

| Chave | Alvo | Aviso |
|---|---|---|
| `prefetch` | `%WINDIR%\Prefetch` (apaga os `.pf` do prefetcher) | "Seguro, mas programas podem abrir mais devagar na primeira vez" |
| `driver_extract_cache` | `%SystemDrive%\NVIDIA\DisplayDriver\*` (cache de extração de drivers NVIDIA) | "Seguro..." |

A limpeza de Prefetch remove dados obsoletos do prefetcher do Windows (que
os tweaks do catálogo já desabilitam via `reg add PrefetchParameters`). É
um passo complementar, não a causa principal do loading rápido.

#### Execução (fluxo técnico)

```
pb_ativar_fluidezmax()          @ 0x13700af0
  └─ perfil_marcar_ativo()      @ 0x13700b24  (flag "ativo" no painel)
  └─ ExecuteGameModeActions()   @ 0x137008d0  (TReetGameModePanel)
       ├─ itera lista de itens em panel+0x2e0
       ├─ filtra item->habilitado (+0x24)
       ├─ copia (chave, icone, descricao) para array
       ├─ cria dialog de progresso
       └─ FUN_13219b0c / FUN_1321a804 — despacha em thread separada
  └─ panel->+0x38e = 1           (flag "fluidezmax aplicado")
```

### 2.9. Timer Resolution — reduzir latência para 0.5ms

O botão **TIMER RESOLUTION** ativa três camadas em conjunto:

#### Camada 1 — NtSetTimerResolution (global, todo o sistema)

| | |
|---|---|
| API | `NtSetTimerResolution` (ntdll.dll, não documentada) |
| Valor | 5000 × 100ns = **0.5ms** (padrão Windows: ~15.6ms = 156001 unidades) |
| Complemento | `timeBeginPeriod(1)` — equivalente Win32 de 1ms |
| Thunk | `0x1357c294` (Ghidra reconhece pelo nome) |

#### Camada 2 — Thread de manutenção ("ReetTimerPrecision")

O Windows pode restaurar o timer quando outros processos que pediram alta resolução saem. Para evitar isso, o ReetFPS mantém um thread de fundo que **re-aplica** o `NtSetTimerResolution(5000)` continuamente enquanto o recurso estiver ativo.

| Função | Endereço | Papel |
|---|---|---|
| `pb_timer_resolution_ativar_manutencao` | `0x1357c9b0` | Cria e inicia a thread |
| `pb_timer_resolution_parar` | `0x1357ca34` | Para a thread e reverte |
| `pb_timer_resolution_revogar` | `0x1357c5e0` | Revert: `NtSetTimerResolution(0, FALSE, ...)` + `timeEndPeriod(1)` |

Nome interno da thread (string `0x1357c870`): `"ReetTimerPrecision"`  
Log de propósito (string `0x1357c40c`): `"Maintain 0.5ms timer for low latency"`

#### Camada 3 — SetProcessInformation / TimerResolutionPolicy (Windows 11)

No Windows 11, um processo pode declarar que vai "ignorar" a resolução global de timer. O ReetFPS **limpa esse flag** no processo do Point Blank para garantir que o jogo responda ao 0.5ms global. Função: `FUN_135a50d8` (`0x135a50d8`), que também desativa EcoQoS no mesmo passo.

| Campo | Valor | Efeito |
|---|---|---|
| `ControlMask = 0x01` (EcoQoS) | `StateMask = 0` | Desativa throttling de execução |
| `ControlMask = 0x04` (IGNORE_TIMER_RESOLUTION) | `StateMask = 0` | Faz o processo respeitar o timer global de 0.5ms |

Strings de log confirmadas no binário: `"GetProcessInformation.TimerResolution.Pre"`, `"SetProcessInformation.TimerResolutionPolicy"`.

### 2.10. Limpeza Inteligente — apaga caches com segurança

A "Limpeza Inteligente" é um varredor recursivo de diretórios com três camadas de proteção que apaga arquivos temporários/cache do Windows e de drivers.

#### Itens de limpeza (tabela em `0x1366c900`)

| Chave | Alvo expandido | Descrição |
|---|---|---|
| `prefetch` | `%WINDIR%\Prefetch\*.pf` | Apaga arquivos do prefetcher do Windows |
| `driver_extract_cache` | `%SystemDrive%\NVIDIA\DisplayDriver\*` | Apaga cache de extração de drivers NVIDIA |

#### Camadas de segurança

| Mecanismo | Implementação |
|---|---|
| Bloqueio de juncão/symlink | `FUN_13674088` testa `FILE_ATTRIBUTE_REPARSE_POINT` antes de recursão |
| Caminhos longos | `FUN_13673dc4` prepende `\\?\` — sem limite de MAX_PATH |
| Reporte de falha granular | Distingue: negado (`ERROR_ACCESS_DENIED`), em-uso (`ERROR_SHARING_VIOLATION`), e outros |

#### Funções mapeadas

| Função | Endereço | Papel |
|---|---|---|
| `limpeza_varrer_diretorio` | `0x13674090` | Scanner recursivo: FindFirstFileW → loop → FindNextFileW |
| `limpeza_deletar_arquivo` | `0x13673ee0` | DeleteFileW com tentativa de posse (`FUN_136712f0`) |
| `limpeza_normalizar_caminho` | `0x13673dc4` | Adiciona prefixo `\\?\` para caminhos longos |
| `limpeza_e_juncao_symlink` | `0x13674088` | Testa `FILE_ATTRIBUTE_REPARSE_POINT` |

#### Estrutura de estatísticas (`TLimpezaStats`, retornada por `FUN_13153234`)

| Offset | Campo | Significado |
|---|---|---|
| `+0x5c` | `negado_acesso` | Ao menos um arquivo teve `ERROR_ACCESS_DENIED` |
| `+0x5d` | `arquivo_em_uso` | Ao menos um arquivo estava aberto |
| `+0x5e` | `deletado` | Ao menos um arquivo foi removido com sucesso |
| `+0x60` | `qtd_falhas` | Contador acumulado de falhas |
| `+0x70`/`0x74` | `qtd_arquivos` | Arquivos deletados (64 bits) |
| `+0x78`/`0x7c` | `qtd_pastas` | Sub-pastas percorridas |
| `+0x80`/`0x84` | `bytes_totais` | Bytes liberados (64 bits) |

String de status final: `"Limpeza coordenada: itens preservados (negado=%s, em uso=%s, protecao=%s)"` @ `0x13673d30`.

---

### 2.11. Miras Customizadas — crosshair sobreposta ao jogo

O ReetFPS desenha uma mira personalizada em uma janela transparente sobre o Point Blank. A mira não altera arquivos do jogo — é uma sobreposição feita com GDI/DirectDraw.

#### Classes RTTI

| Classe | Endereço RTTI | Papel |
|---|---|---|
| `TRPCrosshair` | `0x136945ef` | Classe principal (dados + lógica de desenho) |
| `UDialogCrosshair` | `0x13696e21` | Formulário de configuração |

#### Strings de UI

| String | Endereço | Descrição |
|---|---|---|
| `"PERSONALIZAR MIRA"` | `0x13694a94` | Botão que abre o diálogo |
| `"TAMANHO DA LINHA"` | `0x1369491c` | Controle de espessura |
| `"QUADRADO CENTRAL"` | `0x13694970` | Tipo de forma: quadrado central |
| `"Exibir sombra na mira"` | `0x13695f50` | Checkbox de sombra |
| `"COR DA MIRA"` | `0x13695f88` | Seletor de cor |
| `"CROSSHAIR_SHADOW"` | `0x13696f1c` | Chave de configuração para sombra |

#### Estrutura `TRPCrosshair` (offsets confirmados via `FUN_136951fc`)

| Offset | Campo | Descrição |
|---|---|---|
| `+0x318` | `quantidade_linhas` | Espessura — número de repetições por segmento |
| `+0x334` | `indice_forma` | Índice da forma (0 = QUADRADO CENTRAL, …) |
| `+0x338` | `sombra_ativa` | Bool: exibir sombra sob a mira |

#### Funções mapeadas

| Função | Endereço | Papel |
|---|---|---|
| `crosshair_desenhar` | `0x136951fc` | Renderiza a mira no canvas a cada repaint |
| `crosshair_desenhar_segmento` | `0x13695114` | Desenha um dos 4 segmentos (N/S/L/O) |
| `crosshair_obter_dimensoes` | `0x13694bf0` | Lê largura/altura do canvas |

#### Cores usadas (ARGB)

| Constante | Valor | Cor |
|---|---|---|
| Background | `0xff04050b` | Quase preto (transparente) |
| Contorno | `0xff14172e` | Azul navy escuro |

---

## 3. Resumo em uma linha

> O ReetFPS **não toca em arquivos nem no estado interno do Point Blank**.
> O que ele faz com o jogo é: **achar a instalação, iniciá-lo via PBLauncher,
> elevar prioridade de CPU/GPU/MMCSS do processo, monitorar estabilidade,
> encerrá-lo por `taskkill` sob demanda, e aplicar/reverter os 559 ajustes
> de Windows em torno da sessão de jogo.**

## 4. Nota sobre reconstrução

Nos imports do executável existem APIs de manipulação de memória de outro
processo (`OpenProcess`, `ReadProcessMemory`, `WriteProcessMemory`,
`VirtualAllocEx`, `CreateRemoteThread`) e um grupo de rotinas em
`0x136e5000`–`0x136e7400` que as utiliza. A análise identifica esse grupo
como um **carregador PE em memória** — comportamento dual-use (aparece em
proteções anti-cheat/DRM e em outras categorias de software).
