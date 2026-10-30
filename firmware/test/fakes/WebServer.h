#pragma once
// Contrato mínimo do WebServer para testar o adaptador, sem ponteiros de corpo.
#include <assert.h>
#include <map>
#include <string>
#include <stddef.h>
using String = std::string;
enum HTTPMethod { HTTP_GET, HTTP_POST };
enum HTTPRawStatus { RAW_START, RAW_WRITE, RAW_END, RAW_ABORTED };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
struct HTTPRaw { HTTPRawStatus status = RAW_START; };
struct HTTPUpload { HTTPUploadStatus status = UPLOAD_FILE_START; String filename; };
class WebServer {
  public:
    explicit WebServer(int) {}
    std::map<String, String> testHeaders, testArgs;
    String header(const String &key) { return testHeaders[key]; }
    String arg(const String &key) { return testArgs[key]; }
    bool hasArg(const String &key) { return testArgs.count(key) != 0; }
    // A API real desreferencia um ponteiro nulo quando o formato é o outro.
    HTTPRaw &raw() { assert(false); return *static_cast<HTTPRaw *>(nullptr); }
    HTTPUpload &upload() { assert(false); return *static_cast<HTTPUpload *>(nullptr); }
  protected:
    void _parseArguments(const String &metadata) {
        testArgs.clear();
        size_t pos = 0;
        while (pos < metadata.length()) {
            size_t end = metadata.find('&', pos);
            if (end == String::npos) end = metadata.length();
            const size_t equal = metadata.find('=', pos);
            if (equal < end) testArgs[decode(metadata.substr(pos, equal - pos))] = decode(metadata.substr(equal + 1, end - equal - 1));
            pos = end + 1;
        }
    }
  private:
    static String decode(const String &value) {
        String out;
        for (size_t i = 0; i < value.length(); ++i) {
            if (value[i] == '%' && i + 2 < value.length()) {
                out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else out += value[i] == '+' ? ' ' : value[i];
        }
        return out;
    }
};
