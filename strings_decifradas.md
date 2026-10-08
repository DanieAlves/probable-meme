# Strings decifradas do ReetFPS.exe e da ReetFPS.dll

Quase todo texto sensivel do `ReetFPS.exe` (chaves de configuracao, textos dos cards, URLs do servidor) fica cifrado no binario e so existe em claro em tempo de execucao. Este arquivo lista o texto em claro de cada chamada ao decodificador, para que as secoes de `ponto_blank.c` possam citar o texto e nao so o endereco do blob.

## A cifra do ReetFPS.exe (`FUN_134a8d98` -> `FUN_134a8c90`)

Cada chamada passa ao decodificador o blob cifrado (`EDX`, uma AnsiString com o comprimento no dword anterior), uma chave inicial de 16 bits (`ECX`) e, na pilha, um multiplicador (1o `PUSH`) e um incremento (2o `PUSH`). Para cada byte `c` do blob o decodificador produz `c XOR (k >> 8)` e atualiza `k = ((c + k) * multiplicador + incremento) AND 0xFFFF` -- a cifra de fluxo classica de exemplos Delphi, com chave por chamada. Os dois primeiros caracteres do resultado sao descartados (`FUN_134a8d34`). Exemplo: blob `0x1369e5a0`, chave `0x89`, multiplicador `0xc9`, incremento `0xff` -> `MINIMAP`.

## Como foi validado

1. A formula foi lida do decompilado de `FUN_134a8c90` e testada a mao no blob `0x1369e5a0` (`MINIMAP`); a ordem invertida de multiplicador/incremento produz lixo.
2. O arquivo PE foi varrido por todas as instrucoes `CALL 0x134a8d98`: **1011 chamadas**. Para cada uma, o blob (`MOV EDX, imm`), a chave (`MOV ECX, imm`) e os dois `PUSH` imediatos foram lidos dos bytes que antecedem a chamada e o blob foi decifrado direto do arquivo.
3. Uma lista anterior de 181 chamadas, decodificada de forma independente, foi comparada: **154 de 154** entradas legiveis coincidiram exatamente; 6 entradas daquela lista estavam erradas e foram corrigidas pela varredura (os quatro `FPS Desbloqueado * N` usam o caractere bullet U+2022, nao ponto; `0x1354a06e` e `KILLS`; `0x1372bf70` e `REETGAMEMODE`).
4. Entradas cujo resultado nao e texto legivel (proporcao de caracteres imprimiveis < 0,9, em geral porque a heuristica pegou um PUSH que nao era do decodificador) foram **descartadas: 23**. Restaram **988** entradas.

Colunas: **Chamada** = endereco da instrucao `CALL FUN_134a8d98`; **Blob** = endereco do texto cifrado; **Secao / uso** = funcao ou secao de `ponto_blank.c` onde a chamada fica (`-` quando nao classificada). Quebras de linha do texto aparecem como ` / `.

## Chaves de configuracao e de estado (477)

| Chamada | Blob | Texto em claro | Secao / uso |
|---|---|---|---|
| `0x134a9fea` | `0x134aa028` | REETFPS | - |
| `0x134aa296` | `0x134aa3e8` | STATUS | - |
| `0x134b4050` | `0x134b40e0` | PBApp | - |
| `0x134b41f0` | `0x134b42c8` | REETFPS | - |
| `0x134b4231` | `0x134b42e0` | MSG_BOX | - |
| `0x134b4320` | `0x134b43b8` | MSG_ID | - |
| `0x134b442c` | `0x134b44c8` | MSG_ID | - |
| `0x134b4530` | `0x134b45c8` | MSG_ID2 | - |
| `0x134b463f` | `0x134b46d8` | MSG_ID2 | - |
| `0x134b4740` | `0x134b47ac` | SOUND2 | - |
| `0x13548899` | `0x13548a90` | RANK_POS | - |
| `0x135488e0` | `0x13548aa8` | KILLS | - |
| `0x13548927` | `0x13548abc` | DEATHS | - |
| `0x1354896e` | `0x13548ad4` | HEADSHOTS | - |
| `0x135489b8` | `0x13548aec` | KD_HS | - |
| `0x1354a033` | `0x1354a3fc` | NICKNAME | - |
| `0x1354a06e` | `0x1354a414` | KILLS | - |
| `0x1354a0a9` | `0x1354a428` | DEATHS | - |
| `0x1354a0e4` | `0x1354a440` | HEADSHOTS | - |
| `0x1354a11c` | `0x1354a458` | DETAILS | - |
| `0x1354a154` | `0x1354a470` | STATUS | - |
| `0x1354c4cd` | `0x1354c66c` | RANK_POS | servidor / licenca / login |
| `0x1354c50e` | `0x1354c684` | UNDEFINED | servidor / licenca / login |
| `0x1354c547` | `0x1354c69c` | MY_RANK_POS | servidor / licenca / login |
| `0x1354c589` | `0x1354c69c` | MY_RANK_POS | servidor / licenca / login |
| `0x1354c5c9` | `0x1354c6b8` | SESSION_ID | servidor / licenca / login |
| `0x1354c7e3` | `0x1354ca98` | LICENSE | servidor / licenca / login |
| `0x1354c820` | `0x1354cab0` | SESSION | servidor / licenca / login |
| `0x1354c897` | `0x1354cae0` | FORCE_RESTART | servidor / licenca / login |
| `0x1354c977` | `0x1354cb10` | FALSE | servidor / licenca / login |
| `0x1354c9b4` | `0x1354cb24` | TRUE | servidor / licenca / login |
| `0x1354cb84` | `0x1354cda8` | sessionID | servidor / licenca / login |
| `0x1354cbbc` | `0x1354cdc0` | SAVED_USER_CONFIG | servidor / licenca / login |
| `0x1354d178` | `0x1354d8e4` | REQUEST_STATUS | servidor / licenca / login |
| `0x1354d1f3` | `0x1354d918` | REMAIN_TIME | servidor / licenca / login |
| `0x1354d23c` | `0x1354d934` | CATEGORY | servidor / licenca / login |
| `0x1354d297` | `0x1354d94c` | NOTIFICATION_USER_MSG | servidor / licenca / login |
| `0x1354d2fb` | `0x1354d970` | NOTIFICATION_USER_MSG_ID | servidor / licenca / login |
| `0x1354d341` | `0x1354d998` | NOTIFICATION_USER_MSG | servidor / licenca / login |
| `0x1354d393` | `0x1354d9bc` | NOTIFICATION_SPECIFIC_USER_MSG | servidor / licenca / login |
| `0x1354d3fb` | `0x1354d9ec` | NOTIFICATION_SPECIFIC_USER_MSG_ID | servidor / licenca / login |
| `0x1354d441` | `0x1354da1c` | NOTIFICATION_SPECIFIC_USER_MSG | servidor / licenca / login |
| `0x1354d49f` | `0x1354da4c` | RANK_STRUCT | servidor / licenca / login |
| `0x1354d503` | `0x1354da68` | MY_RANK_STRUCT | servidor / licenca / login |
| `0x1354d561` | `0x1354da88` | SAVED_USER_CONFIG | servidor / licenca / login |
| `0x1354d5c2` | `0x1354daa8` | USER_CONFIG_ID | servidor / licenca / login |
| `0x1354db97` | `0x1354de88` | sessionID | servidor / licenca / login |
| `0x1354dbcc` | `0x1354dea0` | config_id | servidor / licenca / login |
| `0x1354e910` | `0x1354ea08` | ACESS_GRANTED | servidor / licenca / login |
| `0x1354eb09` | `0x1354eca8` | ACESS_ALERT | servidor / licenca / login |
| `0x1354eb44` | `0x1354ecc4` | SYSTEM | servidor / licenca / login |
| `0x1354eb82` | `0x1354ecdc` | SYSTEM2 | servidor / licenca / login |
| `0x1354f520` | `0x1354fc18` | LOGIN_EXPIRED | servidor / licenca / login |
| `0x1354f5ff` | `0x1354fccc` | CHECK_FAILED | servidor / licenca / login |
| `0x1354f689` | `0x1354fd9c` | HARDWARE_ALREADY_ASSOCIATED | servidor / licenca / login |
| `0x1355006a` | `0x1355014c` | dev_ver | servidor / licenca / login |
| `0x13551e7d` | `0x1355205c` | Win32_OperatingSystem | - |
| `0x13553015` | `0x13553118` | ReetFPS | - |
| `0x13553537` | `0x13553890` | WQL | - |
| `0x135535e2` | `0x13553890` | WQL | - |
| `0x13553ad1` | `0x13554238` | WQL | - |
| `0x13553bd4` | `0x135542e8` | WQL | - |
| `0x13553cf5` | `0x1355438c` | WQL | - |
| `0x1355499f` | `0x135549fc` | Win32_DiskDrive | - |
| `0x135965d8` | `0x13596e44` | TROQUE_ESSE_SEGREDO_GRANDE_AQUI_64_CHARS_MINIMO | API assinada (username/ts/nonce/sig) |
| `0x1359765e` | `0x13597714` | ReetFPS | API assinada (username/ts/nonce/sig) |
| `0x13597842` | `0x135978f8` | ReetFPS | API assinada (username/ts/nonce/sig) |
| `0x135fcfd4` | `0x135fd068` | Ativador_ON | avisos de plano / ISOs / toggles |
| `0x135fd0ad` | `0x135fd140` | Ativador_ON | avisos de plano / ISOs / toggles |
| `0x135fd185` | `0x135fd214` | Booster_ON | avisos de plano / ISOs / toggles |
| `0x135fd272` | `0x135fd33c` | Booster_ON | avisos de plano / ISOs / toggles |
| `0x135fd7ee` | `0x135fd8b8` | CPU_ON | avisos de plano / ISOs / toggles |
| `0x135fda18` | `0x135fdaa8` | CPU_ON | avisos de plano / ISOs / toggles |
| `0x135fde3d` | `0x135fdecc` | Energia_ON | avisos de plano / ISOs / toggles |
| `0x135fef72` | `0x135ff040` | Memoria_ON | avisos de plano / ISOs / toggles |
| `0x135ff241` | `0x135ff2d4` | Memoria_ON | - |
| `0x135ff332` | `0x135ff3c4` | Office_ON | - |
| `0x135ff408` | `0x135ff49c` | Office_ON | - |
| `0x135ff4f9` | `0x135ff5c8` | OPTurbo_ON | - |
| `0x135ff744` | `0x135ff7d8` | OPTurbo_ON | - |
| `0x135ff84f` | `0x135ff918` | Ping_ON | - |
| `0x135ffa51` | `0x135ffae0` | Ping_ON | - |
| `0x135ffb3a` | `0x135ffc04` | Power_ON | - |
| `0x135ffd7d` | `0x135ffe0c` | Power_ON | - |
| `0x135ffe69` | `0x135fff38` | PrioridadeGames_ON | - |
| `0x13600150` | `0x136001e4` | PrioridadeGames_ON | - |
| `0x13601022` | `0x136010f0` | Tweaks_ON | - |
| `0x136012e9` | `0x1360137c` | Tweaks_ON | - |
| `0x13690027` | `0x13690194` | ACTIVE | - |
| `0x13690054` | `0x136901ac` | RESOLUTION1 | - |
| `0x13690257` | `0x136903c0` | ACTIVE | - |
| `0x13690284` | `0x136903d8` | RESOLUTION2 | - |
| `0x13690483` | `0x136905f4` | ACTIVE | - |
| `0x136904b0` | `0x1369060c` | RESOLUTION3 | - |
| `0x136906bf` | `0x1369082c` | ACTIVE | - |
| `0x136906ec` | `0x13690844` | RESOLUTION4 | - |
| `0x136908f7` | `0x13690a64` | ACTIVE | - |
| `0x13690924` | `0x13690a7c` | RESOLUTION5 | - |
| `0x13690b2f` | `0x13690c9c` | ACTIVE | - |
| `0x13690b5c` | `0x13690cb4` | RESOLUTION6 | - |
| `0x13693970` | `0x13693a48` | MAX | secao 17 rotulos de preset |
| `0x13697b92` | `0x1369875c` | current_crosshair | secao 13 miras |
| `0x13697bc9` | `0x1369877c` | crosshair1_sizeline | secao 13 miras |
| `0x13697c11` | `0x136987a0` | crosshair1_space | secao 13 miras |
| `0x13697c56` | `0x136987c0` | crosshair1_square | secao 13 miras |
| `0x13697c9e` | `0x136987e0` | crosshair1_color | secao 13 miras |
| `0x13697d8f` | `0x13698810` | current_crosshair | secao 13 miras |
| `0x13697dc6` | `0x13698830` | crosshair2_sizeline | secao 13 miras |
| `0x13697e0e` | `0x13698854` | crosshair2_space | secao 13 miras |
| `0x13697ea1` | `0x13698894` | crosshair2_color | secao 13 miras |
| `0x13697fa1` | `0x136988c4` | current_crosshair | secao 13 miras |
| `0x13697fe4` | `0x136988e4` | crosshair3_sizeline | secao 13 miras |
| `0x1369803e` | `0x13698908` | crosshair3_space | secao 13 miras |
| `0x13698098` | `0x13698928` | crosshair3_square | secao 13 miras |
| `0x136980f2` | `0x13698948` | crosshair3_color | secao 13 miras |
| `0x136981f8` | `0x13698978` | current_crosshair | secao 13 miras |
| `0x1369823e` | `0x13698998` | crosshair4_sizeline | secao 13 miras |
| `0x1369829b` | `0x136989bc` | crosshair4_space | secao 13 miras |
| `0x136982f8` | `0x136989dc` | crosshair4_square | secao 13 miras |
| `0x13698355` | `0x136989fc` | crosshair4_color | secao 13 miras |
| `0x13698451` | `0x13698a5c` | ACTIVE | secao 13 miras |
| `0x13698488` | `0x13698a74` | crosshair_status | secao 13 miras |
| `0x1369af42` | `0x1369b0d4` | FPS360 | store: limpeza FPS* (FUN_1369af04) |
| `0x1369af70` | `0x1369b0ec` | FPS777 | store: limpeza FPS* (FUN_1369af04) |
| `0x1369af9e` | `0x1369b104` | FPS500 | store: limpeza FPS* (FUN_1369af04) |
| `0x1369afd2` | `0x1369b11c` | FPS999 | store: limpeza FPS* (FUN_1369af04) |
| `0x1369b003` | `0x1369b134` | FPS1500 | store: limpeza FPS* (FUN_1369af04) |
| `0x1369b031` | `0x1369b14c` | FPSMAX | store: limpeza FPS* (FUN_1369af04) |
| `0x1369b1d4` | `0x1369b6ec` | ReetFPS_CFG | store JSON (FUN_1369b158) |
| `0x1369b250` | `0x1369b6ec` | ReetFPS_CFG | store JSON (FUN_1369b158) |
| `0x1369b2a9` | `0x1369b738` | RESOLUTION1 | store JSON (FUN_1369b158) |
| `0x1369b2ef` | `0x1369b754` | RESOLUTION2 | store JSON (FUN_1369b158) |
| `0x1369b335` | `0x1369b770` | RESOLUTION3 | store JSON (FUN_1369b158) |
| `0x1369b37e` | `0x1369b78c` | RESOLUTION4 | store JSON (FUN_1369b158) |
| `0x1369b3c7` | `0x1369b7a8` | RESOLUTION5 | store JSON (FUN_1369b158) |
| `0x1369b40d` | `0x1369b7c4` | RESOLUTION6 | store JSON (FUN_1369b158) |
| `0x1369b453` | `0x1369b7e0` | RESOLUTION1 | store JSON (FUN_1369b158) |
| `0x1369b486` | `0x1369b7fc` | RESOLUTION2 | store JSON (FUN_1369b158) |
| `0x1369b4bc` | `0x1369b818` | RESOLUTION3 | store JSON (FUN_1369b158) |
| `0x1369b4ef` | `0x1369b834` | RESOLUTION4 | store JSON (FUN_1369b158) |
| `0x1369b522` | `0x1369b850` | RESOLUTION5 | store JSON (FUN_1369b158) |
| `0x1369b558` | `0x1369b86c` | RESOLUTION6 | store JSON (FUN_1369b158) |
| `0x1369bf00` | `0x1369e31c` | FPS_SELECTION_INDEX | restauracao do painel (FUN_1369beb0) |
| `0x1369bf58` | `0x1369e340` | RESOLUTION1 | restauracao do painel (FUN_1369beb0) |
| `0x1369bfbf` | `0x1369e35c` | RESOLUTION2 | restauracao do painel (FUN_1369beb0) |
| `0x1369c020` | `0x1369e378` | RESOLUTION3 | restauracao do painel (FUN_1369beb0) |
| `0x1369c081` | `0x1369e394` | RESOLUTION4 | restauracao do painel (FUN_1369beb0) |
| `0x1369c0e2` | `0x1369e3b0` | RESOLUTION5 | restauracao do painel (FUN_1369beb0) |
| `0x1369c146` | `0x1369e3cc` | RESOLUTION6 | restauracao do painel (FUN_1369beb0) |
| `0x1369c1a8` | `0x1369e3e8` | CUSTOMRES | restauracao do painel (FUN_1369beb0) |
| `0x1369c219` | `0x1369e400` | BORDER_LESS | restauracao do painel (FUN_1369beb0) |
| `0x1369c28d` | `0x1369e41c` | KEYBOARD | restauracao do painel (FUN_1369beb0) |
| `0x1369c2fe` | `0x1369e434` | FPS_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c3db` | `0x1369e450` | RAM_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c4c1` | `0x1369e46c` | CPU_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c5a1` | `0x1369e488` | GPU_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c684` | `0x1369e4a4` | HORA_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c76d` | `0x1369e4c0` | CROSSHAIR_SHADOW | restauracao do painel (FUN_1369beb0) |
| `0x1369c7ec` | `0x1369e4e0` | HORIZONTAL_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c88f` | `0x1369e504` | VERTICAL_COUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369c999` | `0x1369e544` | FONTE_PERSONALIZADA | restauracao do painel (FUN_1369beb0) |
| `0x1369ca16` | `0x1369e568` | PRIORITYPB | restauracao do painel (FUN_1369beb0) |
| `0x1369ca90` | `0x1369e584` | COUNTERPING | restauracao do painel (FUN_1369beb0) |
| `0x1369cb10` | `0x1369e5a0` | MINIMAP | restauracao do painel (FUN_1369beb0) |
| `0x1369cb87` | `0x1369e5b8` | REETSTATS | restauracao do painel (FUN_1369beb0) |
| `0x1369cc04` | `0x1369e5d0` | HUDPLAYERS | restauracao do painel (FUN_1369beb0) |
| `0x1369cc84` | `0x1369e5ec` | OPTIMIZER_PB_MANAGER | restauracao do painel (FUN_1369beb0) |
| `0x1369ccfe` | `0x1369e610` | FLUIDEZMAX | restauracao do painel (FUN_1369beb0) |
| `0x1369cd83` | `0x1369e62c` | FPSCOUNTER_POS_1 | restauracao do painel (FUN_1369beb0) |
| `0x1369ce2c` | `0x1369e64c` | FPSCOUNTER_POS_2 | restauracao do painel (FUN_1369beb0) |
| `0x1369ced2` | `0x1369e66c` | FPSCOUNTER_POS_3 | restauracao do painel (FUN_1369beb0) |
| `0x1369cf78` | `0x1369e68c` | FPSCOUNTER_POS_4 | restauracao do painel (FUN_1369beb0) |
| `0x1369d027` | `0x1369e6ac` | HUD_PLAYERS_DEATH | restauracao do painel (FUN_1369beb0) |
| `0x1369d0b4` | `0x1369e6cc` | HUD_PLAYERS_HP | restauracao do painel (FUN_1369beb0) |
| `0x1369d147` | `0x1369e6ec` | HUD_PLAYERS_CARD | restauracao do painel (FUN_1369beb0) |
| `0x1369d1dd` | `0x1369e70c` | SKIN_GLOVES | restauracao do painel (FUN_1369beb0) |
| `0x1369d25d` | `0x1369e728` | FPSCOUNTER | restauracao do painel (FUN_1369beb0) |
| `0x1369d2d4` | `0x1369e744` | FULLSCREEN | restauracao do painel (FUN_1369beb0) |
| `0x1369d34b` | `0x1369e760` | LOADINGMAP | restauracao do painel (FUN_1369beb0) |
| `0x1369d3c2` | `0x1369e77c` | INTERFACE | restauracao do painel (FUN_1369beb0) |
| `0x1369d43f` | `0x1369e794` | WEAPON_LEFTY | restauracao do painel (FUN_1369beb0) |
| `0x1369d4b9` | `0x1369e7b0` | SENSI | restauracao do painel (FUN_1369beb0) |
| `0x1369d539` | `0x1369e7c4` | REETHUB_VOICE | restauracao do painel (FUN_1369beb0) |
| `0x1369d5cd` | `0x1369e7e0` | WEAPON_EFFECT | restauracao do painel (FUN_1369beb0) |
| `0x1369d620` | `0x1369e7fc` | REETGAMEMODE | restauracao do painel (FUN_1369beb0) |
| `0x1369d6a2` | `0x1369e818` | SET_INTERFACE | restauracao do painel (FUN_1369beb0) |
| `0x1369d6ea` | `0x1369e834` | crosshair1_sizeline | restauracao do painel (FUN_1369beb0) |
| `0x1369d747` | `0x1369e858` | crosshair1_space | restauracao do painel (FUN_1369beb0) |
| `0x1369d7a4` | `0x1369e878` | crosshair1_square | restauracao do painel (FUN_1369beb0) |
| `0x1369d801` | `0x1369e898` | crosshair1_color | restauracao do painel (FUN_1369beb0) |
| `0x1369d861` | `0x1369e8b8` | crosshair2_sizeline | restauracao do painel (FUN_1369beb0) |
| `0x1369d8be` | `0x1369e8dc` | crosshair2_space | restauracao do painel (FUN_1369beb0) |
| `0x1369d91e` | `0x1369e8fc` | crosshair2_square | restauracao do painel (FUN_1369beb0) |
| `0x1369d97e` | `0x1369e91c` | crosshair2_color | restauracao do painel (FUN_1369beb0) |
| `0x1369d9db` | `0x1369e93c` | crosshair3_sizeline | restauracao do painel (FUN_1369beb0) |
| `0x1369da38` | `0x1369e960` | crosshair3_space | restauracao do painel (FUN_1369beb0) |
| `0x1369da95` | `0x1369e980` | crosshair3_square | restauracao do painel (FUN_1369beb0) |
| `0x1369daf2` | `0x1369e9a0` | crosshair3_color | restauracao do painel (FUN_1369beb0) |
| `0x1369db4f` | `0x1369e9c0` | crosshair4_sizeline | restauracao do painel (FUN_1369beb0) |
| `0x1369dbac` | `0x1369e9e4` | crosshair4_space | restauracao do painel (FUN_1369beb0) |
| `0x1369dc09` | `0x1369ea04` | crosshair4_square | restauracao do painel (FUN_1369beb0) |
| `0x1369dc69` | `0x1369ea24` | crosshair4_color | restauracao do painel (FUN_1369beb0) |
| `0x1369dcc6` | `0x1369ea44` | current_crosshair | restauracao do painel (FUN_1369beb0) |
| `0x1369dd21` | `0x1369ea64` | crosshair_status | restauracao do painel (FUN_1369beb0) |
| `0x136a00e4` | `0x136a01e8` | FPS_COUNTER | contador de FPS (overlay) |
| `0x136a0114` | `0x136a0204` | RAM_COUNTER | contador de FPS (overlay) |
| `0x136a0144` | `0x136a0220` | CPU_COUNTER | contador de FPS (overlay) |
| `0x136a0174` | `0x136a023c` | GPU_COUNTER | contador de FPS (overlay) |
| `0x136a01a1` | `0x136a0258` | HORA_COUNTER | contador de FPS (overlay) |
| `0x136a0616` | `0x136a06d4` | HORIZONTAL_COUNTER | contador de FPS (overlay) |
| `0x136a08ae` | `0x136a09fc` | HORIZONTAL_COUNTER | contador de FPS (overlay) |
| `0x136a08ee` | `0x136a0a38` | VERTICAL_COUNTER | contador de FPS (overlay) |
| `0x136a0931` | `0x136a0a58` | VERTICAL_COUNTER | contador de FPS (overlay) |
| `0x136a0974` | `0x136a0a78` | HORIZONTAL_COUNTER | contador de FPS (overlay) |
| `0x136a0bf7` | `0x136a0d38` | FPSCOUNTER_POS_1 | contador de FPS (overlay) |
| `0x136a0c3a` | `0x136a0d70` | FPSCOUNTER_POS_2 | contador de FPS (overlay) |
| `0x136a0c75` | `0x136a0d90` | FPSCOUNTER_POS_3 | contador de FPS (overlay) |
| `0x136a0cb0` | `0x136a0db0` | FPSCOUNTER_POS_4 | contador de FPS (overlay) |
| `0x136a0e39` | `0x136a0f74` | FPSCOUNTER_POS_3 | contador de FPS (overlay) |
| `0x136a0e76` | `0x136a0fac` | FPSCOUNTER_POS_1 | contador de FPS (overlay) |
| `0x136a0eb1` | `0x136a0fcc` | FPSCOUNTER_POS_2 | contador de FPS (overlay) |
| `0x136a0eec` | `0x136a0fec` | FPSCOUNTER_POS_4 | contador de FPS (overlay) |
| `0x136a107b` | `0x136a11b8` | FPSCOUNTER_POS_2 | contador de FPS (overlay) |
| `0x136a10bb` | `0x136a11f0` | FPSCOUNTER_POS_1 | contador de FPS (overlay) |
| `0x136a10f3` | `0x136a1210` | FPSCOUNTER_POS_3 | contador de FPS (overlay) |
| `0x136a112e` | `0x136a1230` | FPSCOUNTER_POS_4 | contador de FPS (overlay) |
| `0x136a12bf` | `0x136a1404` | FPSCOUNTER_POS_4 | contador de FPS (overlay) |
| `0x136a1302` | `0x136a143c` | FPSCOUNTER_POS_1 | contador de FPS (overlay) |
| `0x136a1340` | `0x136a145c` | FPSCOUNTER_POS_2 | contador de FPS (overlay) |
| `0x136a137b` | `0x136a147c` | FPSCOUNTER_POS_3 | contador de FPS (overlay) |
| `0x136b04bc` | `0x136b0588` | ADMENU_ON | secoes 15/16 toggles do Windows |
| `0x136b06f8` | `0x136b078c` | ADMENU_ON | secoes 15/16 toggles do Windows |
| `0x136b07d0` | `0x136b089c` | AjustesDesempenho_ON | secoes 15/16 toggles do Windows |
| `0x136b09fd` | `0x136b0a90` | AjustesDesempenho_ON | secoes 15/16 toggles do Windows |
| `0x136b0ae3` | `0x136b0b7c` | APPS_ON | secoes 15/16 toggles do Windows |
| `0x136b0fcc` | `0x136b105c` | APPS_ON | secoes 15/16 toggles do Windows |
| `0x136b109a` | `0x136b1114` | Ativador_ON | secoes 15/16 toggles do Windows |
| `0x136b1156` | `0x136b11d0` | Ativador_ON | secoes 15/16 toggles do Windows |
| `0x136b1215` | `0x136b12e4` | Cortana_ON | secoes 15/16 toggles do Windows |
| `0x136b142d` | `0x136b14c0` | Cortana_ON | secoes 15/16 toggles do Windows |
| `0x136b1505` | `0x136b15d4` | DarkTheme_ON | secoes 15/16 toggles do Windows |
| `0x136b16f4` | `0x136b1788` | DarkTheme_ON | secoes 15/16 toggles do Windows |
| `0x136b17d0` | `0x136b189c` | Fax_ON | secoes 15/16 toggles do Windows |
| `0x136b19f1` | `0x136b1a84` | Fax_ON | secoes 15/16 toggles do Windows |
| `0x136b1fd0` | `0x136b20e8` | GameBar_ON | secoes 15/16 toggles do Windows |
| `0x136b2259` | `0x136b22e8` | GameBar_ON | secoes 15/16 toggles do Windows |
| `0x136b2380` | `0x136b2498` | GameDVR_ON | secoes 15/16 toggles do Windows |
| `0x136b25f1` | `0x136b2680` | GameDVR_ON | secoes 15/16 toggles do Windows |
| `0x136b26c5` | `0x136b2794` | Hibernate_ON | secoes 15/16 toggles do Windows |
| `0x136b28d5` | `0x136b2968` | Hibernate_ON | secoes 15/16 toggles do Windows |
| `0x136b29b0` | `0x136b2a7c` | TarefaTelemetria_ON | secoes 15/16 toggles do Windows |
| `0x136b2be8` | `0x136b2c78` | TarefaTelemetria_ON | secoes 15/16 toggles do Windows |
| `0x136b2cc5` | `0x136b2d94` | TeclasAderencia_ON | secoes 15/16 toggles do Windows |
| `0x136b2f01` | `0x136b2f90` | TeclasAderencia_ON | secoes 15/16 toggles do Windows |
| `0x136b2fdd` | `0x136b30ac` | TelemetriaChrome_ON | secoes 15/16 toggles do Windows |
| `0x136b3219` | `0x136b32ac` | TelemetriaChrome_ON | secoes 15/16 toggles do Windows |
| `0x136b3631` | `0x136b3700` | Transparency_ON | secoes 15/16 toggles do Windows |
| `0x136b3871` | `0x136b3940` | Transparency_ON | secoes 15/16 toggles do Windows |
| `0x136b3aa8` | `0x136b3b70` | IniciarSistema_ON | secoes 15/16 toggles do Windows |
| `0x136b3d10` | `0x136b3da0` | IniciarSistema_ON | secoes 15/16 toggles do Windows |
| `0x136b3de9` | `0x136b3eb0` | Latency_ON | secoes 15/16 toggles do Windows |
| `0x136b4070` | `0x136b4100` | Latency_ON | secoes 15/16 toggles do Windows |
| `0x136b4145` | `0x136b420c` | Network_ON | secoes 15/16 toggles do Windows |
| `0x136b43a5` | `0x136b4434` | Network_ON | secoes 15/16 toggles do Windows |
| `0x136b4479` | `0x136b4548` | Notification_ON | secoes 15/16 toggles do Windows |
| `0x136b46bd` | `0x136b4750` | Notification_ON | secoes 15/16 toggles do Windows |
| `0x136b47c1` | `0x136b4890` | GameMode_ON | secoes 15/16 toggles do Windows |
| `0x136b4a6d` | `0x136b4b00` | GameMode_ON | secoes 15/16 toggles do Windows |
| `0x136b4b45` | `0x136b4c14` | GrupoHome_ON | secoes 15/16 toggles do Windows |
| `0x136b4e21` | `0x136b4eb4` | GrupoHome_ON | secoes 15/16 toggles do Windows |
| `0x136b4ef9` | `0x136b4fc0` | OneDrive_ON | secoes 15/16 toggles do Windows |
| `0x136b5135` | `0x136b51c4` | OneDrive_ON | secoes 15/16 toggles do Windows |
| `0x136b521d` | `0x136b52ec` | OpMouse_ON | secoes 15/16 toggles do Windows |
| `0x136b5435` | `0x136b54c4` | OpMouse_ON | secoes 15/16 toggles do Windows |
| `0x136b550c` | `0x136b55d8` | OpTeclado_ON | secoes 15/16 toggles do Windows |
| `0x136b573c` | `0x136b57d0` | OpTeclado_ON | secoes 15/16 toggles do Windows |
| `0x136b58e5` | `0x136b595c` | Office_ON | secoes 15/16 toggles do Windows |
| `0x136b599d` | `0x136b5a14` | Office_ON | secoes 15/16 toggles do Windows |
| `0x136b5a55` | `0x136b5b24` | Services_ON | secoes 15/16 toggles do Windows |
| `0x136b5c6d` | `0x136b5d00` | Services_ON | secoes 15/16 toggles do Windows |
| `0x136b5d48` | `0x136b5e14` | Superfetch_ON | secoes 15/16 toggles do Windows |
| `0x136b5f74` | `0x136b6008` | Superfetch_ON | secoes 15/16 toggles do Windows |
| `0x136b6050` | `0x136b611c` | Update_ON | secoes 15/16 toggles do Windows |
| `0x136b62a8` | `0x136b633c` | Update_ON | - |
| `0x136b6380` | `0x136b644c` | Volume_ON | - |
| `0x136b6624` | `0x136b66b8` | Volume_ON | - |
| `0x136b66fc` | `0x136b67c4` | XboxLive_ON | - |
| `0x136b69d0` | `0x136b6a60` | XboxLive_ON | - |
| `0x136c0b57` | `0x136c0be4` | AMDRESPOSTA | secao 23 GPU / NVIDIA |
| `0x136c0c2f` | `0x136c0cbc` | AMDRESPOSTA | secao 23 GPU / NVIDIA |
| `0x136c0d04` | `0x136c0d94` | CONTROLPOTENCYAMD | secao 23 GPU / NVIDIA |
| `0x136c0de3` | `0x136c0e70` | CONTROLPOTENCYAMD | secao 23 GPU / NVIDIA |
| `0x136c0ebc` | `0x136c0f4c` | DESEMPENHONVIDIA | secao 23 GPU / NVIDIA |
| `0x136c0f9b` | `0x136c1028` | DESEMPENHONVIDIA | secao 23 GPU / NVIDIA |
| `0x136c1071` | `0x136c1100` | FLUXODADOSAMD | secao 23 GPU / NVIDIA |
| `0x136c114b` | `0x136c11d8` | FLUXODADOSAMD | secao 23 GPU / NVIDIA |
| `0x136c12af` | `0x136c133c` | LATENCYNVIDIA | secao 23 GPU / NVIDIA |
| `0x136c1384` | `0x136c1414` | LATENCYNVIDIA | secao 23 GPU / NVIDIA |
| `0x136c145f` | `0x136c14ec` | MEMORYINTEL | secao 23 GPU / NVIDIA |
| `0x136c1534` | `0x136c15c4` | MEMORYINTEL | secao 23 GPU / NVIDIA |
| `0x136c160c` | `0x136c169c` | NUCLEOAMD | secao 23 GPU / NVIDIA |
| `0x136c16e0` | `0x136c1770` | NUCLEOAMD | secao 23 GPU / NVIDIA |
| `0x136c2300` | `0x136c2390` | NVIDIABOOST | secao 23 GPU / NVIDIA |
| `0x136c24b0` | `0x136c2540` | SINCROINTEL | secao 23 GPU / NVIDIA |
| `0x136c2588` | `0x136c2618` | TELEMETRYNVIDIA | secao 23 GPU / NVIDIA |
| `0x136c2664` | `0x136c26f4` | TELEMETRYNVIDIA | secao 23 GPU / NVIDIA |
| `0x136c2740` | `0x136c27d0` | TURBONUCLEOINTEL | secao 23 GPU / NVIDIA |
| `0x136c281c` | `0x136c28ac` | TURBONUCLEOINTEL | secao 23 GPU / NVIDIA |
| `0x137149a3` | `0x13714aa8` | REETFPS_LOGIN | limpeza / login |
| `0x137149f0` | `0x13714ac4` | REETFPS_PASSWORD | limpeza / login |
| `0x13727646` | `0x137276f4` | ENABLE_RANK | - |
| `0x13727685` | `0x13727710` | FALSE | - |
| `0x13727751` | `0x13727800` | ENABLE_INTERACTIVE | - |
| `0x13727790` | `0x13727824` | FALSE | - |
| `0x1372785f` | `0x13727944` | current_crosshair | - |
| `0x13727a9b` | `0x13727f88` | crosshair1_sizeline | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727ac9` | `0x13727fac` | crosshair1_space | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727afa` | `0x13727fcc` | crosshair1_square | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727b53` | `0x1372800c` | crosshair2_sizeline | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727b81` | `0x13728030` | crosshair2_space | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727bb2` | `0x13728050` | crosshair2_square | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727be0` | `0x13728070` | crosshair2_color | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727c0e` | `0x13728090` | crosshair3_sizeline | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727c3c` | `0x137280b4` | crosshair3_space | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727c6a` | `0x137280d4` | crosshair3_square | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727c98` | `0x137280f4` | crosshair3_color | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727cc6` | `0x13728114` | crosshair4_sizeline | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727cf4` | `0x13728138` | crosshair4_space | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727d22` | `0x13728158` | crosshair4_square | secao 21 reset de miras (FUN_13727a5c) |
| `0x13727d53` | `0x13728178` | crosshair4_color | secao 21 reset de miras (FUN_13727a5c) |
| `0x137281c3` | `0x13728220` | crosshair_status | secao 21 reset de miras (FUN_13727a5c) |
| `0x13728288` | `0x13728488` | ACTIVE | card COUNTERPING |
| `0x137282b5` | `0x137284a0` | COUNTERPING | card COUNTERPING |
| `0x137283e4` | `0x1372856c` | ReetFPS | card COUNTERPING |
| `0x137285a9` | `0x13728634` | COUNTERPING | card COUNTERPING |
| `0x137286ae` | `0x13728828` | RESOLUTION2 | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x137286dc` | `0x13728844` | RESOLUTION3 | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x1372870a` | `0x13728860` | RESOLUTION4 | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x13728738` | `0x1372887c` | RESOLUTION5 | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x13728766` | `0x13728898` | RESOLUTION6 | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x137288f6` | `0x137289f4` | ACTIVE | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x13728926` | `0x13728a0c` | CUSTOMRES | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x13728a51` | `0x13728b28` | CUSTOMRES | resolucao (RESOLUTION*/CUSTOMRES) |
| `0x13728c42` | `0x13728cac` | FPS_SELECTION_INDEX | secao 20 desbloqueador |
| `0x13728da3` | `0x13728ef8` | ACTIVE | secao 18 MAPLOADING |
| `0x13728dd0` | `0x13728f10` | LOADINGMAP | secao 18 MAPLOADING |
| `0x13728e6a` | `0x13728fbc` | ReetFPS | secao 18 MAPLOADING |
| `0x13728ff9` | `0x13729084` | LOADINGMAP | secao 18 MAPLOADING |
| `0x137290e9` | `0x137292d8` | ACTIVE | secao 19 MINIMAP |
| `0x13729116` | `0x137292f0` | MINIMAP | secao 19 MINIMAP |
| `0x13729243` | `0x13729404` | ReetFPS | secao 19 MINIMAP |
| `0x13729441` | `0x137294cc` | MINIMAP | secao 19 MINIMAP |
| `0x1372962b` | `0x1372977c` | ACTIVE | card REETSTATS |
| `0x13729658` | `0x13729794` | REETSTATS | card REETSTATS |
| `0x137296ec` | `0x13729870` | ReetFPS | card REETSTATS |
| `0x137298ad` | `0x13729938` | REETSTATS | card REETSTATS |
| `0x13729978` | `0x13729a04` | ACTIVE | card REETSTATS |
| `0x13729ca1` | `0x13729e4c` | ENABLE_INTERACTIVE | card ENABLE_INTERACTIVE |
| `0x13729db8` | `0x13729f04` | ReetFPS | card ENABLE_INTERACTIVE |
| `0x13729f5f` | `0x1372a010` | FALSE | card ENABLE_INTERACTIVE |
| `0x13729f8f` | `0x1372a024` | ENABLE_INTERACTIVE | card ENABLE_INTERACTIVE |
| `0x1372a0c1` | `0x1372a2a4` | ACTIVE | secao 9 FLUIDEZMAX |
| `0x1372a0ee` | `0x1372a2bc` | FLUIDEZMAX | secao 9 FLUIDEZMAX |
| `0x1372a1ff` | `0x1372a370` | ReetFPS | secao 9 FLUIDEZMAX |
| `0x1372a3ad` | `0x1372a438` | FLUIDEZMAX | secao 9 FLUIDEZMAX |
| `0x1372a4cd` | `0x1372a6b4` | ACTIVE | card HUDPLAYERS |
| `0x1372a4fd` | `0x1372a6cc` | HUDPLAYERS | card HUDPLAYERS |
| `0x1372a5fb` | `0x1372a7b8` | ReetFPS | card HUDPLAYERS |
| `0x1372a7f8` | `0x1372a880` | HUDPLAYERS | card HUDPLAYERS |
| `0x1372a936` | `0x1372ab30` | ACTIVE | card FPSCOUNTER |
| `0x1372a966` | `0x1372ab48` | FPSCOUNTER | card FPSCOUNTER |
| `0x1372aa8d` | `0x1372ac48` | ReetFPS | card FPSCOUNTER |
| `0x1372ac95` | `0x1372ad20` | FPSCOUNTER | card FPSCOUNTER |
| `0x1372ade9` | `0x1372af60` | FONTE_PERSONALIZADA | fonte personalizada |
| `0x1372ae24` | `0x1372af84` | FONTE_PERSONALIZADA_INDEX | fonte personalizada |
| `0x1372ae5f` | `0x1372afac` | FONTE_PERSONALIZADA_VALUE | fonte personalizada |
| `0x1372ae9a` | `0x1372afd4` | FONTE_PERSONALIZADA_INITIALIZED | fonte personalizada |
| `0x1372b197` | `0x1372b2a4` | ReetFPS | secao 10 Timer Resolution |
| `0x1372b310` | `0x1372b518` | ENABLE_RANK | card ENABLE_RANK |
| `0x1372b485` | `0x1372b61c` | ReetFPS | card ENABLE_RANK |
| `0x1372b65d` | `0x1372b728` | FALSE | card ENABLE_RANK |
| `0x1372b68d` | `0x1372b73c` | ENABLE_RANK | card ENABLE_RANK |
| `0x1372bbe5` | `0x1372bc40` | OPTIMIZER_PB_MANAGER | OPTIMIZER_PB_MANAGER |
| `0x1372bc99` | `0x1372bd98` | ACTIVE | OPTIMIZER_PB_MANAGER |
| `0x1372bccc` | `0x1372bdb0` | OPTIMIZER_PB_MANAGER | OPTIMIZER_PB_MANAGER |
| `0x1372bf70` | `0x1372bffc` | REETGAMEMODE | REETGAMEMODE |
| `0x1372c05d` | `0x1372c140` | ACTIVE | REETGAMEMODE |
| `0x1372c08d` | `0x1372c158` | REETGAMEMODE | REETGAMEMODE |
| `0x1372cd80` | `0x1372cdec` | WEAPON_LEFTY | WEAPON_LEFTY / SENSI |
| `0x1372ce2f` | `0x1372cf28` | ACTIVE | WEAPON_LEFTY / SENSI |
| `0x1372ce5f` | `0x1372cf40` | WEAPON_LEFTY | WEAPON_LEFTY / SENSI |
| `0x1372cfd3` | `0x1372d1f0` | SENSI | WEAPON_LEFTY / SENSI |
| `0x1372d00e` | `0x1372d204` | SENSI | WEAPON_LEFTY / SENSI |
| `0x1372d045` | `0x1372d218` | SENSI_BIND_DOWN | WEAPON_LEFTY / SENSI |
| `0x1372d080` | `0x1372d238` | SENSI_BIND_DOWN | WEAPON_LEFTY / SENSI |
| `0x1372d0b4` | `0x1372d258` | SENSI_BIND_UP | WEAPON_LEFTY / SENSI |
| `0x1372d0ef` | `0x1372d274` | SENSI_BIND_UP | WEAPON_LEFTY / SENSI |
| `0x1372d2cb` | `0x1372d344` | SENSI | WEAPON_LEFTY / SENSI |
| `0x1372d385` | `0x1372d468` | ACTIVE | WEAPON_LEFTY / SENSI |
| `0x1372d3b5` | `0x1372d480` | SENSI | WEAPON_LEFTY / SENSI |
| `0x1372d519` | `0x1372d7f8` | ACTIVE | secao 16 INTERFACEDELAY |
| `0x1372d549` | `0x1372d810` | INTERFACE | secao 16 INTERFACEDELAY |
| `0x1372d582` | `0x1372d7f8` | ACTIVE | secao 16 INTERFACEDELAY |
| `0x1372d5b5` | `0x1372d828` | SET_INTERFACE | secao 16 INTERFACEDELAY |
| `0x1372d5f4` | `0x1372d844` | SET_INTERFACE2 | secao 16 INTERFACEDELAY |
| `0x1372d722` | `0x1372d93c` | ReetFPS | secao 16 INTERFACEDELAY |
| `0x1372d990` | `0x1372dab0` | INTERFACE | secao 16 INTERFACEDELAY |
| `0x1372d9c8` | `0x1372dac8` | SET_INTERFACE | secao 16 INTERFACEDELAY |
| `0x1372da06` | `0x1372dae4` | SET_INTERFACE2 | secao 16 INTERFACEDELAY |
| `0x1372db7b` | `0x1372dd84` | ACTIVE | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372dba8` | `0x1372dd9c` | KEYBOARD | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372dce6` | `0x1372dee8` | ReetFPS | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372df31` | `0x1372dfa4` | KEYBOARD | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372e053` | `0x1372e228` | ACTIVE | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e080` | `0x1372e240` | FULLSCREEN | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e184` | `0x1372e2f8` | ReetFPS | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e335` | `0x1372e3c0` | FULLSCREEN | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e440` | `0x1372e668` | ACTIVE | secao 22 BORDER_LESS |
| `0x1372e46d` | `0x1372e680` | BORDER_LESS | secao 22 BORDER_LESS |
| `0x1372e5c2` | `0x1372e7b0` | ReetFPS | secao 22 BORDER_LESS |
| `0x1372e7ed` | `0x1372e878` | BORDER_LESS | secao 22 BORDER_LESS |
| `0x1372e8eb` | `0x1372eac0` | ACTIVE | secao 21 PRIORITYPB |
| `0x1372e91b` | `0x1372ead8` | PRIORITYPB | secao 21 PRIORITYPB |
| `0x1372ea22` | `0x1372eba4` | ReetFPS | secao 21 PRIORITYPB |
| `0x1372ec05` | `0x1372ec5c` | PRIORITYPB | secao 21 PRIORITYPB |
| `0x1372ece1` | `0x1372edc0` | ACTIVE | ClearTempFiles |
| `0x1377b48d` | `0x1377b56c` | ReetFPS | - |
| `0x1377e593` | `0x13780644` | GameDVR_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e60a` | `0x13780660` | GameBar_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e681` | `0x1378067c` | Hibernate_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e6f8` | `0x13780698` | OpMouse_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e76c` | `0x137806b4` | Services_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e851` | `0x137806ec` | Update_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e8b3` | `0x13780704` | GameMode_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e915` | `0x13780720` | IniciarSistema_ON | varredura de toggles (FUN_1377e480) |
| `0x1377e97a` | `0x13780740` | Notification_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ea00` | `0x13780760` | OpTeclado_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ea89` | `0x1378077c` | APPS_ON | varredura de toggles (FUN_1377e480) |
| `0x1377eb15` | `0x13780794` | OneDrive_ON | varredura de toggles (FUN_1377e480) |
| `0x1377eb9e` | `0x137807b0` | Transparency_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ec27` | `0x137807d0` | Latency_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ec9e` | `0x137807ec` | TarefaTelemetria_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ed2a` | `0x13780810` | TeclasAderencia_ON | varredura de toggles (FUN_1377e480) |
| `0x1377edb3` | `0x13780834` | TelemetriaChrome_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ee39` | `0x13780858` | TelemetriaOffice_ON | varredura de toggles (FUN_1377e480) |
| `0x1377eebf` | `0x1378087c` | TPM_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ef33` | `0x13780894` | Network_ON | varredura de toggles (FUN_1377e480) |
| `0x1377efa7` | `0x137808b0` | GrupoHome_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f01b` | `0x137808cc` | SmartScreen_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f092` | `0x137808ec` | SMB_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f106` | `0x13780904` | Superfetch_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f18f` | `0x13780920` | Volume_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f218` | `0x13780938` | XboxLive_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f2a1` | `0x13780954` | Fax_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f315` | `0x1378096c` | AjustesDesempenho_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f389` | `0x13780990` | DarkTheme_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f412` | `0x137809ac` | ADMENU_ON | varredura de toggles (FUN_1377e480) |
| `0x1377f498` | `0x137809c4` | LATENCYNVIDIA | varredura de toggles (FUN_1377e480) |
| `0x1377f533` | `0x137809e0` | MEMORYINTEL | varredura de toggles (FUN_1377e480) |
| `0x1377f5cb` | `0x137809fc` | NUCLEOAMD | varredura de toggles (FUN_1377e480) |
| `0x1377f663` | `0x13780a14` | NVIDIABOOST | varredura de toggles (FUN_1377e480) |
| `0x1377f6fb` | `0x13780a30` | TELEMETRYNVIDIA | varredura de toggles (FUN_1377e480) |
| `0x1377f793` | `0x13780a50` | TURBONUCLEOINTEL | varredura de toggles (FUN_1377e480) |
| `0x1377f82b` | `0x13780a70` | DESEMPENHONVIDIA | varredura de toggles (FUN_1377e480) |
| `0x1377f92f` | `0x13780ab4` | DESEMPENHOINTEL | varredura de toggles (FUN_1377e480) |
| `0x1377f982` | `0x13780ad4` | CONTROLPOTENCYAMD | varredura de toggles (FUN_1377e480) |
| `0x1377fa20` | `0x13780af4` | AMDRESPOSTA | varredura de toggles (FUN_1377e480) |
| `0x1377fa9e` | `0x13780b10` | FLUXODADOSAMD | varredura de toggles (FUN_1377e480) |
| `0x1377fb34` | `0x13780b2c` | Ativador_ON | varredura de toggles (FUN_1377e480) |
| `0x1377fbc7` | `0x13780b48` | Office_ON | varredura de toggles (FUN_1377e480) |
| `0x1377fc60` | `0x13780b60` | Energia_ON | varredura de toggles (FUN_1377e480) |
| `0x1377fcf2` | `0x13780b7c` | Ping_ON | varredura de toggles (FUN_1377e480) |
| `0x1377fd87` | `0x13780b94` | Power_ON | varredura de toggles (FUN_1377e480) |
| `0x1377fe1f` | `0x13780bac` | OPTurbo_ON | varredura de toggles (FUN_1377e480) |
| `0x1377fe9d` | `0x13780bc8` | Booster_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ff38` | `0x13780be4` | Tweaks_ON | varredura de toggles (FUN_1377e480) |
| `0x1377ffd0` | `0x13780bfc` | PrioridadeGames_ON | varredura de toggles (FUN_1377e480) |

## Mensagens, cards e textos de interface (192)

| Chamada | Blob | Texto em claro | Secao / uso |
|---|---|---|---|
| `0x134a92e5` | `0x134a9484` | <br>taskkill /F /IM ReetFPS.exe | - |
| `0x134a9301` | `0x134a94b4` | <br>timeout /t 1 > nul | - |
| `0x134a931d` | `0x134a94dc` | <br>set "FileName=ReetFPS.exe" | - |
| `0x134a9339` | `0x134a950c` | <br>start "" "%FileName%" | - |
| `0x134a9355` | `0x134a94b4` | <br>timeout /t 1 > nul | - |
| `0x134a9371` | `0x134a9534` | <br>del "%~f0" | - |
| `0x134a9f5e` | `0x134a9f9c` | \Keyboard Layout eetFPS | - |
| `0x134aa33d` | `0x134aa414` | { "STATUS" : "OK" } | - |
| `0x134b4350` | `0x134b43d0` | \Keyboard Layout eetFPS | - |
| `0x134b445c` | `0x134b44e0` | \Keyboard Layout eetFPS | - |
| `0x134b455d` | `0x134b45e0` | \Keyboard Layout eetFPS | - |
| `0x134b466c` | `0x134b46f0` | \Keyboard Layout eetFPS | - |
| `0x134b583e` | `0x134b58a8` | Prezado usuário, uma nova atualização do ReetFPS está disponível. Gostaria de realizar a atualização agora? | - |
| `0x1354ce9c` | `0x1354cf14` | [ TELEMETRY SYSTEM ] | servidor / licenca / login |
| `0x1354d685` | `0x1354dac8` | ERROR_CONNECT_USER->Exception has occurred! | servidor / licenca / login |
| `0x1354d6e0` | `0x1354db04` | ERROR_CONNECT_USER->Exception limit occurred! | servidor / licenca / login |
| `0x1354ef7a` | `0x1354f9a8` | Por favor, digite um nome de usuário e uma senha. | servidor / licenca / login |
| `0x1354f4d8` | `0x1354fbec` | Usuário ou senha incorretos! | servidor / licenca / login |
| `0x1354f56b` | `0x1354fc34` | A sua licença expirou. Para continuar utilizando nossos serviços, é necessário renová-la. | servidor / licenca / login |
| `0x1354f642` | `0x1354fce8` | Sua licença está vinculada a outro computador. Por segurança, não é permitido trocá-la ou compartilhá-la. Adquira uma nova licença ou solicite um reset pelo site. | servidor / licenca / login |
| `0x1354f6ca` | `0x1354fdc8` | Este dispositivo já está vinculado a outra conta. Por segurança, não é permitido usar o mesmo PC em contas diferentes. Entre em contato com o suporte. | servidor / licenca / login |
| `0x1354f78b` | `0x1354feb4` | Acesso bloqueado! | servidor / licenca / login |
| `0x135501d1` | `0x1355022c` | O Modo desenvolvedor está habilitado, você está ciente?! | servidor / licenca / login |
| `0x13551783` | `0x1355191c` | Deseja mesmo encerrar o ReetFPS? Seu jogo será finalizado. | - |
| `0x135529f6` | `0x13552a34` | \Keyboard Layout eetFPS | chave de estado (secoes 14-16) |
| `0x13553560` | `0x135538a4` | Select %s from %s WHERE index = %s | - |
| `0x13553611` | `0x135538e8` | Select %s from %s | - |
| `0x13553af9` | `0x1355424c` | SELECT Caption, DeviceID FROM Win32_DiskDrive | - |
| `0x13553bff` | `0x135542fc` | ASSOCIATORS OF {Win32_DiskDrive.DeviceID=" | - |
| `0x13553c3d` | `0x13554338` | "} WHERE AssocClass = Win32_DiskDriveToDiskPartition | - |
| `0x13553d20` | `0x135543a0` | ASSOCIATORS OF {Win32_DiskPartition.DeviceID=" | - |
| `0x13553d8a` | `0x135543e0` | "} WHERE AssocClass = Win32_LogicalDiskToPartition | - |
| `0x13553faa` | `0x13554434` | Failed to get Disk Number | - |
| `0x13554609` | `0x13554730` | Failed to get Disk serial number | - |
| `0x13596926` | `0x13596f58` | application/json; charset=utf-8 | API assinada (username/ts/nonce/sig) |
| `0x13596af5` | `0x13596ff0` | Resposta invalida do servidor. HTTP  | API assinada (username/ts/nonce/sig) |
| `0x13596bd7` | `0x13597038` | Erro ao conectar na API:  | API assinada (username/ts/nonce/sig) |
| `0x135fcd66` | `0x135fcdc0` | O plano Basic não oferece suporte para esse serviço específico. Para aproveitar esse recurso, é necessário adquirir o plano Advanced. | aviso "plano Basic" (FUN_135fcd18) |
| `0x135fce93` | `0x135fcef0` | Desculpe, mas a categoria selecionada não está disponível no modo de teste. Por favor, faça a compra para poder utilizá-la. | avisos de plano / ISOs / toggles |
| `0x135fd4ef` | `0x135fd594` | Deseja prosseguir com o ReetFPS CLEAN? O sistema irá realizar uma limpeza completa do seu sistema, removendo todos os logs encontrados, arquivos temporários, itens na lixeira, arquivos de atualização obsoletos e dados de aplicativos não utilizados. | avisos de plano / ISOs / toggles |
| `0x135fdf72` | `0x135fe8a4` | [ReetFPS] Windows 7.iso | avisos de plano / ISOs / toggles |
| `0x135fdfcd` | `0x135fe908` | WINDOWS 7 OTIMIZADO | avisos de plano / ISOs / toggles |
| `0x135fe01e` | `0x135fe92c` | [ReetFPS] Windows 8.iso | avisos de plano / ISOs / toggles |
| `0x135fe07c` | `0x135fe998` | WINDOWS 8 OTIMIZADO | avisos de plano / ISOs / toggles |
| `0x135fe0ca` | `0x135fe9bc` | [ReetFPS] Windows 10.iso | avisos de plano / ISOs / toggles |
| `0x135fe128` | `0x135fea20` | WINDOWS 10 OTIMIZADO | avisos de plano / ISOs / toggles |
| `0x135fe1d4` | `0x135feab4` | WINDOWS 10 GAMER | avisos de plano / ISOs / toggles |
| `0x135fe28c` | `0x135feb3c` | Windows 10 Turbo | avisos de plano / ISOs / toggles |
| `0x135fe2ec` | `0x135feb5c` | [ReetFPS] Windows.11-Gamer.iso | avisos de plano / ISOs / toggles |
| `0x135fe36b` | `0x135febcc` | WINDOWS 11 GAMER | avisos de plano / ISOs / toggles |
| `0x135fe3cb` | `0x135febec` | [ReetFPS] Windows.7-Gamer.iso | avisos de plano / ISOs / toggles |
| `0x135fe447` | `0x135fec58` | WINDOWS 7 GAMER | avisos de plano / ISOs / toggles |
| `0x135fe523` | `0x135fece4` | WINDOWS 11 TURBO | avisos de plano / ISOs / toggles |
| `0x135fe60b` | `0x135fed80` | Windows 11 - ReetChapelin | avisos de plano / ISOs / toggles |
| `0x1367f707` | `0x1367f748` | Verifique sua conexão com a internet, e tente novamente.. | - |
| `0x1367f83b` | `0x1367f87c` | Sem conexão com o servidor da ReetFPS. Tente novamente em alguns minutos. | - |
| `0x1367f8f7` | `0x1367f938` | Procurando atualizações.. | - |
| `0x1367f983` | `0x1367f9c4` | O ReetFPS está em manutenção. | - |
| `0x1367fa66` | `0x1367fb08` | \|, \|app\|: \|ReetFPS\| | - |
| `0x13680663` | `0x136806d8` | Loading: forms adiados criados | - |
| `0x13680a37` | `0x13680aa0` | Loading: fim, abrindo o login | - |
| `0x13680c9f` | `0x13680eb8` | Loading: abrindo o ReetFix ( | - |
| `0x13681671` | `0x136819c4` | Loading: servidor respondeu a versao | - |
| `0x1368179f` | `0x136819f8` | REETFPS OPTIMIZER INICIANDO... | - |
| `0x13681f63` | `0x13681fcc` | Loading: revelacao concluida | - |
| `0x13682087` | `0x136821ac` | Loading: exibido | - |
| `0x136821f9` | `0x136822bc` | Loading: form criado (DFM lido) | - |
| `0x1368f6e8` | `0x1368f770` | /c ipconfig /flushdns | - |
| `0x13690097` | `0x136901c8` | RESOLUÇÃO PERSONALIZADA • 640x480 | - |
| `0x1369010e` | `0x136901f8` | Resolução 640x480 aplicada com êxito! | - |
| `0x136902c4` | `0x136903f4` | RESOLUÇÃO PERSONALIZADA • 800x600 | - |
| `0x13690338` | `0x13690424` | SaSolução 800x600 aplicada com êxito! | - |
| `0x136904f3` | `0x13690628` | RESOLUÇÃO PERSONALIZADA • 1080X1080 | - |
| `0x1369056d` | `0x1369065c` | Resolução 1080x1080 aplicada com êxito! | - |
| `0x1369072f` | `0x13690860` | RESOLUÇÃO PERSONALIZADA • 1100X1080 | - |
| `0x136907a6` | `0x13690894` | Resolução 1180x1080 aplicada com êxito! | - |
| `0x13690967` | `0x13690a98` | RESOLUÇÃO PERSONALIZADA • 1440X1080 | - |
| `0x136909de` | `0x13690acc` | Resolução 1480x1080 aplicada com êxito! | - |
| `0x13690bb6` | `0x13690cd0` | Resolução 1728x1080 aplicada com êxito! | - |
| `0x13690bf0` | `0x13690d08` | RESOLUÇÃO PERSONALIZADA • 1728X1080 | - |
| `0x13693ad1` | `0x13693c34` | FPS Desbloqueado • 250 | secao 17 legendas de preset |
| `0x13693b01` | `0x13693c5c` | FPS Desbloqueado • 360 | secao 17 legendas de preset |
| `0x13693b34` | `0x13693c84` | FPS Desbloqueado • 500 | secao 17 legendas de preset |
| `0x13693b67` | `0x13693cac` | FPS Desbloqueado • 777 | secao 17 legendas de preset |
| `0x13693b94` | `0x13693cd4` | FPS Desbloqueado • 999 | secao 17 legendas de preset |
| `0x13693bbe` | `0x13693cfc` | FPS Desbloqueado • Ilimitado | secao 17 legendas de preset |
| `0x13693bee` | `0x13693d28` | FPS Desbloqueado • 500 | secao 17 legendas de preset |
| `0x136983df` | `0x13698a1c` | Sua mira personalizada foi ativada com sucesso! | secao 13 miras |
| `0x1369b220` | `0x1369b708` | Configuration file for ReetFPS | store JSON (FUN_1369b158) |
| `0x136b1f8f` | `0x136b20a8` | Essa otimização não é necessária no Windows 7! | secoes 15/16 toggles do Windows |
| `0x136b233f` | `0x136b2458` | Essa otimização não é necessária no Windows 7! | secoes 15/16 toggles do Windows |
| `0x136be1c4` | `0x136be3e4` | Falha ao baixar o arquivo. Resposta HTTP inválida. | secao 23 GPU / NVIDIA |
| `0x136be253` | `0x136be428` | Falha ao baixar o arquivo. HTTP %d %s | secao 23 GPU / NVIDIA |
| `0x136be2ce` | `0x136be45c` | Download finalizado, mas o arquivo está vazio ou não existe. | secao 23 GPU / NVIDIA |
| `0x136be349` | `0x136be4a8` | Erro ao baixar o arquivo NVIDIA:  | secao 23 GPU / NVIDIA |
| `0x136be534` | `0x136be734` | Arquivo ZIP não encontrado:  | secao 23 GPU / NVIDIA |
| `0x136be5fc` | `0x136be760` | O ZIP contém um caminho inválido:  | secao 23 GPU / NVIDIA |
| `0x136be69b` | `0x136be794` | Erro ao extrair ZIP NVIDIA:  | secao 23 GPU / NVIDIA |
| `0x136d3af4` | `0x136d3bf0` | OBS Studio.exe | downloads de terceiros |
| `0x136d3d66` | `0x136d6bdc` | Driver Booster | downloads de terceiros |
| `0x136d3f0e` | `0x136d6cf4` | Google Chrome | downloads de terceiros |
| `0x136d3f81` | `0x136d6d10` | uTorrent PRO.zip | downloads de terceiros |
| `0x136d3fdf` | `0x136d6d90` | uTorrent PRO | downloads de terceiros |
| `0x136d4245` | `0x136d6edc` | OBS Studio.exe | downloads de terceiros |
| `0x136d44c0` | `0x136d7070` | Adobe After Effects | downloads de terceiros |
| `0x136d45be` | `0x136d7118` | Microsoft Store | downloads de terceiros |
| `0x136d47cf` | `0x136d7228` | Adobe Photoshop Licenciado | downloads de terceiros |
| `0x136d4ad2` | `0x136d73fc` | CorelDRAW [LICENCIADO] | downloads de terceiros |
| `0x136d4cd1` | `0x136d7520` | VEGAS Pro Portable | downloads de terceiros |
| `0x136d4dcf` | `0x136d75ac` | Adobe Acrobat PRO | downloads de terceiros |
| `0x136d4e54` | `0x136d75cc` | DISK DEFRAG.rar | downloads de terceiros |
| `0x136d4ed0` | `0x136d75cc` | DISK DEFRAG.rar | downloads de terceiros |
| `0x136d52d7` | `0x136d77c0` | VLC media player | downloads de terceiros |
| `0x136d56de` | `0x136d798c` | Point Blank Setup | downloads de terceiros |
| `0x136d5fea` | `0x136d7d1c` | Setup Spotify | downloads de terceiros |
| `0x136db8e3` | `0x136db99c` | Configurações aplicadas com êxito! | downloads de terceiros |
| `0x136db924` | `0x136db9d0` | Configuração inválida ou inexistente! | downloads de terceiros |
| `0x136dba3a` | `0x136dba98` | O ID das suas configurações foi copiado! | downloads de terceiros |
| `0x136dbbb4` | `0x136dbc0c` | Esta opção permite a importação de uma configuração personalizada de outro usuário. Basta inserir o ID da configuração desejada e aplicar as alterações! | downloads de terceiros |
| `0x136dbcdb` | `0x136dbd34` | Copie o ID da sua configuração e forneça-a ao usuário desejado para que possam utilizar sua configuração personalizada. O usuário só precisa inserir o ID fornecido e aplicar as configurações. | downloads de terceiros |
| `0x136e664f` | `0x136e6b84` | Mozilla/5.0 (Windows NT 10.0; Win64; x64) | secao 24 carregador web |
| `0x136e66a1` | `0x136e6bbc` | InternetOpen failed:  | secao 24 carregador web |
| `0x136e673d` | `0x136e6be0` | Invalid URL format | secao 24 carregador web |
| `0x136e67d5` | `0x136e6c04` | InternetConnect failed:  | secao 24 carregador web |
| `0x136e6890` | `0x136e6c34` | HttpOpenRequest failed:  | secao 24 carregador web |
| `0x136e6922` | `0x136e6c5c` | HttpSendRequest failed:  | secao 24 carregador web |
| `0x136e69af` | `0x136e6c84` | InternetReadFile failed:  | secao 24 carregador web |
| `0x136e6a37` | `0x136e6cac` | Empty response | secao 24 carregador web |
| `0x136e6d12` | `0x136e713c` | [WEB] Starting download from:  | secao 24 carregador web |
| `0x136e6d60` | `0x136e716c` | [WEB] Downloading... | secao 24 carregador web |
| `0x136e6d9f` | `0x136e7190` | [WEB] Download failed:  | secao 24 carregador web |
| `0x136e6dfc` | `0x136e71b8` | [WEB] Empty data received | secao 24 carregador web |
| `0x136e6e3c` | `0x136e71e0` | [WEB] Downloaded:  | secao 24 carregador web |
| `0x136e6ee3` | `0x136e721c` | [WEB] Decrypted | secao 24 carregador web |
| `0x136e6f24` | `0x136e723c` | [WEB] e_magic OK (MZ) | secao 24 carregador web |
| `0x136e6faf` | `0x136e7284` | [WEB] Loading library into process... | secao 24 carregador web |
| `0x136e7006` | `0x136e72b8` | [WEB] Success! | secao 24 carregador web |
| `0x136e703c` | `0x136e72d8` | LoadFromMemory failed | secao 24 carregador web |
| `0x137118ae` | `0x137119f4` | Limpando: C:\Windows\SoftwareDistribution\Download | limpeza / login |
| `0x13711b31` | `0x13711c54` | --- Início da limpeza ( | limpeza / login |
| `0x13711cb8` | `0x13711d18` | --- Fim da limpeza --- | limpeza / login |
| `0x13715771` | `0x137157f8` | Erro ao validar login:  | limpeza / login |
| `0x13715a4e` | `0x13715c3c` | Informe o usuário e a senha. | limpeza / login |
| `0x13715acc` | `0x13715c68` | Validando login... | limpeza / login |
| `0x137162a3` | `0x137162fc` | Aguarde até o fim da atualização! | - |
| `0x1372837e` | `0x137284d4` | Seu ping agora é exibido em tempo real no jogo. | card COUNTERPING |
| `0x137283a7` | `0x13728524` | Acompanhe sua latência diretamente na interface do jogo! | card COUNTERPING |
| `0x13728e3a` | `0x13728f4c` | A otimização do Loading dos mapas foi ativada com sucesso. Aproveite o carregamento instantâneo! | secao 18 MAPLOADING |
| `0x137291b1` | `0x13729328` |  O Minimapa foi desativado com sucesso! | secao 19 MINIMAP |
| `0x137291da` | `0x13729370` | Prepare-se para um aumento significativo no desempenho do jogo. | secao 19 MINIMAP |
| `0x13729203` | `0x137293c0` | Aproveite uma experiência mais fluida e responsiva! | secao 19 MINIMAP |
| `0x137296bf` | `0x137297cc` | ReetStats Tracker: Veja sua % de KD e HS, record de kills, HS na partida e progresso diário com % de KD, HS e EXP, tudo em tempo real no PointBlank. | card REETSTATS |
| `0x13729d4c` | `0x13729e88` | ReetStats Interação! | card ENABLE_INTERACTIVE |
| `0x13729d75` | `0x13729ebc` | Quem usa o ReetFPS aparece com o nick roxo na partida. | card ENABLE_INTERACTIVE |
| `0x1372a193` | `0x1372a2f0` | Fluidez Máxima ativada! | secao 9 FLUIDEZMAX |
| `0x1372a1bf` | `0x1372a328` | Seu jogo agora está mais leve, rápido e sem travamentos. | secao 9 FLUIDEZMAX |
| `0x1372a592` | `0x1372a700` | ReetStats HUD PLAYERS ativado com sucesso! | card HUDPLAYERS |
| `0x1372a5bb` | `0x1372a74c` | Monitore o status do seu time em tempo real, incluindo HP, nick e avatar dos jogadores vivos | card HUDPLAYERS |
| `0x1372a9fb` | `0x1372ab7c` | FPS Counter ativado! | card FPSCOUNTER |
| `0x1372aa24` | `0x1372abb0` | Monitore o FPS, CPU e RAM em tempo real durante o jogo. | card FPSCOUNTER |
| `0x1372aa4d` | `0x1372abf8` | Personalize a exibição dos indicadores conforme sua necessidade. | card FPSCOUNTER |
| `0x1372b12e` | `0x1372b224` | Timer Resolution ativado com sucesso! | secao 10 Timer Resolution |
| `0x1372b157` | `0x1372b268` | Input lag reduzido e desempenho otimizado. | secao 10 Timer Resolution |
| `0x1372b3f3` | `0x1372b54c` | Agora você está no ReetStats Ranking! | card ENABLE_RANK |
| `0x1372b41c` | `0x1372b590` | Compita, melhore seu desempenho e suba na classificação. | card ENABLE_RANK |
| `0x1372b445` | `0x1372b5d8` | Fique entre os top 3 para conquistar prêmios diários! | card ENABLE_RANK |
| `0x1372d6bc` | `0x1372d87c` | A otimização da interface foi ativada com sucesso. | secao 16 INTERFACEDELAY |
| `0x1372d6e2` | `0x1372d8d0` | Agora, a navegação entre as interfaces do lobby do Point Blank está mais rápida e sem delays! | secao 16 INTERFACEDELAY |
| `0x1372dc54` | `0x1372ddcc` | Teclado de Precisão Ativado! | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372dc7a` | `0x1372de08` | Pressionar teclas opostas como "W/S" e "A/D" agora evita conflitos. | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372dca6` | `0x1372de5c` | Apenas a última tecla pressionada é reconhecida, garantindo movimentos mais fluidos e precisos para uma jogabilidade suave. | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372e118` | `0x1372e274` | Tela cheia ativada! | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e144` | `0x1372e2a8` | Use F6 para alternar entre tela cheia e janela no Point Blank. | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e533` | `0x1372e6b4` | O modo Borderless foi ativado com sucesso. | secao 22 BORDER_LESS |
| `0x1372e559` | `0x1372e700` | Agora você pode alternar rapidamente entre o jogo e outros aplicativos, | secao 22 BORDER_LESS |
| `0x1372e585` | `0x1372e758` | desfrutar de maior estabilidade e realizar multitarefas sem interrupções. | secao 22 BORDER_LESS |
| `0x1372e9b3` | `0x1372eb0c` | O jogo foi configurado com prioridade máxima! | secao 21 PRIORITYPB |
| `0x1372e9df` | `0x1372eb58` | Isso pode proporcionar um aumento no desempenho e nos FPS. | secao 21 PRIORITYPB |
| `0x13730b17` | `0x13730c14` | Jogo não encontrado. Por favor, execute o PBLauncher pelo menos uma vez com o ReetFPS aberto antes de tentar novamente. | localizar / iniciar o jogo |
| `0x13730d5b` | `0x13730e54` | Jogo não encontrado. Por favor, execute o PBLauncher pelo menos uma vez com o ReetFPS aberto antes de tentar novamente. | localizar / iniciar o jogo |
| `0x13730f9b` | `0x13731094` | Jogo não encontrado. Por favor, execute o PBLauncher pelo menos uma vez com o ReetFPS aberto antes de tentar novamente. | localizar / iniciar o jogo |
| `0x13732e9f` | `0x13732f50` | REDUZIR TIMER RESOLUTION •  | - |
| `0x137330bd` | `0x13733280` | O PointBlank está com prioridade máxima e seu sistema foi limpo para oferecer o melhor desempenho. | - |
| `0x137330ed` | `0x137332f4` | Otimizações aplicadas | - |
| `0x1373312a` | `0x13733318` | O PointBlank foi configurado com prioridade máxima para melhor desempenho. | - |
| `0x13733157` | `0x13733374` | Configuração Aplicada | - |
| `0x1373318f` | `0x13733398` | O sistema foi limpo e otimizado. Seu PC está pronto para rodar o PointBlank com mais desempenho e estabilidade. | - |
| `0x137331bc` | `0x13733418` | Limpeza concluída | - |
| `0x1377b4c9` | `0x1377b584` | Foi minimizado | - |
| `0x1377dba4` | `0x1377ddf8` | Deseja mesmo encerrar o ReetFPS? Seu jogo será finalizado. | - |
| `0x137e298a` | `0x137e2dc8` | ReetFPS \| Login | - |
| `0x137e2a3b` | `0x137e2e20` | Execute o ReetFPS como administrador! | - |

## URLs e parametros de rede (69)

| Chamada | Blob | Texto em claro | Secao / uso |
|---|---|---|---|
| `0x135486f3` | `0x13548804` | http://reetfps.com/painel/organization/avatar/avatar.php?username= | - |
| `0x1354ccce` | `0x1354ce44` | https://reetfps.com/update/server.php | servidor / licenca / login |
| `0x1354dcba` | `0x1354df04` | https://reetfps.com/update/server.php | servidor / licenca / login |
| `0x1354e4a3` | `0x1354e608` | https:// | servidor / licenca / login |
| `0x1354e4e5` | `0x1354e620` | http:// | servidor / licenca / login |
| `0x1354e690` | `0x1354e7f0` | https:// | servidor / licenca / login |
| `0x1354e6d2` | `0x1354e808` | http:// | servidor / licenca / login |
| `0x1354e8d3` | `0x1354e9d4` | https://reetfps.com/acess/user.php? | servidor / licenca / login |
| `0x1354eacf` | `0x1354ec74` | https://reetfps.com/acess/user.php? | servidor / licenca / login |
| `0x1354f410` | `0x1354fb88` | https://reetfps.com/acess/auth.php? | servidor / licenca / login |
| `0x1354f5ae` | `0x1354fc9c` | https://reetfps.com/painel/planos | servidor / licenca / login |
| `0x1354f70a` | `0x1354fe70` | https://reetchapelin.com/ | servidor / licenca / login |
| `0x1354f7ca` | `0x1354fe70` | https://reetchapelin.com/ | servidor / licenca / login |
| `0x1354ff47` | `0x1354ffd4` | https://reetfps.com/acess/user.php? | servidor / licenca / login |
| `0x1355009c` | `0x13550164` | https://reetfps.com/update/server.php? | servidor / licenca / login |
| `0x135955e6` | `0x13595624` | https://reetfps.com/API/community/fluidez_config.php | API assinada (username/ts/nonce/sig) |
| `0x135fe04f` | `0x135fe954` | http://reetfpsdownload.com/OS/Windows8_Otimizado.iso | avisos de plano / ISOs / toggles |
| `0x135fe0fb` | `0x135fe9e4` | http://reetfpsdownload.com/OS/Windows10.iso | avisos de plano / ISOs / toggles |
| `0x135fe1a7` | `0x135fea74` | http://reetfpsdownload.com/OS/Windows10_Gamer.iso | avisos de plano / ISOs / toggles |
| `0x135fe259` | `0x135feb00` | http://reetfpsdownload.com/OS/W10_TURBO.iso | avisos de plano / ISOs / toggles |
| `0x135fe32f` | `0x135feb8c` | http://reetfpsdownload.com/OS/Windows11_Gamer.iso | avisos de plano / ISOs / toggles |
| `0x135fe40e` | `0x135fec18` | http://reetfpsdownload.com/OS/WINDOWS7-GAMER.iso | avisos de plano / ISOs / toggles |
| `0x135fe4ea` | `0x135feca4` | http://reetfpsdownload.com/OS/Windows11_TURBO.iso | avisos de plano / ISOs / toggles |
| `0x135fe5cc` | `0x135fed38` | http://reetfpsdownload.com/OS/Windows11_ReetChapelin.iso | avisos de plano / ISOs / toggles |
| `0x1367f686` | `0x1367f6c4` | http://google.com | - |
| `0x1367f7b3` | `0x1367f7f4` | http://reetfps.com | - |
| `0x136bdd1e` | `0x136bdd5c` | https://reetoptimizer.com/privatedownloads/nvidia/nvidiaProfileInspector.zip | - |
| `0x136d3b2d` | `0x136d3c10` | http://reetoptimizer.com/privatedownloads/programs/OBS/OBS-Studio-27.2.3-Full-Installer-x64.exe | downloads de terceiros |
| `0x136d3d39` | `0x136d6b78` | http://reetoptimizer.com/privatedownloads/programs/DriverBooster/DriverBooster.rar | downloads de terceiros |
| `0x136d3e0a` | `0x136d6c14` | http://reetoptimizer.com/privatedownloads/programs/Rufus/rufus-3.17.exe | downloads de terceiros |
| `0x136d3ee1` | `0x136d6c9c` | http://reetoptimizer.com/privatedownloads/programs/Chrome/ChromeSetup.exe | downloads de terceiros |
| `0x136d3fb2` | `0x136d6d30` | http://reetoptimizer.com/privatedownloads/programs/uTorrentPro/uTorrentPro.zip | downloads de terceiros |
| `0x136d4089` | `0x136d6dcc` | http://reetoptimizer.com/privatedownloads/programs/TeamViewer/TeamViewer_Setup_x64.exe | downloads de terceiros |
| `0x136d4184` | `0x136d6e74` | http://reetfpsdownload.com/privatedownloads/Visual-C.zip | downloads de terceiros |
| `0x136d4288` | `0x136d6efc` | http://reetoptimizer.com/privatedownloads/programs/OBS/OBS-Studio-27.2.3-Full-Installer-x64.exe | downloads de terceiros |
| `0x136d4386` | `0x136d6fa4` | http://reetoptimizer.com/privatedownloads/programs/Office/Office.rar | downloads de terceiros |
| `0x136d4487` | `0x136d7030` | http://reetfpsdownload.com/adobe/AfterEffects.exe | downloads de terceiros |
| `0x136d4582` | `0x136d70b8` | http://reetoptimizer.com/privatedownloads/programs/MS/MicrosoftStore-Install.exe | downloads de terceiros |
| `0x136d4695` | `0x136d7150` | http://reetoptimizer.com/privatedownloads/programs/Steam/SteamSetup.exe | downloads de terceiros |
| `0x136d4793` | `0x136d71e0` | http://reetfpsdownload.com/privatedownloads/Photoshop.rar | downloads de terceiros |
| `0x136d4897` | `0x136d7270` | http://reetoptimizer.com/privatedownloads/programs/AnyDesk/AnyDesk.exe | downloads de terceiros |
| `0x136d4995` | `0x136d72fc` | http://reetoptimizer.com/privatedownloads/programs/Winrar/winrar-x64-610br.exe | downloads de terceiros |
| `0x136d4a99` | `0x136d73a0` | http://reetoptimizer.com/privatedownloads/programs/CorelDRAW/CorelDRAW.zip | downloads de terceiros |
| `0x136d4b9a` | `0x136d7444` | http://reetoptimizer.com/privatedownloads/programs/StreamLabs/Streamlabs.exe | downloads de terceiros |
| `0x136d4c98` | `0x136d74dc` | http://reetfpsdownload.com/privatedownloads/VEGAS.rar | downloads de terceiros |
| `0x136d4d96` | `0x136d7568` | http://reetfpsdownload.com/adobe/AdobeAcrobatPRO.exe | downloads de terceiros |
| `0x136d4e97` | `0x136d75ec` | http://reetoptimizer.com/page2/DISK DEFRAG.rar | downloads de terceiros |
| `0x136d4f95` | `0x136d7650` | http://reetoptimizer.com/page2/setup-lightshot.exe | downloads de terceiros |
| `0x136d5093` | `0x136d76b0` | http://reetoptimizer.com/page2/DiscordSetup.exe | downloads de terceiros |
| `0x136d5197` | `0x136d770c` | http://reetoptimizer.com/page2/Epic Games-x86.msi | downloads de terceiros |
| `0x136d529b` | `0x136d777c` | http://reetfpsdownload.com/privatedownloads/vlc.exe | downloads de terceiros |
| `0x136d539f` | `0x136d7804` | http://reetoptimizer.com/page2/tsetup-x64.4.2.4.exe | downloads de terceiros |
| `0x136d54a0` | `0x136d7868` | http://reetoptimizer.com/page2/UbisoftConnectInstaller.exe | downloads de terceiros |
| `0x136d55a1` | `0x136d78d0` | http://reetoptimizer.com/page2/npp.8.4.6.Installer.x64.exe | downloads de terceiros |
| `0x136d56a5` | `0x136d793c` | http://reetoptimizer.com/page2/PointBlank_SetupFull_20220722.zip | downloads de terceiros |
| `0x136d57a3` | `0x136d79d0` | http://reetoptimizer.com/page2/Battle.net-Setup.exe | downloads de terceiros |
| `0x136d58a1` | `0x136d7a34` | http://reetoptimizer.com/page2/Apple iTunes-x64.exe | downloads de terceiros |
| `0x136d59a2` | `0x136d7a94` | http://reetoptimizer.com/page2/EAappInstaller.exe | downloads de terceiros |
| `0x136d5aa6` | `0x136d7aec` | http://reetoptimizer.com/page2/Adobe Reader-x64.exe | downloads de terceiros |
| `0x136d5ba7` | `0x136d7b50` | http://reetdetect.com/painel/ReetDetect.exe | downloads de terceiros |
| `0x136d5cae` | `0x136d7bb0` | http://reetoptimizer.com/page2/TeamSpeak3-Client-win64-3.5.6.exe | downloads de terceiros |
| `0x136d5dac` | `0x136d7c24` | http://reetoptimizer.com/page2/GHUB_W7.exe | downloads de terceiros |
| `0x136d5ea7` | `0x136d7c7c` | http://reetoptimizer.com/page2/OperaGXSetup.exe | downloads de terceiros |
| `0x136d5fae` | `0x136d7cdc` |  http://reetoptimizer.com/page2/SpotifySetup.exe | downloads de terceiros |
| `0x136d6107` | `0x136d7da8` | http://reetoptimizer.com/page2/DirectX9.rar | downloads de terceiros |
| `0x136e7625` | `0x136e76e8` | https://reetfps.com/update/lib_update.php?index=2 | secao 24 carregador web |
| `0x1377d71d` | `0x1377d76c` | https://reetfps.com/discord | - |
| `0x1378155c` | `0x137815ac` | https://reetfps.com/sociais?grupo | - |
| `0x137815fe` | `0x13781650` | https://whatsapp.reetchapelin.com | - |

## Outras (250)

| Chamada | Blob | Texto em claro | Secao / uso |
|---|---|---|---|
| `0x134a9129` | `0x134a919c` | <br> | - |
| `0x134a92c3` | `0x134a9468` | '@echo off | - |
| `0x134a93a5` | `0x134a9554` | Restart.bat | - |
| `0x134a93e8` | `0x134a9554` | Restart.bat | - |
| `0x134aa07b` | `0x134aa128` | Telemetry | - |
| `0x134aa1b9` | `0x134aa234` | Telemetry | - |
| `0x134aa2e6` | `0x134aa400` | OK | - |
| `0x134aa4bc` | `0x134aa530` | Telemetry | - |
| `0x134aa584` | `0x134aa62c` | Telemetry | - |
| `0x134b4084` | `0x134b40f4` | Point Blank | - |
| `0x134b41b2` | `0x134b42b4` | [-] | - |
| `0x1354c331` | `0x1354c3a4` | 14.0 | servidor / licenca / login |
| `0x1354c35e` | `0x1354c3b8` | dev | servidor / licenca / login |
| `0x1354c8cd` | `0x1354cafc` | OK | servidor / licenca / login |
| `0x1354c90c` | `0x1354cafc` | OK | servidor / licenca / login |
| `0x1354cbf1` | `0x1354cde0` | connect= | servidor / licenca / login |
| `0x1354cc54` | `0x1354ce10` | &content= | servidor / licenca / login |
| `0x1354cc99` | `0x1354ce28` | &rank=true | servidor / licenca / login |
| `0x1354d1b7` | `0x1354d904` | OK | servidor / licenca / login |
| `0x1354dc01` | `0x1354deb8` | setConfig= | servidor / licenca / login |
| `0x1354dc64` | `0x1354deec` | &content= | servidor / licenca / login |
| `0x1354dd29` | `0x1354df50` | sucess | servidor / licenca / login |
| `0x1354dd5c` | `0x1354df68` | config | servidor / licenca / login |
| `0x1354e1d4` | `0x1354e248` | param3 | servidor / licenca / login |
| `0x1354e285` | `0x1354e300` | param3 | servidor / licenca / login |
| `0x1354e84e` | `0x1354e9a4` | paramA= | servidor / licenca / login |
| `0x1354e880` | `0x1354e9bc` | &paramB= | servidor / licenca / login |
| `0x1354ea51` | `0x1354ec44` | paramR= | servidor / licenca / login |
| `0x1354ea81` | `0x1354ec5c` | &paramX= | servidor / licenca / login |
| `0x1354ff0c` | `0x1354ffbc` | &param1= | servidor / licenca / login |
| `0x1355003f` | `0x13550138` | ver | servidor / licenca / login |
| `0x1355161b` | `0x135516d4` | PointBlank.exe | - |
| `0x13551654` | `0x135516f4` | ReetPING.exe | - |
| `0x13551740` | `0x135518fc` | PointBlank.exe | - |
| `0x135517c8` | `0x135518fc` | PointBlank.exe | - |
| `0x13551801` | `0x13551968` | ReetPING.exe | - |
| `0x13551850` | `0x13551968` | ReetPING.exe | - |
| `0x13551e50` | `0x13552044` | Caption | - |
| `0x13551eb4` | `0x13552080` | 7 | - |
| `0x13551f00` | `0x13552090` | 8 | - |
| `0x13551f46` | `0x135520a0` | 10 | - |
| `0x13551f89` | `0x135520b4` | 11 | - |
| `0x135520fa` | `0x1355216c` | open | - |
| `0x135521b6` | `0x13552228` | open | - |
| `0x13552404` | `0x135524b4` | nullptr | - |
| `0x1355281a` | `0x135528f8` | USER32.DLL | - |
| `0x13552843` | `0x13552914` | SetLayeredWindowAttributes | - |
| `0x13553a04` | `0x135541c0` | WbemScripting.SWbemLocator | - |
| `0x13553a59` | `0x135541ec` | root\CIMV2 | - |
| `0x13553a81` | `0x13554208` | localhost | - |
| `0x1359568b` | `0x135956cc` | 40F3ECMB3R2ZB4DAB1107DG15AIZZW03 | API assinada (username/ts/nonce/sig) |
| `0x13595731` | `0x135957cc` | fluidez | API assinada (username/ts/nonce/sig) |
| `0x1359575b` | `0x135957e4` | competitiva | API assinada (username/ts/nonce/sig) |
| `0x13595785` | `0x13595800` | competitiva | API assinada (username/ts/nonce/sig) |
| `0x135964b8` | `0x13596ddc` | 0x899991 | API assinada (username/ts/nonce/sig) |
| `0x135964fb` | `0x13596df4` | 0x899992 | API assinada (username/ts/nonce/sig) |
| `0x1359656b` | `0x13596e1c` | 0x899993 | API assinada (username/ts/nonce/sig) |
| `0x13596611` | `0x13596e84` | 0x899994 | API assinada (username/ts/nonce/sig) |
| `0x135966c7` | `0x13596eac` | username | API assinada (username/ts/nonce/sig) |
| `0x13596704` | `0x13596ec4` | mode | API assinada (username/ts/nonce/sig) |
| `0x1359674a` | `0x13596ed8` | ts | API assinada (username/ts/nonce/sig) |
| `0x13596790` | `0x13596eec` | nonce | API assinada (username/ts/nonce/sig) |
| `0x135967d3` | `0x13596f00` | sig | API assinada (username/ts/nonce/sig) |
| `0x1359689c` | `0x13596f18` | ReetFPS-Client/1.0 | API assinada (username/ts/nonce/sig) |
| `0x135968f7` | `0x13596f3c` | Content-Type | API assinada (username/ts/nonce/sig) |
| `0x1359695b` | `0x13596f88` | Accept | API assinada (username/ts/nonce/sig) |
| `0x1359698d` | `0x13596fa0` | application/json | API assinada (username/ts/nonce/sig) |
| `0x13596a3a` | `0x13596fc0` | message | API assinada (username/ts/nonce/sig) |
| `0x13596a9c` | `0x13596fd8` | success | API assinada (username/ts/nonce/sig) |
| `0x13596b4c` | `0x13597024` | :  | API assinada (username/ts/nonce/sig) |
| `0x1359761c` | `0x135976fc` | icon.png | API assinada (username/ts/nonce/sig) |
| `0x13597800` | `0x135978e0` | icon.png | API assinada (username/ts/nonce/sig) |
| `0x135fe225` | `0x135fead4` | [ReetFPS]-Windows10_TURBO.iso | avisos de plano / ISOs / toggles |
| `0x135fe4aa` | `0x135fec78` | [ReetFPS]-Windows11_TURBO.iso | avisos de plano / ISOs / toggles |
| `0x135fe589` | `0x135fed04` | [ReetFPS]-Windows11_ReetChapelin.iso | avisos de plano / ISOs / toggles |
| `0x1367f22c` | `0x1367f364` | Installed | - |
| `0x1367f25c` | `0x1367f37c` | SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0\VC untimes\X86 | - |
| `0x1367f29d` | `0x1367f3cc` | Installed | - |
| `0x1367f2ca` | `0x1367f3e4` | SOFTWARE\Microsoft\VisualStudio\14.0\VC untimes\X86 | - |
| `0x1367fa27` | `0x1367fadc` | \|action\|: \| | - |
| `0x1367fe4d` | `0x1367feb8` | updating | - |
| `0x1367ff3d` | `0x13680268` | < | - |
| `0x1367ff81` | `0x13680278` | html | - |
| `0x1367ffc7` | `0x1368028c` | warning | - |
| `0x13680013` | `0x136802a4` | fatal error | - |
| `0x1368005c` | `0x136802c0` | mysqli | - |
| `0x136800a5` | `0x136802d8` | mysql | - |
| `0x136800ee` | `0x136802ec` | database | - |
| `0x1368013a` | `0x13680304` | error | - |
| `0x13680cce` | `0x13680ee4` | ) | - |
| `0x13680d50` | `0x13680ef4` | ReetFix.exe | - |
| `0x136814e9` | `0x13681538` | InstallFix | - |
| `0x13681579` | `0x136815c8` | Update | - |
| `0x13681a83` | `0x13681be0` | ReetFix.exe | - |
| `0x13681ac2` | `0x13681bfc` | ReetFix.exe | - |
| `0x1368f70a` | `0x1368f794` | cmd.exe | - |
| `0x1368f72c` | `0x1368f7ac` | runas | - |
| `0x1369387a` | `0x136939e4` | 250 | secao 17 rotulos de preset |
| `0x136938ad` | `0x136939f8` | 360 | secao 17 rotulos de preset |
| `0x136938e0` | `0x13693a0c` | 500 | secao 17 rotulos de preset |
| `0x13693913` | `0x13693a20` | 777 | secao 17 rotulos de preset |
| `0x13693943` | `0x13693a34` | 999 | secao 17 rotulos de preset |
| `0x136939a0` | `0x13693a5c` | 500 | secao 17 rotulos de preset |
| `0x13697b64` | `0x1369874c` | 1 | secao 13 miras |
| `0x13697d61` | `0x13698800` | 2 | secao 13 miras |
| `0x13697f67` | `0x136988b4` | 3 | secao 13 miras |
| `0x136981c4` | `0x13698968` | 4 | secao 13 miras |
| `0x1369bbec` | `0x1369bde4` | 0 | store: valores booleanos |
| `0x1369bc2c` | `0x1369bdf4` | false | store: valores booleanos |
| `0x1369bc6c` | `0x1369be08` | off | store: valores booleanos |
| `0x1369bca9` | `0x1369be1c` | no | store: valores booleanos |
| `0x1369bcdf` | `0x1369be30` | nao | store: valores booleanos |
| `0x1369bd18` | `0x1369be44` | disabled | store: valores booleanos |
| `0x1369c925` | `0x1369e524` | ClearTempFiles | restauracao do painel (FUN_1369beb0) |
| `0x136b0360` | `0x136b0460` | cmd.exe | secoes 15/16 toggles do Windows |
| `0x136b03ad` | `0x136b0478` | cmd.exe | secoes 15/16 toggles do Windows |
| `0x136d3b5f` | `0x136d3c80` | OBS Studio | downloads de terceiros |
| `0x136d3d08` | `0x136d6b58` | DriverBooster.rar | downloads de terceiros |
| `0x136d3dd9` | `0x136d6bfc` | Rufus.exe | downloads de terceiros |
| `0x136d3e37` | `0x136d6c6c` | Rufus | downloads de terceiros |
| `0x136d3eb0` | `0x136d6c80` | Chrome.exe | downloads de terceiros |
| `0x136d4055` | `0x136d6dac` | TeamViewer.exe | downloads de terceiros |
| `0x136d40bf` | `0x136d6e34` | TeamViewer | downloads de terceiros |
| `0x136d4144` | `0x136d6e50` | Visual-C-Runtimes.zip | downloads de terceiros |
| `0x136d41bd` | `0x136d6ebc` | Visual-C-Runtimes | downloads de terceiros |
| `0x136d42c1` | `0x136d6f6c` | OBS Studio | downloads de terceiros |
| `0x136d4346` | `0x136d6f88` | Office.rar | downloads de terceiros |
| `0x136d43bf` | `0x136d6ff8` | Office | downloads de terceiros |
| `0x136d4447` | `0x136d7010` | AfterEffects.exe | downloads de terceiros |
| `0x136d4652` | `0x136d7138` | Steam.exe | downloads de terceiros |
| `0x136d46ce` | `0x136d71a8` | Steam | downloads de terceiros |
| `0x136d4753` | `0x136d71bc` | AdobePhotoshop.rar | downloads de terceiros |
| `0x136d4854` | `0x136d7254` | AnyDesk.exe | downloads de terceiros |
| `0x136d48d0` | `0x136d72c8` | AnyDesk | downloads de terceiros |
| `0x136d4955` | `0x136d72e0` | Winrar.exe | downloads de terceiros |
| `0x136d49d1` | `0x136d735c` | Winrar | downloads de terceiros |
| `0x136d4a59` | `0x136d7374` | CorelDRAW-[LICENCIADO].zip | downloads de terceiros |
| `0x136d4b5a` | `0x136d7424` | Streamlabs.exe | downloads de terceiros |
| `0x136d4bd3` | `0x136d74a0` | StreamLabs | downloads de terceiros |
| `0x136d4c58` | `0x136d74bc` | SonyVegasPro.rar | downloads de terceiros |
| `0x136d4d56` | `0x136d7544` | AdobeAcrobatPRO.exe | downloads de terceiros |
| `0x136d4f55` | `0x136d762c` | Setup-Lightshot.exe | downloads de terceiros |
| `0x136d4fce` | `0x136d762c` | Setup-Lightshot.exe | downloads de terceiros |
| `0x136d5053` | `0x136d7694` | Discord.exe | downloads de terceiros |
| `0x136d50cc` | `0x136d7694` | Discord.exe | downloads de terceiros |
| `0x136d5154` | `0x136d76f0` | EpicGames.msi | downloads de terceiros |
| `0x136d51d3` | `0x136d774c` | EpicGames | downloads de terceiros |
| `0x136d525b` | `0x136d7764` | vlc.exe | downloads de terceiros |
| `0x136d535c` | `0x136d77e0` | Setup_Telegram.exe | downloads de terceiros |
| `0x136d53d8` | `0x136d77e0` | Setup_Telegram.exe | downloads de terceiros |
| `0x136d545d` | `0x136d7848` | Setup_uPlay.exe | downloads de terceiros |
| `0x136d54d9` | `0x136d7848` | Setup_uPlay.exe | downloads de terceiros |
| `0x136d5561` | `0x136d78b4` | Notepad++.exe | downloads de terceiros |
| `0x136d55dd` | `0x136d78b4` | Notepad++.exe | downloads de terceiros |
| `0x136d5665` | `0x136d791c` | PointBlank.zip | downloads de terceiros |
| `0x136d5763` | `0x136d79ac` | Setup_BattleNet.exe | downloads de terceiros |
| `0x136d57dc` | `0x136d79ac` | Setup_BattleNet.exe | downloads de terceiros |
| `0x136d5861` | `0x136d7a14` | Setup_iTunes.exe | downloads de terceiros |
| `0x136d58da` | `0x136d7a14` | Setup_iTunes.exe | downloads de terceiros |
| `0x136d5962` | `0x136d7a78` | Origin.exe | downloads de terceiros |
| `0x136d59de` | `0x136d7a78` | Origin.exe | downloads de terceiros |
| `0x136d5a63` | `0x136d7ad4` | Adobe.exe | downloads de terceiros |
| `0x136d5adf` | `0x136d7ad4` | Adobe.exe | downloads de terceiros |
| `0x136d5b67` | `0x136d7b30` | ReetDetect.exe | downloads de terceiros |
| `0x136d5be3` | `0x136d7b30` | ReetDetect.exe | downloads de terceiros |
| `0x136d5c6b` | `0x136d7b8c` | Setup_TeamSpeak3.exe | downloads de terceiros |
| `0x136d5cea` | `0x136d7b8c` | Setup_TeamSpeak3.exe | downloads de terceiros |
| `0x136d5e67` | `0x136d7c60` | OperaGX.exe | downloads de terceiros |
| `0x136d5ee0` | `0x136d7c60` | OperaGX.exe | downloads de terceiros |
| `0x136d5f68` | `0x136d7cbc` | SpotifySetup.exe | downloads de terceiros |
| `0x136d6040` | `0x136d7d38` | DirectX 9 | downloads de terceiros |
| `0x136d60c4` | `0x136d7d8c` | DirectX9.rar | downloads de terceiros |
| `0x136e50e2` | `0x136e5388` | old_version.exe | secao 24 carregador web |
| `0x136e511f` | `0x136e53a8` | old_version.exe | secao 24 carregador web |
| `0x136e5150` | `0x136e53c8` | ReetFix.exe | secao 24 carregador web |
| `0x136e518a` | `0x136e53e4` | ReetFix | secao 24 carregador web |
| `0x136e51ba` | `0x136e53fc` | ReetFix.exe | secao 24 carregador web |
| `0x136e51fb` | `0x136e5418` | ReetFix.exe | secao 24 carregador web |
| `0x136e5240` | `0x136e5434` | 7EE0236DE0E17D935FB65BCAAD3D3514 | secao 24 carregador web |
| `0x136e5277` | `0x136e53e4` | ReetFix | secao 24 carregador web |
| `0x136e52a7` | `0x136e53fc` | ReetFix.exe | secao 24 carregador web |
| `0x136e6e7b` | `0x136e7204` |  bytes | secao 24 carregador web |
| `0x136e78ef` | `0x136e7944` | PointBlank.exe | secao 24 carregador web |
| `0x136f7000` | `0x136f71f8` | 0 | despachante |
| `0x136f7040` | `0x136f7208` | false | despachante |
| `0x136f7080` | `0x136f721c` | off | despachante |
| `0x136f70bd` | `0x136f7230` | no | despachante |
| `0x136f70f3` | `0x136f7244` | nao | despachante |
| `0x136f712c` | `0x136f7258` | disabled | despachante |
| `0x137117b9` | `0x1371198c` | Limpando:  | limpeza / login |
| `0x137117f8` | `0x137119a8` | Temp | limpeza / login |
| `0x13711823` | `0x137119bc` | SystemRoot | limpeza / login |
| `0x1371186c` | `0x137119d8` | Limpando:  | limpeza / login |
| `0x137118e0` | `0x13711a38` | C:\Windows\SoftwareDistribution\Download | limpeza / login |
| `0x13711a9d` | `0x13711c34` | limpeza_log.txt | limpeza / login |
| `0x13711b82` | `0x13711c7c` | ) --- | limpeza / login |
| `0x13714851` | `0x137148c4` | 45787854457845 | limpeza / login |
| `0x1371492c` | `0x13714a88` | 45787854457845 | limpeza / login |
| `0x137273e0` | `0x13727454` | GamePath | - |
| `0x137274b2` | `0x13727534` | Enabled | - |
| `0x13728339` | `0x137284bc` | icon.png | card COUNTERPING |
| `0x13729aa3` | `0x13729b40` | icon.png | card ENABLE_INTERACTIVE |
| `0x13729d07` | `0x13729e70` | icon.png | card ENABLE_INTERACTIVE |
| `0x1372a14b` | `0x1372a2d8` | icon.png | secao 9 FLUIDEZMAX |
| `0x1372a54d` | `0x1372a6e8` | icon.png | card HUDPLAYERS |
| `0x1372a9b6` | `0x1372ab64` | icon.png | card FPSCOUNTER |
| `0x1372b3ae` | `0x1372b534` | icon.png | card ENABLE_RANK |
| `0x1372d674` | `0x1372d864` | icon.png | secao 16 INTERFACEDELAY |
| `0x1372dc0f` | `0x1372ddb4` | icon.png | secao 14 KEYBOARD (Teclado de Precisao) |
| `0x1372e0d0` | `0x1372e25c` | icon.png | secao 22 TELACHEIA (FULLSCREEN) |
| `0x1372e4ee` | `0x1372e69c` | icon.png | secao 22 BORDER_LESS |
| `0x1372e96b` | `0x1372eaf4` | icon.png | secao 21 PRIORITYPB |
| `0x1372ed11` | `0x1372edd8` | ClearTempFiles | ClearTempFiles |
| `0x1372ef61` | `0x1372efb8` | ClearTempFiles | ClearTempFiles |
| `0x13730ac6` | `0x13730bf4` | \PBLauncher.exe | localizar / iniciar o jogo |
| `0x13730d0a` | `0x13730e38` | \PBConfig.exe | localizar / iniciar o jogo |
| `0x13730f4a` | `0x13731078` | \PBConfig.exe | localizar / iniciar o jogo |
| `0x13731378` | `0x137313ec` | GamePath | localizar / iniciar o jogo |
| `0x13731431` | `0x13731510` | PointBlank.exe | localizar / iniciar o jogo |
| `0x13731464` | `0x13731530` | CB.exe | localizar / iniciar o jogo |
| `0x1373149b` | `0x13731548` | PBLauncher.exe | localizar / iniciar o jogo |
| `0x13731592` | `0x1373168c` | PointBlank.exe | localizar / iniciar o jogo |
| `0x137315ce` | `0x137316ac` | PBLauncher.exe | localizar / iniciar o jogo |
| `0x1373160d` | `0x137316cc` | CB.exe | localizar / iniciar o jogo |
| `0x13731b95` | `0x13731f08` | PBLauncher.exe | - |
| `0x13731bd0` | `0x13731f28` | CB.exe | - |
| `0x13732a6f` | `0x13732c68` | PBLauncher.exe | - |
| `0x13732aab` | `0x13732c88` | PointBlank.exe | - |
| `0x13732aee` | `0x13732ca8` | PBLauncher.exe | - |
| `0x13732b31` | `0x13732cc8` | PointBlank.exe | - |
| `0x13732b8a` | `0x13732ce8` | GamePath | - |
| `0x13733b61` | `0x13733c80` | NewAnnounce | - |
| `0x13733bf9` | `0x13733c80` | NewAnnounce | - |
| `0x1377b627` | `0x1377b694` | open | - |
| `0x1377b6dd` | `0x1377b754` | runas | - |
| `0x1377b857` | `0x1377b8fc` | %APPDATA% | - |
| `0x1377d96d` | `0x1377da5c` | PointBlank.exe | - |
| `0x1377d9b9` | `0x1377daa0` | ReetPING.exe | - |
| `0x1377db63` | `0x1377ddd8` | PointBlank.exe | - |
| `0x137e2775` | `0x137e27f4` | Update.exe | - |
| `0x137e29b1` | `0x137e2de8` | ReetFPS.exe | - |
| `0x137e29e6` | `0x137e2e04` | Update.exe | - |
| `0x137e2a87` | `0x137e2e54` | ReetFPS.exe | - |
| `0x137e2abe` | `0x137e2e54` | ReetFPS.exe | - |
| `0x137e2afb` | `0x137e2e70` | ReetFPS.exe | - |
| `0x137e2b2d` | `0x137e2e8c` | ReetFPS.exe | - |
| `0x137e2b6a` | `0x137e2ea8` | Update.exe | - |
| `0x137e2b94` | `0x137e2ec4` | ReetFPS.exe | - |
| `0x137e2c0f` | `0x137e2ee0` | ReetFPS.exe | - |
| `0x137e2c94` | `0x137e2ee0` | ReetFPS.exe | - |

## Strings da ReetFPS.dll

A DLL usa outro esquema: cada string e montada na pilha byte a byte e decifrada por XOR de um unico byte, sendo o **primeiro byte do buffer a chave**. Os valores abaixo foram decifrados a partir das constantes do decompilado de `FUN_100041d0` (thread principal) e `FUN_10003c00` (autenticacao).

| Onde (ReetFPS.dll) | Chave | Texto em claro |
|---|---|---|
| `FUN_100041d0 local_144` | 0x06 | PointBlank.exe |
| `FUN_100041d0 local_120` | 0x60 | Waiting process |
| `FUN_100041d0 local_f0` | 0x02 | RESTART PROCESS AGAIN! |
| `FUN_100041d0 local_1ac` | 0x1e | GetProcessInformation->Failed to get process informations |
| `FUN_100041d0 local_16c` | 0x38 | [ INIT ] GetProcessInformation->OK! |
| `FUN_100041d0 local_1e8` | 0x42 | [ INIT ] GetProcessInformation->ProcessInfo.hWND->0x%X |
| `FUN_100041d0 local_220` | 0x5b | Failed to load library, ID: 0x%d / Message: %s |
| `FUN_100041d0 local_d4` | 0x67 | [ INIT ] Init->Sucess! |
| `FUN_100041d0 local_b8` | 0x70 | [ INIT ] GetFile->FAIL! |
| `FUN_100041d0 local_134` | 0x36 | stub_path->%s |
| `FUN_100041d0 local_108` | 0x6d | File not found->%s |
| `FUN_100041d0 local_9c` | 0x77 | [ INIT ] AUTH_CHECK->OK! |
| `FUN_100041d0 local_80` | 0x48 | [ INIT ] AUTH_CHECK->FAIL! |
| `FUN_100041d0 local_5b..local_52` | (sem cifra) | window.ime |
| `FUN_10003c00 local_a0` | 0x43 | Control Panel\Desktop\Colors\ |
| `FUN_10003c00 local_7f` | (sem cifra) | WindowMsg |
| `FUN_10003c00 local_6f` | (sem cifra) | 1482301 (multiplicador do PID) |
| `FUN_10001be0` | (sem cifra) | "window" + ".ime" (apos o diretorio System) |
| `FUN_10002100` | (literal) | Keyboard Layout\Preload |

Todas as 13 strings cifradas da thread foram decifradas com sucesso; o contexto completo esta na secao 24 de `ponto_blank.c`.
