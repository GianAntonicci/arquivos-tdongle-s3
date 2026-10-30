# AGENTS.md — T-Dongle Bridge

Guia para agentes (e pessoas) que forem mexer neste repositório: o que o projeto é,
como compilar, as regras que não podem ser quebradas, as decisões já tomadas e o que
ainda falta fazer.

## Registro obrigatório das atividades dos agentes

**Sempre que um agente realizar algum trabalho neste repositório, deve registrar
a atividade neste `AGENTS.md` antes de encerrar a tarefa.** Isso inclui alterações
de código, documentação e configuração, investigações, diagnósticos, testes,
gravações no hardware e operações Git autorizadas, mesmo quando não houver
mudança no código.

- Atualizar as seções afetadas para que descrevam o estado real do projeto.
- Acrescentar ao **Histórico de atividades dos agentes** um registro com data,
  resumo do trabalho, arquivos ou componentes envolvidos e resultado obtido.
- Informar as validações executadas e seus resultados. Quando não forem
  executadas ou não se aplicarem, indicar isso e explicar o motivo.
- Registrar decisões, limitações, problemas encontrados e pendências relevantes
  para que o próximo agente consiga continuar o trabalho.
- Consolidar o registro por tarefa ou bloco de trabalho; não é necessário listar
  cada chamada de ferramenta. A atualização do próprio registro entra no mesmo
  bloco, sem gerar registros recursivos.

## O projeto

O LilyGO T-Dongle S3 (ESP32-S3) vira uma **ponte de arquivos**:

```
Celular --(Wi-Fi AP + HTTP)--> T-Dongle --(USB CDC, frames binários)--> Página Web Serial (Chrome) --> pasta no PC
```

**Regra central: nenhum arquivo é armazenado no dongle.** Nada vai para SD nem para a
flash, só blocos em trânsito na RAM. O slot microSD não é usado. Na NVS ficam apenas
configurações (SO alvo, layout, URL, comando do Linux, brilho do LED).

Estado de referência: firmware **0.2.2**, protocolo USB **v1**. O envio de um
JPEG de 4.336.521 bytes foi confirmado no hardware com tamanho/CRC válidos;
isso não substitui os testes físicos de arquivos grandes, lotes e falhas.

## Estrutura

```
firmware/            projeto PlatformIO (rodar `pio` sempre de dentro daqui)
  platformio.ini
  include/config.h   SSID, senha, URL, pinos, delays do HID, flags
  include/*.h        headers dos módulos
  src/main.cpp       máquina de estados do app (telas, LED, menu, eventos app::)
  src/display_ui.cpp ST7735 via LovyanGFX + task de UI (display, LED, botão) no core 0
  src/led.cpp        APA102 por bit-bang
  src/button.cpp     clique curto / duplo / longo
  src/settings.cpp   NVS (Preferences)
  src/hid_launcher.cpp  sequência de teclado que abre o Chrome (não bloqueante)
  src/keymap_abnt2.cpp  mapas caractere -> tecla HID para US e ABNT2
  src/usb_bridge.cpp USB composta (CDC + HID), HELLO e transferência com crédito
  src/protocol.cpp   frames/CRC32/parser, espelho do protocolo da web
  src/web_portal.cpp AP + DNS captive + WebServer + upload em streaming
  include/web_portal_html.h  páginas do celular (envio e /config)
web/                 projeto Vite + TypeScript strict (rodar `npm` sempre de dentro daqui)
  src/protocol.ts    formato dos frames, CRC32, parser  <- fonte da verdade do protocolo
  src/serial.ts      Web Serial: conexão, reconexão, HELLO
  src/storage.ts     pasta (File System Access + IndexedDB), streaming, fallback download
  src/ui.ts, main.ts, style.css
.github/workflows/deploy-pages.yml   build e deploy do web/ no GitHub Pages
```

Não criar `platformio.ini` nem `package.json` na raiz.

## Comandos

```sh
# firmware
cd firmware && pio run                        # compila o env padrão (tdongle)
cd firmware && pio run -t upload              # grava
cd firmware && pio run -e tdongle-nullsink -t upload   # aceita uploads e descarta (mede o Wi-Fi sem PC)
cd firmware && pio run -e tdongle-demo -t upload       # clique curto percorre todas as telas
cd firmware && pio run -e tdongle-debug       # DEBUG_LOG=1 (logs via frame LOG)

# web
cd web && npm run dev          # http://localhost:5173 (localhost permite Web Serial)
cd web && npm run typecheck
cd web && npm run build        # tsc --noEmit + vite build -> web/dist
```

- **Gravação:** o reset para modo de gravação via DTR/RTS da CDC está ligado
  (`enableReboot(true)`), então o upload normalmente funciona direto. Se falhar,
  segure o botão BOOT ao plugar e grave de novo.
- **Deploy:** o workflow roda com push na `main` (quando muda `web/**`) ou
  manualmente pela aba Actions, a partir da `main`. Pages em `Source: GitHub Actions`.
  URL: https://gianantonicci.github.io/arquivos-tdongle-s3/
- **Git:** quem faz commit, push e deploy é o dono do repositório. **Agentes não
  devem commitar, dar push nem disparar deploy.**

## Publicação de versões nas GitHub Releases

**Toda nova versão do projeto deve ser publicada nas GitHub Releases do
repositório.** A entrega de uma versão só está completa quando a Release
correspondente estiver publicada.

- Usar uma tag semântica `vX.Y.Z`, apontando para o commit exato da versão
  validada e disponível no remoto. Nas versões do firmware, a tag deve
  corresponder a `FW_VERSION` em `firmware/include/config.h`; a versão do
  protocolo USB é independente e deve ser informada nas notas.
- Escrever notas em português com funcionalidades, correções, mudanças de
  compatibilidade, validações realizadas e limitações conhecidas.
- Anexar os binários de produção gerados para a versão, incluindo
  `firmware.bin` e `firmware.factory.bin`, com instruções de gravação e
  identificação do hardware e do ambiente de compilação. Identificar
  separadamente qualquer variante de debug, demonstração ou nullsink.
- Registrar a tag, o commit, o link da Release e os resultados das validações
  no histórico deste `AGENTS.md`, atualizando também o README quando necessário.
- Se a publicação não puder ser concluída, registrar a pendência e o motivo;
  não informar a versão como publicada. Esta regra não altera as restrições
  de commit, push e deploy descritas acima.

## Hardware (confirmado no repositório oficial da LilyGO)

| Função | GPIO |
|---|---|
| LCD MOSI / SCLK / CS / DC / RST | 3 / 5 / 4 / 2 / 1 |
| Backlight | 38 (ativo em LOW, PWM via LovyanGFX `Light_PWM` invertido) |
| LED APA102 DATA / CLK | 40 / 39 (ordem BGR) |
| Botão | 0 (BOOT) |

- 16 MB de flash QIO e **sem PSRAM** (~320 KB de RAM). Buffers pequenos e estáticos.
- Display ST7735S de 80x160 (usado em paisagem, 160x80): offset 26/1, invert, `rgb_order=false`.
- VID/PID USB `303A:82C1` (o PID registrado do T-Dongle S3). A página Web Serial filtra por eles.

## Regras de código

- **Comentários em português**, no estilo dos arquivos existentes.
- **Sem `String` dinâmica nos caminhos críticos** (dados de upload, frames,
  renderização). Use buffers estáticos e `snprintf`. `String` só é aceitável uma vez
  por requisição (ex.: ler a query string no início do upload).
- **A CDC é exclusiva do protocolo.** Nunca escreva texto solto nela. Logs vão como
  frame `LOG`, só com `DEBUG_LOG=1`, pela macro `DBG(...)`.
- **O protocolo tem que ficar idêntico nos dois lados**: `web/src/protocol.ts` e
  `firmware/include/protocol.h` / `firmware/src/protocol.cpp`. Mudou o formato
  em um lado, atualize o outro e o README.
- Acesso ao modelo da UI: `UiLock` quando altera (redesenha e conta como atividade
  do backlight) e `UiReadLock` quando só lê. Ler com `UiLock` impede o backlight de apagar.
- Compile os envs afetados depois de cada mudança. O código do projeto deve compilar
  **sem avisos**.
- Na web: TypeScript strict com `noUncheckedIndexedAccess`; sem frameworks nem
  bibliotecas de UI; nomes vindos do celular entram no DOM só por `textContent`.

## Decisões já tomadas (e por quê)

- **pioarduino 55.03.311 (Arduino-ESP32 3.3.11)**, não a plataforma `espressif32`
  oficial (que parou no core 2.0.x). A LilyGO pede core 3.3+ para USB/HID.
- **`ARDUINO_USB_MODE=0` e `ARDUINO_USB_CDC_ON_BOOT=0`** (com `build_unflags` dos
  valores da placa base). A CDC é criada à mão para fixar VID/PID e nomes antes de
  `USB.begin()`; o HID é registrado antes também (`hid::begin()` vem antes de
  `usb_bridge::begin()`).
- **LovyanGFX** em vez de TFT_eSPI (problemas conhecidos no core 3.x com S3). QR code
  pela função embutida da LovyanGFX. As fontes só têm ASCII: nomes com acento são
  convertidos para ASCII apenas na exibição (`foldAscii`).
- **Task de UI no core 0** (display + LED + botão): a tela continua viva enquanto o
  loop principal fica bloqueado dentro do callback de upload.
- **Upload com corpo cru** (`application/octet-stream`) em vez de multipart:
  o parser multipart do `WebServer` lê byte a byte. O caminho `HTTPRaw` lê blocos de
  `HTTP_RAW_BUFLEN=4096`. Multipart continua aceito (útil para `curl -F`). No raw,
  nome, tamanho, índice e total vão no cabeçalho `X-Upload-Metadata` como query
  URL-encoded UTF-8. O core 3.3.11 não interpreta a query da URL nesse caminho.
  `PortalWebServer::prepareRawMetadata()` interpreta o cabeçalho no RAW_START;
  `BridgeUploadHandler` separa raw/multipart pelos callbacks tipados. Nunca
  decidir o formato com `hasArg("raw")`: isso acessava `HTTPUpload` nulo e
  reiniciava o ESP32. No multipart os metadados continuam na query da URL.
- **Digitação independente de layout:** a URL é digitada sem `https://` (o `:` muda de
  tecla entre US e ABNT2) e o `/` sai pelo teclado numérico (0x54) nos dois mapas. A
  URL padrão só usa caracteres que ficam na mesma tecla nos dois layouts. O layout do
  menu só importa para comandos personalizados com `: ; ? ' "` etc.
- **macOS usa Cmd+T** (aba nova) em vez de Cmd+L, para não substituir a aba aberta.
- **Linux:** modo 0 = Alt+F2 + comando (padrão); modo 1 = Ctrl+Alt+T +
  `setsid -f <cmd> >/dev/null 2>&1 && exit` (`&&` em vez de `;` pelo mesmo motivo do layout).
- **Teclas mortas do ABNT2** (`~ ^ \``) recebem um Espaço depois. Caps Lock ligado é
  compensado (estado lido do relatório de LEDs do host).
- **HID automático uma vez por plug**, 2 s após a enumeração; cancelado se a página do
  PC já estiver mandando HELLO com destino pronto. Clique curto repete.
- **Escolhas do menu são salvas na hora** (desplugar dentro do menu não perde nada).
- **Captive portal:** o DNS responde o IP do dongle para qualquer domínio; os testes de
  conectividade são redirecionados (302) para `http://192.168.4.1/`. No core 3.x o DHCP
  só anuncia DNS se ele for passado no 5º parâmetro de `softAPConfig()`. A opção 114 do
  DHCP (RFC 8910) foi removida: exige uma API CAPPORT HTTPS, não a página HTML
  HTTP deste projeto. O portal usa DNS wildcard e redirecionamentos HTTP 302,
  inclusive rotas explícitas dos testes Android/Apple/Windows. IP/DHCP, DNS e
  HTTP são preparados antes de aplicar o SSID do projeto; erros de inicialização
  aparecem como aviso no display.

## Protocolo USB (fonte: `web/src/protocol.ts`)

Frame, todos os inteiros little-endian:

| off | tam | campo |
|---|---|---|
| 0 | 2 | magic `A5 5A` |
| 2 | 1 | tipo |
| 3 | 1 | flags (reservado, 0) |
| 4 | 2 | `len` do payload (máx. 8192) |
| 6 | len | payload |
| 6+len | 4 | CRC32 IEEE/zlib sobre os bytes 2..6+len |

| Tipo | Direção | Payload |
|---|---|---|
| `0x01` START | dongle→PC | id u32, size u64, index u16, count u16, nome UTF-8 |
| `0x02` DATA | dongle→PC | id u32, offset u64, dados (até 4096) |
| `0x03` END | dongle→PC | id u32, crc32 u32 do arquivo inteiro |
| `0x04` ABORT | dongle→PC | id u32, motivo u8 |
| `0x05` LOG | dongle→PC | texto UTF-8 (só com DEBUG_LOG) |
| `0x06` HELLO_ACK | dongle→PC | proto u8, janela u8, chunk u16, versão do firmware UTF-8 |
| `0x10` HELLO | PC→dongle | proto u8, flags u8 (bit0 = destino pronto) |
| `0x11` ACK | PC→dongle | id u32, offset u64 (bytes já gravados) |
| `0x12` NAK | PC→dongle | id u32, motivo u8 (dongle deve abortar) |
| `0x13` DONE | PC→dongle | id u32, status u8 (0 = gravado e CRC ok) |

Motivos (iguais ao `enum class ErrorReason` do firmware): 0 None, 1 Crc, 2 HostGone,
3 WifiLost, 4 Aborted, 5 NoHost, 6 DiskError, 7 Busy.

Fluxo: a página manda HELLO a cada 1 s. Host vivo = HELLO com bit "pronto" nos últimos
3,5 s. START → o PC cria o arquivo e responde **ACK(id, 0)** (ou NAK) → DATA com janela
de crédito (o dongle só lê mais do socket se houver crédito; cada ACK libera um) → END →
o PC confere tamanho e CRC e responde DONE → só então o dongle responde HTTP 200 ao celular.

Vetores para conferir a implementação C++:
- `crc32("123456789") = 0xCBF43926`
- `ackFrame(7, 4096)` = `a55a11000c000700000000100000000000003600479d`
- `helloFrame(true)` = `a55a100002000101646a0176`

## Situação atual

| Etapa | Estado |
|---|---|
| (a) display + LED + botão + menu/NVS | feita e testada no hardware |
| (b) USB CDC + HID abrindo o Chrome | feita e testada (Linux/GNOME funcionou) |
| (c) AP + captive portal + página do celular | acesso manual no celular confirmado pelo usuário; abertura automática a validar |
| (d) protocolo no firmware | implementado; testes nativos e parser web; JPEG de 4,14 MB validado no hardware com firmware 0.2.2 |
| (e) polimento, README, testes | README e testes de protocolo iniciados; testes físicos/medição pendentes |

### Portal no celular: acesso confirmado, abertura automática pendente

O problema inicial era a falta de acesso HTTP no Android, inclusive pelo IP
`192.168.4.1`. O firmware passou a anunciar DNS no DHCP (5º parâmetro do
`softAPConfig`), removeu o anúncio incorreto da opção 114 e a segunda
parada/reinicialização do DHCP, antecipou IP/DHCP e serviços em relação ao SSID,
verificou erros de inicialização e adicionou rotas dos testes de conectividade.
Depois dessas mudanças, o usuário confirmou acesso à página e o envio do JPEG.
A abertura automática da janela de login em cada celular ainda precisa ser
validada; o aviso "sem internet" é esperado nesta rede local.

Se a falha de acesso voltar, diagnosticar nesta ordem:
1. Confirmar que o firmware mais recente está gravado e que a rede foi esquecida e
   reconectada no celular; testar com os dados móveis desligados.
2. Pegar o IP e o gateway do celular (esperado `192.168.4.2`–`.12` / `192.168.4.1`).
3. Testar a partir do PC (exige autorização do usuário: o PC usa o Wi-Fi `FIAP-IOT` para
   a internet). Script: conectar no AP, registrar IP/rota/DNS, `dig @192.168.4.1`,
   `curl http://192.168.4.1/status`, e **sempre reconectar ao `FIAP-IOT` no final**
   (trap), já que a sessão do agente depende da internet.
4. Se continuar falhando, verificar os avisos de inicialização no display e se o
   loop principal está chegando em `web_portal::loop()`. A abertura automática
   depende da decisão do celular; acesso manual ao IP deve funcionar primeiro.

## Implementação atual e próximos passos

### Etapa (d): protocolo implementado, primeiro envio físico validado
- `protocol.h/.cpp` espelham o v1 da web; CRC32 ROM no ESP32 e referência portátil
  nos testes. Parser com ressincronização e limite estático de 8192 bytes.
- `usb_bridge.cpp`: HELLO pronto válido por 3,5 s; HELLO_ACK; START/ACK(0),
  DATA de até 4096 com janela 4, ACK cumulativo validado, NAK, END/DONE e
  ABORT idempotente. Timeout de ACK 15 s, DONE 30 s, exigindo HELLO vivo.
- CDC TX não bloqueante, escrita limitada ao `availableForWrite()` e a 64 bytes,
  com `delay(1)` entre pacotes e limite por frame de 1,5 s. A versão 0.2.2 usa
  esse envio conservador após observar um DATA de 4118 bytes truncado em 496
  bytes no Chrome (offset 24576; próximo DATA de offset 28672 já em 3622).
  A origem exata da perda dentro do caminho USB ainda não está comprovada.
  Com o espaçamento, o mesmo JPEG de 4.336.521 bytes concluiu no hardware com
  CRC/tamanho válidos, em 1min12s, média 58,9 KB/s. Firmware gravado e versão
  confirmada pela página do PC; download concluído. Otimizar vazão só mantendo
  essa validação; arquivos grandes e demais falhas ainda precisam de testes.
  `debugLog()` enfileira texto e o loop emite LOG, sem texto solto na CDC.
- Testes nativos em `firmware/test/`: vetores/ressincronização, ponte com CDC
  simulada e decodificação dos frames C++ pelo parser TypeScript real.
  Comandos no README. No PC é necessário clicar em Conectar e autorizar a USB;
  pasta bloqueada precisa ser liberada. A página publicada já é compatível.
- Medir vazão e ajustar janela/TX no hardware. O FIFO TX TinyUSB do core é de
  64 bytes (`CONFIG_TINYUSB_CDC_TX_BUFSIZE`), o que pode limitar a velocidade.

### Detecção automática de SO e layout
- SO ao plugar: implementado em `host_os.cpp` e `usb_os_fingerprint.h` por
  heurística das requisições STRING USB. `--wrap=dcd_event_handler` observa
  SETUP/reset sem alterar o core instalado; revisar esse ponto ao atualizar o core.
  Modo Auto é o padrão (`osmode` na NVS); escolhas manuais continuam disponíveis.
  O roteiro é escolhido ao fim da espera de enumeração. Sem assinatura, não digita
  e pede escolha manual. Precisa de validação física em Windows/macOS/Linux.
- Futuro, ainda não implementado: frame `CONFIG` (PC→dongle). A página detectaria
  o SO (`navigator.userAgentData.platform`)
  e o layout (`navigator.keyboard.getLayoutMap()`: tecla `Semicolon` = `ç` → ABNT2,
  `;` → US) e manda ao dongle, que grava em NVS. Os próximos plugs já usam os valores certos.
  Precisa ser adicionado em `protocol.ts`, `protocol.cpp` e no README ao mesmo tempo.
- O frame CONFIG ainda é uma melhoria futura para confirmação pelo navegador e
  detecção de layout; não é requisito para a heurística USB ao conectar.

### Etapa (e): documentação existente e testes pendentes
- `README.md` já documenta visão geral, gravação/modo boot, comandos da web,
  configuração, protocolo byte a byte, teste do layout com `KB_TEST_TEXT` e
  solução de problemas. Manter essas instruções alinhadas ao código e aos
  resultados de novos testes; publicação da web permanece descrita nos
  comandos e nas regras de deploy deste guia.
- Roteiro de testes: arquivos > 500 MB, nomes com acento e espaço, vários arquivos em
  sequência, Wi-Fi caindo no meio, dongle desplugado no meio, página do PC fechada no meio.
- Ajustar o chunk e a janela para chegar perto do limite do Wi-Fi (medir com `tdongle-nullsink`).
- Possíveis extras: distinguir "cancelado pelo celular" de "Wi-Fi caiu" (hoje os dois
  aparecem como `WifiLost`); página Web Serial notificar ao terminar um lote.

## Histórico de atividades dos agentes

### 2026-10-30 — Revisão da documentação e registro obrigatório

- **Trabalho:** revisados `README.md` e `AGENTS.md` contra o firmware 0.2.2 e
  os resultados do teste físico. Corrigidas referências antigas ao protocolo
  ainda não implementado e à falta de acesso HTTP no celular; separados os
  resultados já confirmados dos testes pendentes. Documentados o teste do
  teclado e a mudança de porta USB durante a gravação no macOS.
- **Regra adicionada:** toda atividade futura de agentes deve ser registrada
  neste arquivo, incluindo análises e diagnósticos sem alteração de código.
- **Validação:** conferidos a versão do firmware, o texto de `KB_TEST_TEXT`
  em relação ao README e os links locais; `git diff --check` sem problemas.
  Não foi necessário recompilar: este bloco alterou somente documentação.
- **Pendências:** arquivos grandes, lotes, quedas de conexão e abertura
  automática do portal em diferentes celulares continuam sem validação física
  completa. As últimas alterações documentais permanecem locais, sem commit
  ou push.

### 2026-10-30 — Regra de publicação de versões nas Releases

- **Trabalho:** adicionada ao `AGENTS.md` a obrigação de publicar cada nova
  versão nas GitHub Releases, com tag semântica, commit de referência, notas
  em português, binários e instruções de gravação. Definido o registro do link
  e das validações no histórico.
- **Validação:** revisão da consistência com as regras Git existentes e
  `git diff --check` sem problemas. Não houve compilação porque a alteração
  é exclusivamente documental.
- **Resultado:** regra documentada. Nenhuma Release foi criada nesta tarefa,
  cujo escopo foi adicionar a regra.

### 2026-10-30 — Publicação da versão 0.2.2 e regra de Releases

- **Trabalho concluído:** regra de Releases publicada em `dev`, PR de
  `dev` para `main` aberto e pacote de produção `v0.2.2` publicado. A `main`
  já continha o firmware pelo PR #2; a tag referencia o commit fonte
  `836f9c92747d189742f4dae37ffc044cd8046821`.
- **Artefatos:** `firmware.bin`, `firmware.factory.bin`, `INSTRUCOES.md`,
  `BUILD_INFO.json` e `SHA256SUMS`. O pacote identifica ambiente `tdongle`,
  ESP32-S3, flash de 16 MB sem PSRAM e protocolo USB v1.
- **Validação:** código de `firmware/`, `web/` e workflow idêntico ao commit
  fonte; imagens ESP32-S3 com checksum/hash válidos; aplicação da imagem
  factory idêntica ao binário individual; checksums SHA-256 do pacote
  conferidos. Reutilizadas as compilações, testes e validação física do JPEG
  já registrados; não houve mudança de código que exigisse nova compilação.
- **Release publicada:** [v0.2.2](https://github.com/GianAntonicci/arquivos-tdongle-s3/releases/tag/v0.2.2), com os cinco anexos
  conferidos após o upload (estado, tamanho e SHA-256).
- **Pull request:** [#3](https://github.com/GianAntonicci/arquivos-tdongle-s3/pull/3), de `dev` para `main`.
  A publicação da Release não fez merge do PR nem disparou deploy manual.
- **Pendências:** permanecem os testes físicos de arquivos grandes, lotes e
  falhas de conexão, além da otimização da vazão USB.
