#pragma once
#include <WebServer.h>
class RequestHandler {
  public:
    virtual ~RequestHandler() = default;
    virtual bool canHandle(WebServer &, HTTPMethod, const String &) { return false; }
    virtual bool canRaw(WebServer &, const String &) { return false; }
    virtual bool canUpload(WebServer &, const String &) { return false; }
    virtual bool handle(WebServer &, HTTPMethod, const String &) { return false; }
    virtual void raw(WebServer &, const String &, HTTPRaw &) {}
    virtual void upload(WebServer &, const String &, HTTPUpload &) {}
};
