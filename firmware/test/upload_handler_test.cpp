// Regressão: raw sem query interpretada nunca pode acessar HTTPUpload nulo.
#include "upload_handler.h"
#include <assert.h>
static PortalWebServer server(80);
static unsigned rawEvents = 0, multipartEvents = 0, done = 0, refused = 0;
static void onRaw(HTTPRaw &body) {
    ++rawEvents;
    if (body.status == RAW_START && !server.prepareRawMetadata()) ++refused;
}
static void onMultipart(HTTPUpload &body) {
    ++multipartEvents;
    assert(body.filename == "multipart.jpg");
}
int main() {
    BridgeUploadHandler handler([] { ++done; }, onRaw, onMultipart);
    RequestHandler &route = handler;
    assert(route.canHandle(server, HTTP_POST, "/upload"));
    assert(!route.canHandle(server, HTTP_GET, "/upload"));
    assert(!route.canHandle(server, HTTP_POST, "/status"));
    assert(route.canRaw(server, "/upload") && route.canUpload(server, "/upload"));
    // O parser do core entrega RAW_START sem interpretar os argumentos da URL.
    assert(!server.hasArg("raw"));
    server.testHeaders["X-Upload-Metadata"] = "name=f%C3%A9rias%20%26%20%2B.jpg&size=4097&index=1&count=2";
    HTTPRaw raw;
    route.raw(server, "/upload", raw);
    assert(rawEvents == 1 && multipartEvents == 0 && refused == 0);
    assert(server.arg("name") == "férias & +.jpg" && server.arg("size") == "4097");
    raw.status = RAW_WRITE; route.raw(server, "/upload", raw);
    raw.status = RAW_END; route.raw(server, "/upload", raw);
    assert(route.handle(server, HTTP_POST, "/upload") && done == 1);
    // Uma aba antiga não pode reutilizar os metadados do arquivo anterior.
    server.testHeaders.clear(); raw.status = RAW_START;
    route.raw(server, "/upload", raw);
    assert(refused == 1 && !server.hasArg("name") && !server.hasArg("size"));
    server.testHeaders["X-Upload-Metadata"] = "name=vazio&size=0&index=2&count=2";
    route.raw(server, "/upload", raw);
    assert(refused == 1 && server.arg("size") == "0");
    // Multipart usa seu próprio objeto e não passa pelo callback raw.
    HTTPUpload multipart; multipart.filename = "multipart.jpg";
    const unsigned before = rawEvents;
    route.upload(server, "/upload", multipart);
    assert(multipartEvents == 1 && rawEvents == before);
    assert(!route.handle(server, HTTP_GET, "/upload") && done == 1);
}
