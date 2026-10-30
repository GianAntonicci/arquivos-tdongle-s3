# AGENTS.md — T-Dongle Bridge

Guia para agentes (e pessoas) que forem mexer neste repositório: o que o projeto é,
como compilar, as regras que não podem ser quebradas, as decisões já tomadas e o que
ainda falta fazer.

## O projeto

O LilyGO T-Dongle S3 (ESP32-S3) vira uma **ponte de arquivos**:

```
Celular --(Wi-Fi AP + HTTP)--> T-Dongle --(USB CDC, frames binários)--> Página Web Serial (Chrome) --> pasta no PC
```

**Regra central: nenhum arquivo é armazenado no dongle.** Nada vai para SD nem para a
flash, só blocos em trânsito na RAM. O slot microSD não é usado. Na NVS ficam apenas
configurações (SO alvo, layout, URL, comando do Linux, brilho do LED).

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
  src/usb_bridge.cpp USB composta (CDC + HID); protocolo AINDA É STUB (etapa d)
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
  `firmware/src/protocol.cpp` (a criar). Mudou um, mude o outro e o README.
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
- **Upload com corpo cru** (`application/octet-stream`, `?raw=1`) em vez de multipart:
  o parser multipart do `WebServer` lê byte a byte. O caminho `HTTPRaw` lê blocos de
  `HTTP_RAW_BUFLEN=4096`. Multipart continua aceito (útil para `curl -F`). Nome,
  tamanho, índice e total vão na query string (URL-encoded, UTF-8).
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
  PC já estiver mandando HELLO. Clique curto repete.
- **Escolhas do menu são salvas na hora** (desplugar dentro do menu não perde nada).
- **Captive portal:** o DNS responde o IP do dongle para qualquer domínio; os testes de
  conectividade são redirecionados (302) para `http://192.168.4.1/`. No core 3.x o DHCP
  só anuncia DNS se ele for passado no 5º parâmetro de `softAPConfig()`. A opção 114 do
  DHCP (RFC 8910) também é enviada.

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
~3 s. START → o PC cria o arquivo e responde **ACK(id, 0)** (ou NAK) → DATA com janela
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
| (c) AP + captive portal + página do celular | código pronto; **problema aberto no hardware** (abaixo) |
| (d) protocolo no firmware | página web pronta e publicada; firmware ainda é stub |
| (e) polimento, README, testes | não começada |

### Problema aberto (etapa c)

No Android, o celular conecta ao `TDongle-Bridge` como "conectado sem internet" e
**nem `http://192.168.4.1` nem `http://connectivitycheck.gstatic.com/generate_204` abrem**
("não é possível acessar o site"). Ou seja, o celular não está falando com o dongle por
HTTP. Já aplicado: DNS anunciado no DHCP (5º parâmetro do `softAPConfig`) e opção 114.

Próximos passos de diagnóstico:
1. Confirmar que o firmware mais recente está gravado e que a rede foi esquecida e
   reconectada no celular; testar com os dados móveis desligados.
2. Pegar o IP e o gateway do celular (esperado `192.168.4.2`–`.12` / `192.168.4.1`).
3. Testar a partir do PC (exige autorização do usuário: o PC usa o Wi-Fi `FIAP-IOT` para
   a internet). Script: conectar no AP, registrar IP/rota/DNS, `dig @192.168.4.1`,
   `curl http://192.168.4.1/status`, e **sempre reconectar ao `FIAP-IOT` no final**
   (trap), já que a sessão do agente depende da internet.
4. Suspeitas a verificar: ordem `softAP()`/`softAPConfig()` no core 3.x (o IP pode não
   estar sendo aplicado); o efeito de parar e religar o DHCPS para a opção 114
   (testar sem esse bloco); se o loop principal está chegando em `web_portal::loop()`.

## Mudanças futuras

### Etapa (d): protocolo no firmware
- Criar `firmware/include/protocol.h` e `firmware/src/protocol.cpp`, espelhando
  `web/src/protocol.ts` (frames, CRC32 com `esp_rom_crc32_le`, conferindo com os vetores
  acima, parser de RX com ressincronização).
- Em `usb_bridge.cpp`, substituir os stubs:
  - `loop()`: ler a CDC, tratar HELLO (responder HELLO_ACK, marcar host vivo com
    timeout de ~3 s e o bit "pronto"), ACK, NAK e DONE;
  - `beginFile()`: recusar se não houver host (`NoHost`) ou se estiver ocupado;
    mandar START e esperar ACK(0) ou NAK;
  - `writeChunk()`: fatiar em DATA ≤ 4096, respeitar a janela de crédito (proposta: 4–8),
    ler a CDC enquanto espera, timeout → `HostGone`;
  - `endFile()`: mandar END com o CRC32 acumulado e esperar DONE;
  - `abortFile()`: mandar ABORT (idempotente).
- Ajustar o TX da CDC (`setTxTimeoutMs`) e medir a vazão: o FIFO TX do TinyUSB neste
  core é de 64 bytes (`CONFIG_TINYUSB_CDC_TX_BUFSIZE`), o que pode limitar a velocidade.
- Implementar `debugLog()` (frame LOG) para `DEBUG_LOG=1`.
- O HID automático passa a ser cancelado de fato quando a página já estiver aberta
  (`hostAlive()` real).

### Detecção automática de SO e layout (opção 2, combinada com o usuário)
- Frame novo `CONFIG` (PC→dongle): a página detecta o SO (`navigator.userAgentData.platform`)
  e o layout (`navigator.keyboard.getLayoutMap()`: tecla `Semicolon` = `ç` → ABNT2,
  `;` → US) e manda ao dongle, que grava em NVS. Os próximos plugs já usam os valores certos.
  Precisa ser adicionado em `protocol.ts`, `protocol.cpp` e no README ao mesmo tempo.
- Fingerprint USB do SO (padrão de requisições de descritores na enumeração): avaliado e
  **não recomendado** por ora (exige interceptar funções internas do TinyUSB, frágil).

### Etapa (e): polimento e testes
- `README.md` na raiz (português): visão geral, gravação (incluindo o modo boot),
  rodar e publicar a web, configuração, protocolo byte a byte, teste do layout
  (menu "Testar teclado", texto esperado em `KB_TEST_TEXT` no `main.cpp`) e solução de
  problemas (DNS privado e dados móveis no Android, Assistente de Teclado no macOS,
  grupo `dialout` no Linux para Web Serial, janela "Fazer login" do Android sem seletor
  de arquivos).
- Roteiro de testes: arquivos > 500 MB, nomes com acento e espaço, vários arquivos em
  sequência, Wi-Fi caindo no meio, dongle desplugado no meio, página do PC fechada no meio.
- Ajustar o chunk e a janela para chegar perto do limite do Wi-Fi (medir com `tdongle-nullsink`).
- Possíveis extras: distinguir "cancelado pelo celular" de "Wi-Fi caiu" (hoje os dois
  aparecem como `WifiLost`); página Web Serial notificar ao terminar um lote.
