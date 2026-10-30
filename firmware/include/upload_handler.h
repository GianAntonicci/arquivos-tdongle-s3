#pragma once
#include <WebServer.h>
#include <detail/RequestHandler.h>

// O core 3.3.11 não interpreta a query string no caminho HTTPRaw. Os mesmos
// metadados URL-encoded chegam em um cabeçalho, disponível antes de RAW_START.
class PortalWebServer : public WebServer {
  public:
    using WebServer::WebServer;
    bool prepareRawMetadata() {
        const String metadata = header("X-Upload-Metadata");
        _parseArguments(metadata);
        return metadata.length() && hasArg("name") && hasArg("size");
    }
};

// Usa o objeto fornecido pelo WebServer, sem inferir o formato por hasArg().
// No caminho raw, upload() referencia um ponteiro nulo (e vice-versa).
class BridgeUploadHandler : public RequestHandler {
  public:
    using Done = void (*)();
    using Raw = void (*)(HTTPRaw &);
    using Multipart = void (*)(HTTPUpload &);
    BridgeUploadHandler(Done done, Raw raw, Multipart multipart)
        : m_done(done), m_raw(raw), m_multipart(multipart) {}
    bool canHandle(WebServer &, HTTPMethod method, const String &uri) override {
        return method == HTTP_POST && uri == "/upload";
    }
    bool canRaw(WebServer &, const String &uri) override { return uri == "/upload"; }
    bool canUpload(WebServer &, const String &uri) override { return uri == "/upload"; }
    bool handle(WebServer &server, HTTPMethod method, const String &uri) override {
        if (!canHandle(server, method, uri)) return false;
        m_done();
        return true;
    }
    void raw(WebServer &, const String &, HTTPRaw &body) override { m_raw(body); }
    void upload(WebServer &, const String &, HTTPUpload &body) override { m_multipart(body); }
  private:
    Done m_done;
    Raw m_raw;
    Multipart m_multipart;
};
