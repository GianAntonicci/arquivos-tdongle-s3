#pragma once
// Wi-Fi em modo AP + captive portal + página de envio do celular.

namespace web_portal {
void begin();
void loop();   // DNS + HTTP; durante um upload fica aqui dentro (bloqueante)
}
