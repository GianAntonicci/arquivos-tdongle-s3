# T-Dongle Bridge

Ponte de arquivos do celular para o PC usando Wi-Fi no LilyGO T-Dongle S3,
USB CDC e uma página Web Serial no Chrome. Os arquivos passam pela RAM;
nunca são gravados na flash nem no cartão SD do dongle.

Estado atual: firmware **0.2.2**, protocolo USB **v1**. Um JPEG de 4,14 MB foi
recebido no hardware com tamanho/CRC válidos e download concluído no Chrome.
Arquivos grandes, lotes e quedas de conexão ainda precisam de validação física.

## Sistema operacional automático

Ao conectar ao computador, o firmware observa os tamanhos das requisições USB
de descritores de texto e estima se o host é Windows, macOS ou Linux. Não precisa
da página aberta nem de um programa instalado no PC para fazer essa estimativa.
Após a enumeração, espera 2 segundos antes de escolher o roteiro e digitar:

| Sistema | Ação |
| --- | --- |
| Windows | Win+R → `chrome <URL>` |
| macOS | Cmd+Espaço → Google Chrome → Cmd+T → URL |
| Linux | Alt+F2 → comando configurado (ou Ctrl+Alt+T, conforme a configuração) |

O modo **Auto** é o padrão, inclusive na primeira atualização de um firmware
antigo: a nova preferência usa a chave NVS `osmode`. Uma escolha manual feita
depois da atualização continua salva nos próximos plugs. A detecção em si não
é salva: cada conexão é analisada novamente. A tela de status e a tela
“Abrindo Chrome” mostram o sistema efetivo.

É uma heurística, baseada nos padrões documentados pelo
[QMK](https://docs.qmk.fm/features/os_detection), e precisa ser validada nos
computadores usados. Hubs, KVMs, máquinas virtuais e mudanças no SO podem mudar
o padrão. Se não reconhecer, o dongle não digita e mostra “Escolha o SO no menu”.
Segure BOOT para abrir o menu e selecionar Windows, macOS ou Linux, ou escolha
o SO em `http://192.168.4.1/config` pelo celular. Um clique curto repete a abertura.
Para voltar a detectar em cada conexão, selecione **Auto**.

O layout US/ABNT2 continua configurado separadamente. No Linux, o atalho e o
comando também dependem do ambiente gráfico e do nome do executável do Chrome.

## Compilar e gravar

Binários de produção e instruções estão na
[Release v0.2.2](https://github.com/GianAntonicci/arquivos-tdongle-s3/releases/tag/v0.2.2).
Confira `SHA256SUMS` antes de gravar; os offsets e a diferença entre imagem
factory e atualização somente da aplicação estão no anexo `INSTRUCOES.md`.

Execute de dentro de `firmware/`:

```sh
pio run
pio run -t upload
```

Se a gravação falhar, segure BOOT ao conectar o dongle e tente novamente.
Feche a aba Web Serial e outros programas que estejam usando a porta antes
de gravar. No macOS, a porta pode mudar ao entrar no bootloader; se aparecer
`Device not configured`, rode `pio device list` e repita a gravação com
`pio run -t upload --upload-port <porta-atual>`.

Para testar a classificação das sequências USB no computador:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -Iinclude test/usb_os_fingerprint_test.cpp -o /tmp/tdongle-os-test
/tmp/tdongle-os-test
```

Validação no hardware: conectar diretamente em Windows, macOS e Linux, conferir
o SO na tela e a abertura da página; repetir com hub, reconectar após desconectar
e testar uma escolha manual. Sem assinatura conhecida, conferir o aviso e a
ausência de teclas. Compilar não comprova a identificação em um computador real.

## Página do PC

Execute de dentro de `web/`:

```sh
npm ci
npm run dev
npm run typecheck
npm run build
```

Página publicada: https://gianantonicci.github.io/arquivos-tdongle-s3/

**Abrir a página não conecta a USB na primeira utilização.** No Chrome do PC,
clique em **Conectar**, escolha **T-Dongle Bridge** e confirme. Escolha uma pasta
para salvar os arquivos; se a pasta lembrada estiver bloqueada, libere a permissão.
O modo download também aceita arquivos, mas acumula os blocos no navegador;
prefira pasta para arquivos grandes. Mantenha a aba aberta durante o envio.
Sem confirmação do firmware, conferir se outra aba ou monitor serial ocupa a porta.
No Linux, conferir também a permissão de acesso à porta (grupo `dialout`).

Desde o firmware 0.2.0, a página recebe `HELLO_ACK` e exibe a versão do firmware,
protocolo e janela. No celular deve aparecer **PC pronto para receber** antes
do envio. A página já publicada usa o mesmo protocolo v1; não exige novo deploy.

A versão **0.2.1** corrige um acesso nulo no upload da página do celular que
reiniciava o ESP32. Depois de gravar, **recarregue a página no celular**: o envio
agora leva os metadados no cabeçalho `X-Upload-Metadata`. Uma aba antiga é
recusada com pedido de atualização, sem usar a estrutura de multipart.

A versão **0.2.2** altera o envio USB após um frame DATA ter chegado truncado ao
Chrome: escritas não bloqueantes de até 64 bytes, limitadas ao espaço disponível,
com 1 ms entre pacotes. É um ajuste conservador para validar a integridade;
a vazão fica limitada a aproximadamente 64 KB/s antes dos demais custos.
Os testes simulam FIFO cheio, escritas parciais e um arquivo de 4.336.521 bytes.
No hardware, o mesmo JPEG que falhava concluiu com tamanho/CRC válidos: 4.336.521
bytes em 1min12s, média 58,9 KB/s, download confirmado no Chrome. Isso valida
esse envio; arquivos grandes, lotes e quedas de conexão ainda precisam de testes.

## Testar o layout do teclado

Abra um editor de texto no PC e deixe o cursor no documento. No dongle, segure
BOOT para abrir o menu, navegue com cliques curtos e segure para ativar
**Testar teclado**. O texto esperado, definido em `KB_TEST_TEXT` no `main.cpp`, é:

```text
abc XYZ 0123456789 !@#$%&*()-_=+ []{}\| ;:'",.<>/? ~^` https://a.b/c?d=e_f
```

Se os símbolos saírem diferentes, selecione US ou ABNT2 no menu do dongle ou
no portal `/config`, conforme o layout ativo no PC. A detecção de SO não detecta
o layout. No macOS, conclua o Assistente de Configuração de Teclado, caso apareça,
antes de repetir o teste ou a abertura do Chrome.

## Portal no celular

Conecte ao Wi-Fi **TDongle-Bridge**, senha **bridge1234**. O dongle fornece
IP, gateway e DNS locais; os testes HTTP do celular são redirecionados para
`http://192.168.4.1/`. A rede não oferece acesso à internet. A janela de login
ou a notificação de portal depende do sistema do celular e pode não reaparecer
em toda reconexão. O firmware não consegue obrigar o celular a abrir o navegador.

Após atualizar o firmware, esqueça a rede no celular e conecte novamente para
renovar o DHCP. Aceite permanecer conectado se aparecer “sem internet”. Se a
página não abrir, teste `http://192.168.4.1/` no navegador, com dados móveis
desligados e DNS privado em Automático/desligado. Use HTTP, sem HTTPS.

Se nem o Wi-Fi conectar, verifique a senha e eventuais avisos de erro de
Wi-Fi/IP/DHCP no display. Se conectar mas o endereço local não abrir, confira
se o celular recebeu IP `192.168.4.2` a `.12` e gateway `192.168.4.1`.
Na janela de login de alguns celulares o seletor de arquivos não funciona;
nesse caso abra o endereço no navegador normal para enviar arquivos.

O envio usa `POST /upload`, corpo `application/octet-stream` e cabeçalho
`X-Upload-Metadata: name=foto.jpg&size=123&index=1&count=1` (o tamanho deve ser
o real, e os valores são URL-encoded UTF-8). Isso evita depender da query da
URL, que o core 3.3.11 não interpreta no caminho HTTPRaw. Multipart continua
aceito com seus metadados na query da URL.

Não anunciamos DHCP opção 114: ela descobre uma
[API CAPPORT](https://developer.android.com/about/versions/11/features/captive-portal),
não a página HTML HTTP. A detecção usada aqui é DNS e redirecionamento HTTP.

## Protocolo USB v1

CDC exclusiva de frames binários. Todos os inteiros são little-endian:

| Offset | Bytes | Campo |
| --- | --- | --- |
| 0 | 2 | Magic `A5 5A` |
| 2 | 1 | Tipo |
| 3 | 1 | Flags reservadas, zero |
| 4 | 2 | Tamanho N do payload, máximo 8192 |
| 6 | N | Payload |
| 6+N | 4 | CRC32 IEEE/zlib dos bytes de offset 2 até 5+N |

| Tipo | Direção | Payload |
| --- | --- | --- |
| `01` START | dongle → PC | id u32, tamanho u64, índice u16, quantidade u16, nome UTF-8 |
| `02` DATA | dongle → PC | id u32, offset u64, até 4096 bytes de dados |
| `03` END | dongle → PC | id u32, CRC32 u32 do arquivo |
| `04` ABORT | dongle → PC | id u32, motivo u8 |
| `05` LOG | dongle → PC | texto UTF-8, apenas com DEBUG_LOG=1 |
| `06` HELLO_ACK | dongle → PC | protocolo u8, janela u8, chunk u16, versão UTF-8 |
| `10` HELLO | PC → dongle | protocolo u8, flags u8 (bit 0 = destino pronto) |
| `11` ACK | PC → dongle | id u32, offset u64 já gravado |
| `12` NAK | PC → dongle | id u32, motivo u8 |
| `13` DONE | PC → dongle | id u32, status u8 (zero = sucesso) |

Motivos: 0 nenhum, 1 CRC, 2 PC ausente, 3 Wi-Fi perdido, 4 cancelado,
5 PC não pronto, 6 disco, 7 ocupado. Não há retransmissão: NAK/erro aborta o arquivo.

A página envia HELLO a cada segundo; o destino fica disponível por até 3,5 s
sem novo HELLO pronto. START espera ACK(id, 0). DATA usa uma janela de quatro
blocos; ACKs cumulativos liberam crédito somente em offsets efetivamente enviados.
Duplicatas e ACKs de outro id não liberam crédito. Ao encher a janela, a ponte
espera antes de devolver o controle ao leitor HTTP. END só sai após todos os ACKs;
o PC confere tamanho/CRC e fecha o arquivo antes de enviar DONE. Só então o celular
recebe HTTP 200. Sem progresso: até 15 s para ACK e 30 s para DONE, sempre exigindo
HELLO vivo. Arquivos usam blocos estáticos em RAM, sem armazenamento no dongle.

Vetores: `crc32("123456789") = CBF43926`,
`ACK(7, 4096) = a55a11000c000700000000100000000000003600479d`,
`HELLO(pronto) = a55a100002000101646a0176`.

## Testes do protocolo

Dentro de `firmware/`:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -Iinclude test/protocol_test.cpp src/protocol.cpp -o /tmp/tdongle-protocol-test
/tmp/tdongle-protocol-test
c++ -std=c++17 -Wall -Wextra -Werror -Itest/fakes -Iinclude test/usb_bridge_test.cpp src/protocol.cpp -o /tmp/tdongle-bridge-test
/tmp/tdongle-bridge-test /tmp/tdongle-transfer.bin
c++ -std=c++17 -Wall -Wextra -Werror -Itest/fakes -Iinclude test/upload_handler_test.cpp -o /tmp/tdongle-upload-test
/tmp/tdongle-upload-test
node test/portal_sender_test.cjs
```

Para conferir os frames C++ com o parser TypeScript real, depois dos testes acima,
execute de dentro de `web/` (dependências instaladas com `npm ci`):

```sh
./node_modules/.bin/tsc src/protocol.ts --module commonjs --target ES2020 --outDir /tmp/tdongle-protocol-js
node ../firmware/test/check_web_protocol.cjs /tmp/tdongle-protocol-js/protocol.js /tmp/tdongle-transfer.bin
```

Os testes simulam CDC com escritas parciais, crédito, ACK duplicado/cumulativo,
recusa, timeout e desconexão. Além do JPEG já confirmado no hardware, ainda
faltam testes físicos com arquivos grandes, lotes, nomes variados e quedas de
Wi-Fi/USB. Demais etapas em [AGENTS.md](AGENTS.md).
