// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef _ESPAsyncWebServer_H_
#define _ESPAsyncWebServer_H_

#include <Arduino.h>
#include <FS.h>
#include <lwip/tcpbase.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <list>
#include <unordered_map>
#include <vector>

#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESPAsyncTCP.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#include <HTTP_Method.h>
#include <http_parser.h>
#else
#error Platform not supported
#endif

#include "literals.h"

//#include "AsyncWebServerVersion.h"
#define ASYNCWEBSERVER_FORK_ESP32Async

#ifdef ASYNCWEBSERVER_REGEX
#define ASYNCWEBSERVER_REGEX_ATTRIBUTE
#else
#define ASYNCWEBSERVER_REGEX_ATTRIBUTE __attribute__((warning("ASYNCWEBSERVER_REGEX not defined")))
#endif

// See https://github.com/ESP32Async/ESPAsyncWebServer/commit/3d3456e9e81502a477f6498c44d0691499dda8f9#diff-646b25b11691c11dce25529e3abce843f0ba4bd07ab75ec9eee7e72b06dbf13fR388-R392
// This setting slowdown chunk serving but avoids crashing or deadlocks in the case where slow chunk responses are created, like file serving form SD Card
#ifndef ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
#define ASYNCWEBSERVER_USE_CHUNK_INFLIGHT 1
#endif

class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncWebServerResponse;
class AsyncWebHeader;
class AsyncWebParameter;
class AsyncWebRewrite;
class AsyncWebHandler;
class AsyncStaticWebHandler;
class AsyncCallbackWebHandler;
class AsyncResponseStream;
class AsyncMiddlewareChain;

#if defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
typedef enum http_method WebRequestMethod;
#else
#ifndef WEBSERVER_H
typedef enum {
  HTTP_GET       = 0b0000000000000001,
  HTTP_POST      = 0b0000000000000010,
  HTTP_DELETE    = 0b0000000000000100,
  HTTP_PUT       = 0b0000000000001000,
  HTTP_PATCH     = 0b0000000000010000,
  HTTP_HEAD      = 0b0000000000100000,
  HTTP_OPTIONS   = 0b0000000001000000,
  HTTP_PROPFIND  = 0b0000000010000000,
  HTTP_LOCK      = 0b0000000100000000,
  HTTP_UNLOCK    = 0b0000001000000000,
  HTTP_PROPPATCH = 0b0000010000000000,
  HTTP_MKCOL     = 0b0000100000000000,
  HTTP_MOVE      = 0b0001000000000000,
  HTTP_COPY      = 0b0010000000000000,
  HTTP_RESERVED  = 0b0100000000000000,
  HTTP_ANY       = 0b0111111111111111
} WebRequestMethod;
#endif
#endif

#ifndef HAVE_FS_FILE_OPEN_MODE
namespace fs {
class FileOpenMode {
public:
  static const char *read;
  static const char *write;
  static const char *append;
};
};  // namespace fs
#else
#include "FileOpenMode.h"
#endif

// if this value is returned when asked for data, packet will not be sent and you will be asked for data again
#define RESPONSE_TRY_AGAIN          0xFFFFFFFF
#define RESPONSE_STREAM_BUFFER_SIZE 1460

typedef uint16_t WebRequestMethodComposite;
typedef std::function<void(void)> ArDisconnectHandler;

/*
 * PARAMETER :: Chainable object to hold GET/POST and FILE parameters
 * */

class AsyncWebParameter {
private:
  String _name;
  String _value;
  size_t _size;
  bool _isForm;
  bool _isFile;

public:
  AsyncWebParameter(const String &name, const String &value, bool form = false, bool file = false, size_t size = 0)
    : _name(name), _value(value), _size(size), _isForm(form), _isFile(file) {}
  const String &name() const {
    return _name;
  }
  const String &value() const {
    return _value;
  }
  size_t size() const {
    return _size;
  }
  bool isPost() const {
    return _isForm;
  }
  bool isFile() const {
    return _isFile;
  }
};

/*
 * HEADER :: Chainable object to hold the headers
 * */

class AsyncWebHeader {
private:
  String _name;
  String _value;

public:
  AsyncWebHeader() {}
  AsyncWebHeader(const AsyncWebHeader &) = default;
  AsyncWebHeader(AsyncWebHeader &&) = default;
  AsyncWebHeader(const char *name, const char *value) : _name(name), _value(value) {}
  AsyncWebHeader(const String &name, const String &value) : _name(name), _value(value) {}

#ifndef ESP8266
  [[deprecated("Use AsyncWebHeader::parse(data) instead")]]
#endif
  AsyncWebHeader(const String &data)
    : AsyncWebHeader(parse(data)){};

  AsyncWebHeader &operator=(const AsyncWebHeader &) = default;
  AsyncWebHeader &operator=(AsyncWebHeader &&other) = default;

  const String &name() const {
    return _name;
  }
  const String &value() const {
    return _value;
  }

  String toString() const;

  // returns true if the header is valid
  operator bool() const {
    return _name.length();
  }

  static const AsyncWebHeader parse(const String &data) {
    return parse(data.c_str());
  }
  static const AsyncWebHeader parse(const char *data);
};

/*
 * REQUEST :: Each incoming Client is wrapped inside a Request and both live together until disconnect
 * */

typedef enum {
  RCT_NOT_USED = -1,
  RCT_DEFAULT = 0,
  RCT_HTTP,
  RCT_WS,
  RCT_EVENT,
  RCT_MAX
} RequestedConnectionType;

// this enum is similar to Arduino WebServer's AsyncAuthType and PsychicHttp
typedef enum {
  AUTH_NONE = 0,  // always allow
  AUTH_BASIC = 1,
  AUTH_DIGEST = 2,
  AUTH_BEARER = 3,
  AUTH_OTHER = 4,
  AUTH_DENIED = 255,  // always returns 401
} AsyncAuthType;

typedef std::function<size_t(uint8_t *, size_t, size_t)> AwsResponseFiller;
typedef std::function<String(const String &)> AwsTemplateProcessor;

using AsyncWebServerRequestPtr = std::weak_ptr<AsyncWebServerRequest>;

class AsyncWebServerRequest {
  using File = fs::File;
  using FS = fs::FS;
  friend class AsyncWebServer;
  friend class AsyncCallbackWebHandler;
  friend class AsyncFileResponse;

private:
  AsyncClient *_client;
  AsyncWebServer *_server;
  AsyncWebHandler *_handler;
  AsyncWebServerResponse *_response;
  ArDisconnectHandler _onDisconnectfn;

  bool _sent = false;                            // response is sent
  bool _paused = false;                          // request is paused (request continuation)
  std::shared_ptr<AsyncWebServerRequest> _this;  // shared pointer to this request

  String _temp;
  uint8_t _parseState;

  uint8_t _version;
  WebRequestMethodComposite _method;
  String _url;
  String _host;
  String _contentType;
  String _boundary;
  String _authorization;
  RequestedConnectionType _reqconntype;
  AsyncAuthType _authMethod = AsyncAuthType::AUTH_NONE;
  bool _isMultipart;
  bool _isPlainPost;
  bool _expectingContinue;
  size_t _contentLength;
  size_t _parsedLength;

  std::list<AsyncWebHeader> _headers;
  std::list<AsyncWebParameter> _params;
  std::list<String> _pathParams;

  std::unordered_map<const char *, String, std::hash<const char *>, std::equal_to<const char *>> _attributes;

  uint8_t _multiParseState;
  uint8_t _boundaryPosition;
  size_t _itemStartIndex;
  size_t _itemSize;
  String _itemName;
  String _itemFilename;
  String _itemType;
  String _itemValue;
  uint8_t *_itemBuffer;
  size_t _itemBufferIndex;
  bool _itemIsFile;

  void _onPoll();
  void _onAck(size_t len, uint32_t time);
  void _onError(int8_t error);
  void _onTimeout(uint32_t time);
  void _onDisconnect();
  void _onData(void *buf, size_t len);

  void _addPathParam(const char *param);

  bool _parseReqHead();
  bool _parseReqHeader();
  void _parseLine();
  void _parsePlainPostChar(uint8_t data);
  void _parseMultipartPostByte(uint8_t data, bool last);
  void _addGetParams(const String &params);

  void _handleUploadStart();
  void _handleUploadByte(uint8_t data, bool last);
  void _handleUploadEnd();

  void _send();
  void _runMiddlewareChain();

  static void _getEtag(uint8_t trailer[4], char *serverETag);

public:
  File _tempFile;
  void *_tempObject;

  AsyncWebServerRequest(AsyncWebServer *, AsyncClient *);
  ~AsyncWebServerRequest();

  AsyncClient *client() {
    return _client;
  }
  uint8_t version() const {
    return _version;
  }
  WebRequestMethodComposite method() const {
    return _method;
  }
  const String &url() const {
    return _url;
  }
  const String &host() const {
    return _host;
  }
  const String &contentType() const {
    return _contentType;
  }
  size_t contentLength() const {
    return _contentLength;
  }
  bool multipart() const {
    return _isMultipart;
  }

  const char *methodToString() const;
  const char *requestedConnTypeToString() const;

  RequestedConnectionType requestedConnType() const {
    return _reqconntype;
  }
  bool isExpectedRequestedConnType(RequestedConnectionType erct1, RequestedConnectionType erct2 = RCT_NOT_USED, RequestedConnectionType erct3 = RCT_NOT_USED)
    const;
  bool isWebSocketUpgrade() const {
    return _method == HTTP_GET && isExpectedRequestedConnType(RCT_WS);
  }
  bool isSSE() const {
    return _method == HTTP_GET && isExpectedRequestedConnType(RCT_EVENT);
  }
  bool isHTTP() const {
    return isExpectedRequestedConnType(RCT_DEFAULT, RCT_HTTP);
  }
  void onDisconnect(ArDisconnectHandler fn);

  // hash is the string representation of:
  //  base64(user:pass) for basic or
  //  user:realm:md5(user:realm:pass) for digest
  bool authenticate(const char *hash) const;
  bool authenticate(const char *username, const char *credentials, const char *realm = NULL, bool isHash = false) const;
  void requestAuthentication(const char *realm = nullptr, bool isDigest = true) {
    requestAuthentication(isDigest ? AsyncAuthType::AUTH_DIGEST : AsyncAuthType::AUTH_BASIC, realm);
  }
  void requestAuthentication(AsyncAuthType method, const char *realm = nullptr, const char *_authFailMsg = nullptr);

  // IMPORTANT: this method is for internal use ONLY
  // Please do not use it!
  // It can be removed or modified at any time without notice
  void setHandler(AsyncWebHandler *handler) {
    _handler = handler;
  }

#ifndef ESP8266
  [[deprecated("All headers are now collected. Use removeHeader(name) or AsyncHeaderFreeMiddleware if you really need to free some headers.")]]
#endif
  void addInterestingHeader(__unused const char *name) {
  }
#ifndef ESP8266
  [[deprecated("All headers are now collected. Use removeHeader(name) or AsyncHeaderFreeMiddleware if you really need to free some headers.")]]
#endif
  void addInterestingHeader(__unused const String &name) {
  }

  /**
     * @brief issue HTTP redirect response with Location header
     *
     * @param url - url to redirect to
     * @param code - response code, default is 302 : temporary redirect
     */
  void redirect(const char *url, int code = 302);
  void redirect(const String &url, int code = 302) {
    return redirect(url.c_str(), code);
  };

  void send(AsyncWebServerResponse *response);
  AsyncWebServerResponse *getResponse() const {
    return _response;
  }

  void send(int code, const char *contentType = asyncsrv::empty, const char *content = asyncsrv::empty, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(code, contentType, content, callback));
  }
  void send(int code, const String &contentType, const char *content = asyncsrv::empty, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(code, contentType.c_str(), content, callback));
  }
  void send(int code, const String &contentType, const String &content, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(code, contentType.c_str(), content.c_str(), callback));
  }

  void send(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(code, contentType, content, len, callback));
  }
  void send(int code, const String &contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(code, contentType, content, len, callback));
  }

  void send(FS &fs, const String &path, const char *contentType = asyncsrv::empty, bool download = false, AwsTemplateProcessor callback = nullptr);
  void send(FS &fs, const String &path, const String &contentType, bool download = false, AwsTemplateProcessor callback = nullptr) {
    send(fs, path, contentType.c_str(), download, callback);
  }

  void send(File content, const String &path, const char *contentType = asyncsrv::empty, bool download = false, AwsTemplateProcessor callback = nullptr) {
    if (content) {
      send(beginResponse(content, path, contentType, download, callback));
    } else {
      send(404);
    }
  }
  void send(File content, const String &path, const String &contentType, bool download = false, AwsTemplateProcessor callback = nullptr) {
    send(content, path, contentType.c_str(), download, callback);
  }

  void send(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(stream, contentType, len, callback));
  }
  void send(Stream &stream, const String &contentType, size_t len, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse(stream, contentType, len, callback));
  }

  void send(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr) {
    send(beginResponse(contentType, len, callback, templateCallback));
  }
  void send(const String &contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr) {
    send(beginResponse(contentType, len, callback, templateCallback));
  }

  void sendChunked(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr) {
    send(beginChunkedResponse(contentType, callback, templateCallback));
  }
  void sendChunked(const String &contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr) {
    send(beginChunkedResponse(contentType, callback, templateCallback));
  }

#ifndef ESP8266
  [[deprecated("Replaced by send(int code, const String& contentType, const uint8_t* content, size_t len, AwsTemplateProcessor callback = nullptr)")]]
#endif
  void send_P(int code, const String &contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr) {
    send(code, contentType, content, len, callback);
  }
#ifndef ESP8266
  [[deprecated("Replaced by send(int code, const String& contentType, const char* content = asyncsrv::empty, AwsTemplateProcessor callback = nullptr)")]]
  void send_P(int code, const String &contentType, PGM_P content, AwsTemplateProcessor callback = nullptr) {
    send(code, contentType, content, callback);
  }
#else
  void send_P(int code, const String &contentType, PGM_P content, AwsTemplateProcessor callback = nullptr) {
    send(beginResponse_P(code, contentType, content, callback));
  }
#endif

  AsyncWebServerResponse *
    beginResponse(int code, const char *contentType = asyncsrv::empty, const char *content = asyncsrv::empty, AwsTemplateProcessor callback = nullptr);
  AsyncWebServerResponse *beginResponse(int code, const String &contentType, const char *content = asyncsrv::empty, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(code, contentType.c_str(), content, callback);
  }
  AsyncWebServerResponse *beginResponse(int code, const String &contentType, const String &content, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(code, contentType.c_str(), content.c_str(), callback);
  }

  AsyncWebServerResponse *beginResponse(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr);
  AsyncWebServerResponse *beginResponse(int code, const String &contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(code, contentType.c_str(), content, len, callback);
  }

  AsyncWebServerResponse *
    beginResponse(FS &fs, const String &path, const char *contentType = asyncsrv::empty, bool download = false, AwsTemplateProcessor callback = nullptr);
  AsyncWebServerResponse *
    beginResponse(FS &fs, const String &path, const String &contentType = emptyString, bool download = false, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(fs, path, contentType.c_str(), download, callback);
  }

  AsyncWebServerResponse *
    beginResponse(File content, const String &path, const char *contentType = asyncsrv::empty, bool download = false, AwsTemplateProcessor callback = nullptr);
  AsyncWebServerResponse *
    beginResponse(File content, const String &path, const String &contentType = emptyString, bool download = false, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(content, path, contentType.c_str(), download, callback);
  }

  AsyncWebServerResponse *beginResponse(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback = nullptr);
  AsyncWebServerResponse *beginResponse(Stream &stream, const String &contentType, size_t len, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(stream, contentType.c_str(), len, callback);
  }

  AsyncWebServerResponse *beginResponse(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr);
  AsyncWebServerResponse *beginResponse(const String &contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr) {
    return beginResponse(contentType.c_str(), len, callback, templateCallback);
  }

  AsyncWebServerResponse *beginChunkedResponse(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr);
  AsyncWebServerResponse *beginChunkedResponse(const String &contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr) {
    return beginChunkedResponse(contentType.c_str(), callback, templateCallback);
  }

  AsyncResponseStream *beginResponseStream(const char *contentType, size_t bufferSize = RESPONSE_STREAM_BUFFER_SIZE);
  AsyncResponseStream *beginResponseStream(const String &contentType, size_t bufferSize = RESPONSE_STREAM_BUFFER_SIZE) {
    return beginResponseStream(contentType.c_str(), bufferSize);
  }

#ifndef ESP8266
  [[deprecated("Replaced by beginResponse(int code, const String& contentType, const uint8_t* content, size_t len, AwsTemplateProcessor callback = nullptr)")]]
#endif
  AsyncWebServerResponse *beginResponse_P(int code, const String &contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr) {
    return beginResponse(code, contentType.c_str(), content, len, callback);
  }
#ifndef ESP8266
  [[deprecated("Replaced by beginResponse(int code, const String& contentType, const char* content = asyncsrv::empty, AwsTemplateProcessor callback = nullptr)"
  )]]
#endif
  AsyncWebServerResponse *beginResponse_P(int code, const String &contentType, PGM_P content, AwsTemplateProcessor callback = nullptr);

  /**
   * @brief Request Continuation: this function pauses the current request and returns a weak pointer (AsyncWebServerRequestPtr is a std::weak_ptr) to the request in order to reuse it later on.
   * The middelware chain will continue to be processed until the end, but no response will be sent.
   * To resume operations (send the request), the request must be retrieved from the weak pointer and a send() function must be called.
   * AsyncWebServerRequestPtr is the only object allowed to exist the scope of the request handler.
   * @warning This function should be called from within the context of a request (in a handler or middleware for example).
   * @warning While the request is paused, if the client aborts the request, the latter will be disconnected and deleted.
   * So it is the responsibility of the user to check the validity of the request pointer (AsyncWebServerRequestPtr) before using it by calling lock() and/or expired().
   */
  AsyncWebServerRequestPtr pause();

  bool isPaused() const {
    return _paused;
  }

  /**
   * @brief Aborts the request and close the client (RST).
   * Mark the request as sent.
   * If it was paused, it will be unpaused and it won't be possible to resume it.
   */
  void abort();

  bool isSent() const {
    return _sent;
  }

  /**
     * @brief Get the Request parameter by name
     *
     * @param name
     * @param post
     * @param file
     * @return const AsyncWebParameter*
     */
  const AsyncWebParameter *getParam(const char *name, bool post = false, bool file = false) const;

  const AsyncWebParameter *getParam(const String &name, bool post = false, bool file = false) const {
    return getParam(name.c_str(), post, file);
  };
#ifdef ESP8266
  const AsyncWebParameter *getParam(const __FlashStringHelper *data, bool post, bool file) const;
#endif

  /**
     * @brief Get request parameter by number
     * i.e., n-th parameter
     * @param num
     * @return const AsyncWebParameter*
     */
  const AsyncWebParameter *getParam(size_t num) const;
  const AsyncWebParameter *getParam(int num) const {
    return num < 0 ? nullptr : getParam((size_t)num);
  }

  size_t args() const {
    return params();
  }  // get arguments count

  // get request argument value by name
  const String &arg(const char *name) const;
  // get request argument value by name
  const String &arg(const String &name) const {
    return arg(name.c_str());
  };
#ifdef ESP8266
  const String &arg(const __FlashStringHelper *data) const;  // get request argument value by F(name)
#endif
  const String &arg(size_t i) const;  // get request argument value by number
  const String &arg(int i) const {
    return i < 0 ? emptyString : arg((size_t)i);
  };
  const String &argName(size_t i) const;  // get request argument name by number
  const String &argName(int i) const {
    return i < 0 ? emptyString : argName((size_t)i);
  };
  bool hasArg(const char *name) const;  // check if argument exists
  bool hasArg(const String &name) const {
    return hasArg(name.c_str());
  };
#ifdef ESP8266
  bool hasArg(const __FlashStringHelper *data) const;  // check if F(argument) exists
#endif

  const String &ASYNCWEBSERVER_REGEX_ATTRIBUTE pathArg(size_t i) const;
  const String &ASYNCWEBSERVER_REGEX_ATTRIBUTE pathArg(int i) const {
    return i < 0 ? emptyString : pathArg((size_t)i);
  }

  // get request header value by name
  const String &header(const char *name) const;
  const String &header(const String &name) const {
    return header(name.c_str());
  };

#ifdef ESP8266
  const String &header(const __FlashStringHelper *data) const;  // get request header value by F(name)
#endif

  const String &header(size_t i) const;  // get request header value by number
  const String &header(int i) const {
    return i < 0 ? emptyString : header((size_t)i);
  };
  const String &headerName(size_t i) const;  // get request header name by number
  const String &headerName(int i) const {
    return i < 0 ? emptyString : headerName((size_t)i);
  };

  size_t headers() const;  // get header count

  // check if header exists
  bool hasHeader(const char *name) const;
  bool hasHeader(const String &name) const {
    return hasHeader(name.c_str());
  };
#ifdef ESP8266
  bool hasHeader(const __FlashStringHelper *data) const;  // check if header exists
#endif

  const AsyncWebHeader *getHeader(const char *name) const;
  const AsyncWebHeader *getHeader(const String &name) const {
    return getHeader(name.c_str());
  };
#ifdef ESP8266
  const AsyncWebHeader *getHeader(const __FlashStringHelper *data) const;
#endif

  const AsyncWebHeader *getHeader(size_t num) const;
  const AsyncWebHeader *getHeader(int num) const {
    return num < 0 ? nullptr : getHeader((size_t)num);
  };

  const std::list<AsyncWebHeader> &getHeaders() const {
    return _headers;
  }

  size_t getHeaderNames(std::vector<const char *> &names) const;

  // Remove a header from the request.
  // It will free the memory and prevent the header to be seen during request processing.
  bool removeHeader(const char *name);
  // Remove all request headers.
  void removeHeaders() {
    _headers.clear();
  }

  size_t params() const;  // get arguments count
  bool hasParam(const char *name, bool post = false, bool file = false) const;
  bool hasParam(const String &name, bool post = false, bool file = false) const {
    return hasParam(name.c_str(), post, file);
  };
#ifdef ESP8266
  bool hasParam(const __FlashStringHelper *data, bool post = false, bool file = false) const {
    return hasParam(String(data).c_str(), post, file);
  };
#endif

  // REQUEST ATTRIBUTES

  void setAttribute(const char *name, const char *value) {
    _attributes[name] = value;
  }
  void setAttribute(const char *name, bool value) {
    _attributes[name] = value ? "1" : emptyString;
  }
  void setAttribute(const char *name, long value) {
    _attributes[name] = String(value);
  }
  void setAttribute(const char *name, float value, unsigned int decimalPlaces = 2) {
    _attributes[name] = String(value, decimalPlaces);
  }
  void setAttribute(const char *name, double value, unsigned int decimalPlaces = 2) {
    _attributes[name] = String(value, decimalPlaces);
  }

  bool hasAttribute(const char *name) const {
    return _attributes.find(name) != _attributes.end();
  }

  const String &getAttribute(const char *name, const String &defaultValue = emptyString) const;
  bool getAttribute(const char *name, bool defaultValue) const;
  long getAttribute(const char *name, long defaultValue) const;
  float getAttribute(const char *name, float defaultValue) const;
  double getAttribute(const char *name, double defaultValue) const;

  String urlDecode(const String &text) const;
};

/*
 * FILTER :: Callback to filter AsyncWebRewrite and AsyncWebHandler (done by the Server)
 * */

using ArRequestFilterFunction = std::function<bool(AsyncWebServerRequest *request)>;

bool ON_STA_FILTER(AsyncWebServerRequest *request);

bool ON_AP_FILTER(AsyncWebServerRequest *request);

/*
 * MIDDLEWARE :: Request interceptor, assigned to a AsyncWebHandler (or the server), which can be used:
 * 1. to run some code before the final handler is executed (e.g. check authentication)
 * 2. decide whether to proceed or not with the next handler
 * */

using ArMiddlewareNext = std::function<void(void)>;
using ArMiddlewareCallback = std::function<void(AsyncWebServerRequest *request, ArMiddlewareNext next)>;

// Middleware is a base class for all middleware
class AsyncMiddleware {
public:
  virtual ~AsyncMiddleware() {}
  virtual void run(__unused AsyncWebServerRequest *request, __unused ArMiddlewareNext next) {
    return next();
  };

private:
  friend class AsyncWebHandler;
  friend class AsyncEventSource;
  friend class AsyncMiddlewareChain;
  bool _freeOnRemoval = false;
};

// Create a custom middleware by providing an anonymous callback function
class AsyncMiddlewareFunction : public AsyncMiddleware {
public:
  AsyncMiddlewareFunction(ArMiddlewareCallback fn) : _fn(fn) {}
  void run(AsyncWebServerRequest *request, ArMiddlewareNext next) override {
    return _fn(request, next);
  };

private:
  ArMiddlewareCallback _fn;
};

// For internal use only: super class to add/remove middleware to server or handlers
class AsyncMiddlewareChain {
public:
  ~AsyncMiddlewareChain();

  void addMiddleware(ArMiddlewareCallback fn);
  void addMiddleware(AsyncMiddleware *middleware);
  void addMiddlewares(std::vector<AsyncMiddleware *> middlewares);
  bool removeMiddleware(AsyncMiddleware *middleware);

  // For internal use only
  void _runChain(AsyncWebServerRequest *request, ArMiddlewareNext finalizer);

protected:
  std::list<AsyncMiddleware *> _middlewares;
};

// AsyncAuthenticationMiddleware is a middleware that checks if the request is authenticated
class AsyncAuthenticationMiddleware : public AsyncMiddleware {
public:
  void setUsername(const char *username);
  void setPassword(const char *password);
  void setPasswordHash(const char *hash);

  void setRealm(const char *realm) {
    _realm = realm;
  }
  void setAuthFailureMessage(const char *message) {
    _authFailMsg = message;
  }

  // set the authentication method to use
  // default is AUTH_NONE: no authentication required
  // AUTH_BASIC: basic authentication
  // AUTH_DIGEST: digest authentication
  // AUTH_BEARER: bearer token authentication
  // AUTH_OTHER: other authentication method
  // AUTH_DENIED: always return 401 Unauthorized
  // if a method is set but no username or password is set, authentication will be ignored
  void setAuthType(AsyncAuthType authMethod) {
    _authMethod = authMethod;
  }

  // precompute and store the hash value based on the username, password, realm.
  // can be used for DIGEST and BASIC to avoid recomputing the hash for each request.
  // returns true if the hash was successfully generated and replaced
  bool generateHash();

  // returns true if the username and password (or hash) are set
  bool hasCredentials() const {
    return _hasCreds;
  }

  bool allowed(AsyncWebServerRequest *request) const;

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next);

private:
  String _username;
  String _credentials;
  bool _hash = false;

  String _realm = asyncsrv::T_LOGIN_REQ;
  AsyncAuthType _authMethod = AsyncAuthType::AUTH_NONE;
  String _authFailMsg;
  bool _hasCreds = false;
};

using ArAuthorizeFunction = std::function<bool(AsyncWebServerRequest *request)>;
// AsyncAuthorizationMiddleware is a middleware that checks if the request is authorized
class AsyncAuthorizationMiddleware : public AsyncMiddleware {
public:
  AsyncAuthorizationMiddleware(ArAuthorizeFunction authorizeConnectHandler) : _code(403), _authz(authorizeConnectHandler) {}
  AsyncAuthorizationMiddleware(int code, ArAuthorizeFunction authorizeConnectHandler) : _code(code), _authz(authorizeConnectHandler) {}

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
    return _authz && !_authz(request) ? request->send(_code) : next();
  }

private:
  int _code;
  ArAuthorizeFunction _authz;
};

// remove all headers from the incoming request except the ones provided in the constructor
class AsyncHeaderFreeMiddleware : public AsyncMiddleware {
public:
  void keep(const char *name) {
    _toKeep.push_back(name);
  }
  void unKeep(const char *name) {
    _toKeep.remove(name);
  }

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next);

private:
  std::list<const char *> _toKeep;
};

// filter out specific headers from the incoming request
class AsyncHeaderFilterMiddleware : public AsyncMiddleware {
public:
  void filter(const char *name) {
    _toRemove.push_back(name);
  }
  void unFilter(const char *name) {
    _toRemove.remove(name);
  }

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next);

private:
  std::list<const char *> _toRemove;
};

// curl-like logging of incoming requests
class AsyncLoggingMiddleware : public AsyncMiddleware {
public:
  void setOutput(Print &output) {
    _out = &output;
  }
  void setEnabled(bool enabled) {
    _enabled = enabled;
  }
  bool isEnabled() const {
    return _enabled && _out;
  }

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next);

private:
  Print *_out = nullptr;
  bool _enabled = true;
};

// CORS Middleware
class AsyncCorsMiddleware : public AsyncMiddleware {
public:
  void setOrigin(const char *origin) {
    _origin = origin;
  }
  void setMethods(const char *methods) {
    _methods = methods;
  }
  void setHeaders(const char *headers) {
    _headers = headers;
  }
  void setAllowCredentials(bool credentials) {
    _credentials = credentials;
  }
  void setMaxAge(uint32_t seconds) {
    _maxAge = seconds;
  }

  void addCORSHeaders(AsyncWebServerResponse *response);

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next);

private:
  String _origin = "*";
  String _methods = "*";
  String _headers = "*";
  bool _credentials = true;
  uint32_t _maxAge = 86400;
};

// Rate limit Middleware
class AsyncRateLimitMiddleware : public AsyncMiddleware {
public:
  void setMaxRequests(size_t maxRequests) {
    _maxRequests = maxRequests;
  }
  void setWindowSize(uint32_t seconds) {
    _windowSizeMillis = seconds * 1000;
  }

  bool isRequestAllowed(uint32_t &retryAfterSeconds);

  void run(AsyncWebServerRequest *request, ArMiddlewareNext next);

private:
  size_t _maxRequests = 0;
  uint32_t _windowSizeMillis = 0;
  std::list<uint32_t> _requestTimes;
};

/*
 * REWRITE :: One instance can be handle any Request (done by the Server)
 * */

class AsyncWebRewrite {
protected:
  String _from;
  String _toUrl;
  String _params;
  ArRequestFilterFunction _filter{nullptr};

public:
  AsyncWebRewrite(const char *from, const char *to) : _from(from), _toUrl(to) {
    int index = _toUrl.indexOf('?');
    if (index > 0) {
      _params = _toUrl.substring(index + 1);
      _toUrl = _toUrl.substring(0, index);
    }
  }
  virtual ~AsyncWebRewrite() {}
  AsyncWebRewrite &setFilter(ArRequestFilterFunction fn) {
    _filter = fn;
    return *this;
  }
  bool filter(AsyncWebServerRequest *request) const {
    return _filter == NULL || _filter(request);
  }
  const String &from(void) const {
    return _from;
  }
  const String &toUrl(void) const {
    return _toUrl;
  }
  const String &params(void) const {
    return _params;
  }
  virtual bool match(AsyncWebServerRequest *request) {
    return from() == request->url() && filter(request);
  }
};

/*
 * HANDLER :: One instance can be attached to any Request (done by the Server)
 * */

class AsyncWebHandler : public AsyncMiddlewareChain {
protected:
  ArRequestFilterFunction _filter = nullptr;
  AsyncAuthenticationMiddleware *_authMiddleware = nullptr;
  bool _skipServerMiddlewares = false;

public:
  AsyncWebHandler() {}
  virtual ~AsyncWebHandler() {}
  AsyncWebHandler &setFilter(ArRequestFilterFunction fn);
  AsyncWebHandler &setAuthentication(const char *username, const char *password, AsyncAuthType authMethod = AsyncAuthType::AUTH_DIGEST);
  AsyncWebHandler &setAuthentication(const String &username, const String &password, AsyncAuthType authMethod = AsyncAuthType::AUTH_DIGEST) {
    return setAuthentication(username.c_str(), password.c_str(), authMethod);
  };
  AsyncWebHandler &setSkipServerMiddlewares(bool state) {
    _skipServerMiddlewares = state;
    return *this;
  }
  // skip all globally defined server middlewares for this handler and only execute those defined for this handler specifically
  AsyncWebHandler &skipServerMiddlewares() {
    return setSkipServerMiddlewares(true);
  }
  bool mustSkipServerMiddlewares() const {
    return _skipServerMiddlewares;
  }
  bool filter(AsyncWebServerRequest *request) {
    return _filter == NULL || _filter(request);
  }
  virtual bool canHandle(AsyncWebServerRequest *request __attribute__((unused))) const {
    return false;
  }
  virtual void handleRequest(__unused AsyncWebServerRequest *request) {}
  virtual void handleUpload(
    __unused AsyncWebServerRequest *request, __unused const String &filename, __unused size_t index, __unused uint8_t *data, __unused size_t len,
    __unused bool final
  ) {}
  virtual void handleBody(__unused AsyncWebServerRequest *request, __unused uint8_t *data, __unused size_t len, __unused size_t index, __unused size_t total) {}
  virtual bool isRequestHandlerTrivial() const {
    return true;
  }
};

/*
 * RESPONSE :: One instance is created for each Request (attached by the Handler)
 * */

typedef enum {
  RESPONSE_SETUP,
  RESPONSE_HEADERS,
  RESPONSE_CONTENT,
  RESPONSE_WAIT_ACK,
  RESPONSE_END,
  RESPONSE_FAILED
} WebResponseState;

class AsyncWebServerResponse {
protected:
  int _code;
  std::list<AsyncWebHeader> _headers;
  String _contentType;
  size_t _contentLength;
  bool _sendContentLength;
  bool _chunked;
  size_t _headLength;
  size_t _sentLength;
  size_t _ackedLength;
  size_t _writtenLength;
  WebResponseState _state;

  static bool headerMustBePresentOnce(const String &name);

public:
  static const char *responseCodeToString(int code);

public:
  AsyncWebServerResponse();
  virtual ~AsyncWebServerResponse() {}
  void setCode(int code);
  int code() const {
    return _code;
  }
  void setContentLength(size_t len);
  void setContentType(const String &type) {
    setContentType(type.c_str());
  }
  void setContentType(const char *type);
  bool addHeader(AsyncWebHeader &&header, bool replaceExisting = true);
  bool addHeader(const AsyncWebHeader &header, bool replaceExisting = true) {
    return header && addHeader(header.name(), header.value(), replaceExisting);
  }
  bool addHeader(const char *name, const char *value, bool replaceExisting = true);
  bool addHeader(const String &name, const String &value, bool replaceExisting = true) {
    return addHeader(name.c_str(), value.c_str(), replaceExisting);
  }
  bool addHeader(const char *name, long value, bool replaceExisting = true) {
    return addHeader(name, String(value), replaceExisting);
  }
  bool addHeader(const String &name, long value, bool replaceExisting = true) {
    return addHeader(name.c_str(), value, replaceExisting);
  }
  bool removeHeader(const char *name);
  bool removeHeader(const char *name, const char *value);
  const AsyncWebHeader *getHeader(const char *name) const;
  const std::list<AsyncWebHeader> &getHeaders() const {
    return _headers;
  }

#ifndef ESP8266
  [[deprecated("Use instead: _assembleHead(String& buffer, uint8_t version)")]]
#endif
  String _assembleHead(uint8_t version) {
    String buffer;
    _assembleHead(buffer, version);
    return buffer;
  }
  void _assembleHead(String &buffer, uint8_t version);

  virtual bool _started() const;
  virtual bool _finished() const;
  virtual bool _failed() const;
  virtual bool _sourceValid() const;
  virtual void _respond(AsyncWebServerRequest *request);
  virtual size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time);
};

/*
 * SERVER :: One instance
 * */

typedef std::function<void(AsyncWebServerRequest *request)> ArRequestHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)>
  ArUploadHandlerFunction;
typedef std::function<void(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)> ArBodyHandlerFunction;

class AsyncWebServer : public AsyncMiddlewareChain {
protected:
  AsyncServer _server;
  std::list<std::shared_ptr<AsyncWebRewrite>> _rewrites;
  std::list<std::unique_ptr<AsyncWebHandler>> _handlers;
  AsyncCallbackWebHandler *_catchAllHandler;

public:
  AsyncWebServer(uint16_t port);
  ~AsyncWebServer();

  void begin();
  void end();

  tcp_state state() const {
#ifdef ESP8266
    // ESPAsyncTCP and RPAsyncTCP methods are not corrected declared with const for immutable ones.
    return static_cast<tcp_state>(const_cast<AsyncWebServer *>(this)->_server.status());
#else
    return static_cast<tcp_state>(_server.status());
#endif
  }

#if ASYNC_TCP_SSL_ENABLED
  void onSslFileRequest(AcSSlFileHandler cb, void *arg);
  void beginSecure(const char *cert, const char *private_key_file, const char *password);
#endif

  AsyncWebRewrite &addRewrite(AsyncWebRewrite *rewrite);

  /**
     * @brief (compat) Add url rewrite rule by pointer
     * a deep copy of the pointer object will be created,
     * it is up to user to manage further lifetime of the object in argument
     *
     * @param rewrite pointer to rewrite object to copy setting from
     * @return AsyncWebRewrite& reference to a newly created rewrite rule
     */
  AsyncWebRewrite &addRewrite(std::shared_ptr<AsyncWebRewrite> rewrite);

  /**
     * @brief add url rewrite rule
     *
     * @param from
     * @param to
     * @return AsyncWebRewrite&
     */
  AsyncWebRewrite &rewrite(const char *from, const char *to);

  /**
     * @brief (compat) remove rewrite rule via referenced object
     * this will NOT deallocate pointed object itself, internal rule with same from/to urls will be removed if any
     * it's a compat method, better use `removeRewrite(const char* from, const char* to)`
     * @param rewrite
     * @return true
     * @return false
     */
  bool removeRewrite(AsyncWebRewrite *rewrite);

  /**
     * @brief remove rewrite rule
     *
     * @param from
     * @param to
     * @return true
     * @return false
     */
  bool removeRewrite(const char *from, const char *to);

  AsyncWebHandler &addHandler(AsyncWebHandler *handler);
  bool removeHandler(AsyncWebHandler *handler);

  AsyncCallbackWebHandler &on(const char *uri, ArRequestHandlerFunction onRequest) {
    return on(uri, HTTP_ANY, onRequest);
  }
  AsyncCallbackWebHandler &on(
    const char *uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload = nullptr,
    ArBodyHandlerFunction onBody = nullptr
  );

  AsyncStaticWebHandler &serveStatic(const char *uri, fs::FS &fs, const char *path, const char *cache_control = NULL);

  void onNotFound(ArRequestHandlerFunction fn);   // called when handler is not assigned
  void onFileUpload(ArUploadHandlerFunction fn);  // handle file uploads
  void onRequestBody(ArBodyHandlerFunction fn);   // handle posts with plain body content (JSON often transmitted this way as a request)
  // give access to the handler used to catch all requests, so that middleware can be added to it
  AsyncWebHandler &catchAllHandler() const;

  void reset();  // remove all writers and handlers, with onNotFound/onFileUpload/onRequestBody

  void _handleDisconnect(AsyncWebServerRequest *request);
  void _attachHandler(AsyncWebServerRequest *request);
  void _rewriteRequest(AsyncWebServerRequest *request);
};

class DefaultHeaders {
  using headers_t = std::list<AsyncWebHeader>;
  headers_t _headers;

public:
  DefaultHeaders() = default;

  using ConstIterator = headers_t::const_iterator;

  void addHeader(const String &name, const String &value) {
    _headers.emplace_back(name, value);
  }

  ConstIterator begin() const {
    return _headers.begin();
  }
  ConstIterator end() const {
    return _headers.end();
  }

  DefaultHeaders(DefaultHeaders const &) = delete;
  DefaultHeaders &operator=(DefaultHeaders const &) = delete;

  static DefaultHeaders &Instance() {
    static DefaultHeaders instance;
    return instance;
  }
};

//#include "AsyncEventSource.h"
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef ASYNCEVENTSOURCE_H_
#define ASYNCEVENTSOURCE_H_

#include <Arduino.h>

#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#ifdef LIBRETINY
#ifdef round
#undef round
#endif
#endif
#include <mutex>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 32
#endif
#define SSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q
#elif defined(ESP8266)
#include <ESPAsyncTCP.h>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 8
#endif
#define SSE_MIN_INFLIGH 2 * 1460  // allow 2 MSS packets
#define SSE_MAX_INFLIGH 8 * 1024  // but no more than 8k, no need to blow it, since same data is kept in local Q
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#ifndef SSE_MAX_QUEUED_MESSAGES
#define SSE_MAX_QUEUED_MESSAGES 32
#endif
#define SSE_MIN_INFLIGH 2 * 1460   // allow 2 MSS packets
#define SSE_MAX_INFLIGH 16 * 1024  // but no more than 16k, no need to blow it, since same data is kept in local Q
#endif

#include <ESPAsyncWebServer.h>

#ifdef ESP8266
#include <Hash.h>
#ifdef CRYPTO_HASH_h  // include Hash.h from espressif framework if the first include was from the crypto library
#include <../src/Hash.h>
#endif
#endif

class AsyncEventSource;
class AsyncEventSourceResponse;
class AsyncEventSourceClient;
using ArEventHandlerFunction = std::function<void(AsyncEventSourceClient *client)>;
using ArAuthorizeConnectHandler = ArAuthorizeFunction;
// shared message object container
using AsyncEvent_SharedData_t = std::shared_ptr<String>;

/**
 * @brief Async Event Message container with shared message content data
 *
 */
class AsyncEventSourceMessage {

private:
  const AsyncEvent_SharedData_t _data;
  size_t _sent{0};   // num of bytes already sent
  size_t _acked{0};  // num of bytes acked

public:
  AsyncEventSourceMessage(AsyncEvent_SharedData_t data) : _data(data){};
#if defined(ESP32)
  AsyncEventSourceMessage(const char *data, size_t len) : _data(std::make_shared<String>(data, len)){};
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
  AsyncEventSourceMessage(const char *data, size_t len) : _data(std::make_shared<String>()) {
    if (data && len > 0) {
      _data->concat(data, len);
    }
  };
#else
  // esp8266's String does not have constructor with data/length arguments. Use a concat method here
  AsyncEventSourceMessage(const char *data, size_t len) {
    _data->concat(data, len);
  };
#endif

  /**
     * @brief acknowledge sending len bytes of data
     * @note if num of bytes to ack is larger then the unacknowledged message length the number of carried over bytes are returned
     *
     * @param len bytes to acknowledge
     * @param time
     * @return size_t number of extra bytes carried over
     */
  size_t ack(size_t len, uint32_t time = 0);

  /**
     * @brief write message data to client's buffer
     * @note this method does NOT call client's send
     *
     * @param client
     * @return size_t number of bytes written
     */
  size_t write(AsyncClient *client);

  /**
     * @brief writes message data to client's buffer and calls client's send method
     *
     * @param client
     * @return size_t returns num of bytes the clien was able to send()
     */
  size_t send(AsyncClient *client);

  // returns true if full message's length were acked
  bool finished() {
    return _acked == _data->length();
  }

  /**
     * @brief returns true if all data has been sent already
     *
     */
  bool sent() {
    return _sent == _data->length();
  }
};

/**
 * @brief class holds a sse messages queue for a particular client's connection
 *
 */
class AsyncEventSourceClient {
private:
  AsyncClient *_client;
  AsyncEventSource *_server;
  uint32_t _lastId{0};
  size_t _inflight{0};                    // num of unacknowledged bytes that has been written to socket buffer
  size_t _max_inflight{SSE_MAX_INFLIGH};  // max num of unacknowledged bytes that could be written to socket buffer
  std::list<AsyncEventSourceMessage> _messageQueue;
#ifdef ESP32
  mutable std::recursive_mutex _lockmq;
#endif
  bool _queueMessage(const char *message, size_t len);
  bool _queueMessage(AsyncEvent_SharedData_t &&msg);
  void _runQueue();

public:
  AsyncEventSourceClient(AsyncWebServerRequest *request, AsyncEventSource *server);
  ~AsyncEventSourceClient();

  /**
     * @brief Send an SSE message to client
     * it will craft an SSE message and place it to client's message queue
     *
     * @param message body string, could be single or multi-line string sepprated by \n, \r, \r\n
     * @param event body string, a sinle line string
     * @param id sequence id
     * @param reconnect client's reconnect timeout
     * @return true if message was placed in a queue
     * @return false if queue is full
     */
  bool send(const char *message, const char *event = NULL, uint32_t id = 0, uint32_t reconnect = 0);
  bool send(const String &message, const String &event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event.c_str(), id, reconnect);
  }
  bool send(const String &message, const char *event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event, id, reconnect);
  }

  /**
     * @brief place supplied preformatted SSE message to the message queue
     * @note message must a properly formatted SSE string according to https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events
     *
     * @param message data
     * @return true on success
     * @return false on queue overflow or no client connected
     */
  bool write(AsyncEvent_SharedData_t message) {
    return connected() && _queueMessage(std::move(message));
  };

  [[deprecated("Use _write(AsyncEvent_SharedData_t message) instead to share same data with multiple SSE clients")]]
  bool write(const char *message, size_t len) {
    return connected() && _queueMessage(message, len);
  };

  // close client's connection
  void close();

  // getters

  AsyncClient *client() {
    return _client;
  }
  bool connected() const {
    return _client && _client->connected();
  }
  uint32_t lastId() const {
    return _lastId;
  }
  size_t packetsWaiting() const {
    return _messageQueue.size();
  };

  /**
     * @brief Sets max amount of bytes that could be written to client's socket while awaiting delivery acknowledge
     * used to throttle message delivery length to tradeoff memory consumption
     * @note actual amount of data written could possible be a bit larger but no more than available socket buff space
     *
     * @param value
     */
  void set_max_inflight_bytes(size_t value);

  /**
     * @brief Get current max inflight bytes value
     *
     * @return size_t
     */
  size_t get_max_inflight_bytes() const {
    return _max_inflight;
  }

  // system callbacks (do not call if from user code!)
  void _onAck(size_t len, uint32_t time);
  void _onPoll();
  void _onTimeout(uint32_t time);
  void _onDisconnect();
};

/**
 * @brief a class that maintains all connected HTTP clients subscribed to SSE delivery
 * dispatches supplied messages to the client's queues
 *
 */
class AsyncEventSource : public AsyncWebHandler {
private:
  String _url;
  std::list<std::unique_ptr<AsyncEventSourceClient>> _clients;
#ifdef ESP32
  // Same as for individual messages, protect mutations of _clients list
  // since simultaneous access from different tasks is possible
  mutable std::recursive_mutex _client_queue_lock;
#endif
  ArEventHandlerFunction _connectcb = nullptr;
  ArEventHandlerFunction _disconnectcb = nullptr;

  // this method manipulates in-fligh data size for connected client depending on number of active connections
  void _adjust_inflight_window();

public:
  typedef enum {
    DISCARDED = 0,
    ENQUEUED = 1,
    PARTIALLY_ENQUEUED = 2,
  } SendStatus;

  AsyncEventSource(const char *url) : _url(url){};
  AsyncEventSource(const String &url) : _url(url){};
  ~AsyncEventSource() {
    close();
  };

  const char *url() const {
    return _url.c_str();
  }
  // close all connected clients
  void close();

  /**
     * @brief set on-connect callback for the client
     * used to deliver messages to client on first connect
     *
     * @param cb
     */
  void onConnect(ArEventHandlerFunction cb) {
    _connectcb = cb;
  }

  /**
     * @brief Send an SSE message to client
     * it will craft an SSE message and place it to all connected client's message queues
     *
     * @param message body string, could be single or multi-line string sepprated by \n, \r, \r\n
     * @param event body string, a sinle line string
     * @param id sequence id
     * @param reconnect client's reconnect timeout
     * @return SendStatus if message was placed in any/all/part of the client's queues
     */
  SendStatus send(const char *message, const char *event = NULL, uint32_t id = 0, uint32_t reconnect = 0);
  SendStatus send(const String &message, const String &event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event.c_str(), id, reconnect);
  }
  SendStatus send(const String &message, const char *event, uint32_t id = 0, uint32_t reconnect = 0) {
    return send(message.c_str(), event, id, reconnect);
  }

  // The client pointer sent to the callback is only for reference purposes. DO NOT CALL ANY METHOD ON IT !
  void onDisconnect(ArEventHandlerFunction cb) {
    _disconnectcb = cb;
  }
  void authorizeConnect(ArAuthorizeConnectHandler cb);

  // returns number of connected clients
  size_t count() const;

  // returns average number of messages pending in all client's queues
  size_t avgPacketsWaiting() const;

  // system callbacks (do not call from user code!)
  void _addClient(AsyncEventSourceClient *client);
  void _handleDisconnect(AsyncEventSourceClient *client);
  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
};

class AsyncEventSourceResponse : public AsyncWebServerResponse {
private:
  AsyncEventSource *_server;

public:
  AsyncEventSourceResponse(AsyncEventSource *server);
  void _respond(AsyncWebServerRequest *request);
  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time);
  bool _sourceValid() const {
    return true;
  }
};

#endif /* ASYNCEVENTSOURCE_H_ */



//#include "AsyncWebSocket.h"
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef ASYNCWEBSOCKET_H_
#define ASYNCWEBSOCKET_H_

#include <Arduino.h>

#if defined(ESP32) || defined(LIBRETINY)
#include <AsyncTCP.h>
#ifdef LIBRETINY
#ifdef round
#undef round
#endif
#endif
#include <mutex>
#ifndef WS_MAX_QUEUED_MESSAGES
#define WS_MAX_QUEUED_MESSAGES 32
#endif
#elif defined(ESP8266)
#include <ESPAsyncTCP.h>
#ifndef WS_MAX_QUEUED_MESSAGES
#define WS_MAX_QUEUED_MESSAGES 8
#endif
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <RPAsyncTCP.h>
#ifndef WS_MAX_QUEUED_MESSAGES
#define WS_MAX_QUEUED_MESSAGES 32
#endif
#endif

#include <ESPAsyncWebServer.h>

#include <memory>

#ifdef ESP8266
#include <Hash.h>
#ifdef CRYPTO_HASH_h  // include Hash.h from espressif framework if the first include was from the crypto library
#include <../src/Hash.h>
#endif
#endif

#ifndef DEFAULT_MAX_WS_CLIENTS
#ifdef ESP32
#define DEFAULT_MAX_WS_CLIENTS 8
#else
#define DEFAULT_MAX_WS_CLIENTS 4
#endif
#endif

using AsyncWebSocketSharedBuffer = std::shared_ptr<std::vector<uint8_t>>;

class AsyncWebSocket;
class AsyncWebSocketResponse;
class AsyncWebSocketClient;
class AsyncWebSocketControl;

typedef struct {
  /** Message type as defined by enum AwsFrameType.
     * Note: Applications will only see WS_TEXT and WS_BINARY.
     * All other types are handled by the library. */
  uint8_t message_opcode;
  /** Frame number of a fragmented message. */
  uint32_t num;
  /** Is this the last frame in a fragmented message ?*/
  uint8_t final;
  /** Is this frame masked? */
  uint8_t masked;
  /** Message type as defined by enum AwsFrameType.
     * This value is the same as message_opcode for non-fragmented
     * messages, but may also be WS_CONTINUATION in a fragmented message. */
  uint8_t opcode;
  /** Length of the current frame.
     * This equals the total length of the message if num == 0 && final == true */
  uint64_t len;
  /** Mask key */
  uint8_t mask[4];
  /** Offset of the data inside the current frame. */
  uint64_t index;
} AwsFrameInfo;

typedef enum {
  WS_DISCONNECTED,
  WS_CONNECTED,
  WS_DISCONNECTING
} AwsClientStatus;
typedef enum {
  WS_CONTINUATION,
  WS_TEXT,
  WS_BINARY,
  WS_DISCONNECT = 0x08,
  WS_PING,
  WS_PONG
} AwsFrameType;
typedef enum {
  WS_MSG_SENDING,
  WS_MSG_SENT,
  WS_MSG_ERROR
} AwsMessageStatus;
typedef enum {
  WS_EVT_CONNECT,
  WS_EVT_DISCONNECT,
  WS_EVT_PING,
  WS_EVT_PONG,
  WS_EVT_ERROR,
  WS_EVT_DATA
} AwsEventType;

class AsyncWebSocketMessageBuffer {
  friend AsyncWebSocket;
  friend AsyncWebSocketClient;

private:
  AsyncWebSocketSharedBuffer _buffer;

public:
  AsyncWebSocketMessageBuffer() {}
  explicit AsyncWebSocketMessageBuffer(size_t size);
  AsyncWebSocketMessageBuffer(const uint8_t *data, size_t size);
  //~AsyncWebSocketMessageBuffer();
  bool reserve(size_t size);
  uint8_t *get() {
    return _buffer->data();
  }
  size_t length() const {
    return _buffer->size();
  }
};

class AsyncWebSocketMessage {
private:
  AsyncWebSocketSharedBuffer _WSbuffer;
  uint8_t _opcode{WS_TEXT};
  bool _mask{false};
  AwsMessageStatus _status{WS_MSG_ERROR};
  size_t _sent{};
  size_t _ack{};
  size_t _acked{};

public:
  AsyncWebSocketMessage(AsyncWebSocketSharedBuffer buffer, uint8_t opcode = WS_TEXT, bool mask = false);

  bool finished() const {
    return _status != WS_MSG_SENDING;
  }
  bool betweenFrames() const {
    return _acked == _ack;
  }

  void ack(size_t len, uint32_t time);
  size_t send(AsyncClient *client);
};

class AsyncWebSocketClient {
private:
  AsyncClient *_client;
  AsyncWebSocket *_server;
  uint32_t _clientId;
  AwsClientStatus _status;
#ifdef ESP32
  mutable std::recursive_mutex _lock;
#endif
  std::deque<AsyncWebSocketControl> _controlQueue;
  std::deque<AsyncWebSocketMessage> _messageQueue;
  bool closeWhenFull = true;

  uint8_t _pstate;
  AwsFrameInfo _pinfo;

  uint32_t _lastMessageTime;
  uint32_t _keepAlivePeriod;

  bool _queueControl(uint8_t opcode, const uint8_t *data = NULL, size_t len = 0, bool mask = false);
  bool _queueMessage(AsyncWebSocketSharedBuffer buffer, uint8_t opcode = WS_TEXT, bool mask = false);
  void _runQueue();
  void _clearQueue();

public:
  void *_tempObject;

  AsyncWebSocketClient(AsyncWebServerRequest *request, AsyncWebSocket *server);
  ~AsyncWebSocketClient();

  // client id increments for the given server
  uint32_t id() const {
    return _clientId;
  }
  AwsClientStatus status() const {
    return _status;
  }
  AsyncClient *client() {
    return _client;
  }
  const AsyncClient *client() const {
    return _client;
  }
  AsyncWebSocket *server() {
    return _server;
  }
  const AsyncWebSocket *server() const {
    return _server;
  }
  AwsFrameInfo const &pinfo() const {
    return _pinfo;
  }

  //  - If "true" (default), the connection will be closed if the message queue is full.
  // This is the default behavior in yubox-node-org, which is not silently discarding messages but instead closes the connection.
  // The big issue with this behavior is  that is can cause the UI to automatically re-create a new WS connection, which can be filled again,
  // and so on, causing a resource exhaustion.
  //
  // - If "false", the incoming message will be discarded if the queue is full.
  // This is the default behavior in the original ESPAsyncWebServer library from me-no-dev.
  // This behavior allows the best performance at the expense of unreliable message delivery in case the queue is full (some messages may be lost).
  //
  // - In any case, when the queue is full, a message is logged.
  // - IT is recommended to use the methods queueIsFull(), availableForWriteAll(), availableForWrite(clientId) to check if the queue is full before sending a message.
  //
  // Usage:
  //  - can be set in the onEvent listener when connecting (event type is: WS_EVT_CONNECT)
  //
  // Use cases:,
  // - if using websocket to send logging messages, maybe some loss is acceptable.
  // - But if using websocket to send UI update messages, maybe the connection should be closed and the UI redrawn.
  void setCloseClientOnQueueFull(bool close) {
    closeWhenFull = close;
  }
  bool willCloseClientOnQueueFull() const {
    return closeWhenFull;
  }

  IPAddress remoteIP() const;
  uint16_t remotePort() const;

  bool shouldBeDeleted() const {
    return !_client;
  }

  // control frames
  void close(uint16_t code = 0, const char *message = NULL);
  bool ping(const uint8_t *data = NULL, size_t len = 0);

  // set auto-ping period in seconds. disabled if zero (default)
  void keepAlivePeriod(uint16_t seconds) {
    _keepAlivePeriod = seconds * 1000;
  }
  uint16_t keepAlivePeriod() {
    return (uint16_t)(_keepAlivePeriod / 1000);
  }

  // data packets
  void message(AsyncWebSocketSharedBuffer buffer, uint8_t opcode = WS_TEXT, bool mask = false) {
    _queueMessage(buffer, opcode, mask);
  }
  bool queueIsFull() const;
  size_t queueLen() const;

  size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3)));

  bool text(AsyncWebSocketSharedBuffer buffer);
  bool text(const uint8_t *message, size_t len);
  bool text(const char *message, size_t len);
  bool text(const char *message);
  bool text(const String &message);
  bool text(AsyncWebSocketMessageBuffer *buffer);

  bool binary(AsyncWebSocketSharedBuffer buffer);
  bool binary(const uint8_t *message, size_t len);
  bool binary(const char *message, size_t len);
  bool binary(const char *message);
  bool binary(const String &message);
  bool binary(AsyncWebSocketMessageBuffer *buffer);

  bool canSend() const;

  // system callbacks (do not call)
  void _onAck(size_t len, uint32_t time);
  void _onError(int8_t);
  void _onPoll();
  void _onTimeout(uint32_t time);
  void _onDisconnect();
  void _onData(void *pbuf, size_t plen);

#ifdef ESP8266
  size_t printf_P(PGM_P formatP, ...) __attribute__((format(printf, 2, 3)));
  bool text(const __FlashStringHelper *message);
  bool binary(const __FlashStringHelper *message, size_t len);
#endif
};

using AwsHandshakeHandler = std::function<bool(AsyncWebServerRequest *request)>;
using AwsEventHandler = std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)>;

// WebServer Handler implementation that plays the role of a socket server
class AsyncWebSocket : public AsyncWebHandler {
private:
  String _url;
  std::list<AsyncWebSocketClient> _clients;
  uint32_t _cNextId;
  AwsEventHandler _eventHandler;
  AwsHandshakeHandler _handshakeHandler;
  bool _enabled;
#ifdef ESP32
  mutable std::mutex _lock;
#endif

public:
  typedef enum {
    DISCARDED = 0,
    ENQUEUED = 1,
    PARTIALLY_ENQUEUED = 2,
  } SendStatus;

  explicit AsyncWebSocket(const char *url, AwsEventHandler handler = nullptr) : _url(url), _cNextId(1), _eventHandler(handler), _enabled(true) {}
  AsyncWebSocket(const String &url, AwsEventHandler handler = nullptr) : _url(url), _cNextId(1), _eventHandler(handler), _enabled(true) {}
  ~AsyncWebSocket(){};
  const char *url() const {
    return _url.c_str();
  }
  void enable(bool e) {
    _enabled = e;
  }
  bool enabled() const {
    return _enabled;
  }
  bool availableForWriteAll();
  bool availableForWrite(uint32_t id);

  size_t count() const;
  AsyncWebSocketClient *client(uint32_t id);
  bool hasClient(uint32_t id) {
    return client(id) != nullptr;
  }

  void close(uint32_t id, uint16_t code = 0, const char *message = NULL);
  void closeAll(uint16_t code = 0, const char *message = NULL);
  void cleanupClients(uint16_t maxClients = DEFAULT_MAX_WS_CLIENTS);

  bool ping(uint32_t id, const uint8_t *data = NULL, size_t len = 0);
  SendStatus pingAll(const uint8_t *data = NULL, size_t len = 0);  //  done

  bool text(uint32_t id, const uint8_t *message, size_t len);
  bool text(uint32_t id, const char *message, size_t len);
  bool text(uint32_t id, const char *message);
  bool text(uint32_t id, const String &message);
  bool text(uint32_t id, AsyncWebSocketMessageBuffer *buffer);
  bool text(uint32_t id, AsyncWebSocketSharedBuffer buffer);

  SendStatus textAll(const uint8_t *message, size_t len);
  SendStatus textAll(const char *message, size_t len);
  SendStatus textAll(const char *message);
  SendStatus textAll(const String &message);
  SendStatus textAll(AsyncWebSocketMessageBuffer *buffer);
  SendStatus textAll(AsyncWebSocketSharedBuffer buffer);

  bool binary(uint32_t id, const uint8_t *message, size_t len);
  bool binary(uint32_t id, const char *message, size_t len);
  bool binary(uint32_t id, const char *message);
  bool binary(uint32_t id, const String &message);
  bool binary(uint32_t id, AsyncWebSocketMessageBuffer *buffer);
  bool binary(uint32_t id, AsyncWebSocketSharedBuffer buffer);

  SendStatus binaryAll(const uint8_t *message, size_t len);
  SendStatus binaryAll(const char *message, size_t len);
  SendStatus binaryAll(const char *message);
  SendStatus binaryAll(const String &message);
  SendStatus binaryAll(AsyncWebSocketMessageBuffer *buffer);
  SendStatus binaryAll(AsyncWebSocketSharedBuffer buffer);

  size_t printf(uint32_t id, const char *format, ...) __attribute__((format(printf, 3, 4)));
  size_t printfAll(const char *format, ...) __attribute__((format(printf, 2, 3)));

#ifdef ESP8266
  bool text(uint32_t id, const __FlashStringHelper *message);
  SendStatus textAll(const __FlashStringHelper *message);
  bool binary(uint32_t id, const __FlashStringHelper *message, size_t len);
  SendStatus binaryAll(const __FlashStringHelper *message, size_t len);
  size_t printf_P(uint32_t id, PGM_P formatP, ...) __attribute__((format(printf, 3, 4)));
  size_t printfAll_P(PGM_P formatP, ...) __attribute__((format(printf, 2, 3)));
#endif

  void onEvent(AwsEventHandler handler) {
    _eventHandler = handler;
  }
  void handleHandshake(AwsHandshakeHandler handler) {
    _handshakeHandler = handler;
  }

  // system callbacks (do not call)
  uint32_t _getNextId() {
    return _cNextId++;
  }
  AsyncWebSocketClient *_newClient(AsyncWebServerRequest *request);
  void _handleDisconnect(AsyncWebSocketClient *client);
  void _handleEvent(AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;

  //  messagebuffer functions/objects.
  AsyncWebSocketMessageBuffer *makeBuffer(size_t size = 0);
  AsyncWebSocketMessageBuffer *makeBuffer(const uint8_t *data, size_t size);

  std::list<AsyncWebSocketClient> &getClients() {
    return _clients;
  }
};

// WebServer response to authenticate the socket and detach the tcp client from the web server request
class AsyncWebSocketResponse : public AsyncWebServerResponse {
private:
  String _content;
  AsyncWebSocket *_server;

public:
  AsyncWebSocketResponse(const String &key, AsyncWebSocket *server);
  void _respond(AsyncWebServerRequest *request);
  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time);
  bool _sourceValid() const {
    return true;
  }
};

class AsyncWebSocketMessageHandler {
public:
  AwsEventHandler eventHandler() const {
    return _handler;
  }

  void onConnect(std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client)> onConnect) {
    _onConnect = onConnect;
  }

  void onDisconnect(std::function<void(AsyncWebSocket *server, uint32_t clientId)> onDisconnect) {
    _onDisconnect = onDisconnect;
  }

  /**
   * Error callback
   * @param reason null-terminated string
   * @param len length of the string
   */
  void onError(std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, uint16_t errorCode, const char *reason, size_t len)> onError) {
    _onError = onError;
  }

  /**
   * Complete message callback
   * @param data pointer to the data (binary or null-terminated string). This handler expects the user to know which data type he uses.
   */
  void onMessage(std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, const uint8_t *data, size_t len)> onMessage) {
    _onMessage = onMessage;
  }

  /**
   * Fragmented message callback
   * @param data pointer to the data (binary or null-terminated string), will be null-terminated. This handler expects the user to know which data type he uses.
   */
  // clang-format off
  void onFragment(std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, const AwsFrameInfo *frameInfo, const uint8_t *data, size_t len)> onFragment) {
    _onFragment = onFragment;
  }
  // clang-format on

private:
  // clang-format off
  std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client)> _onConnect;
  std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, uint16_t errorCode, const char *reason, size_t len)> _onError;
  std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, const uint8_t *data, size_t len)> _onMessage;
  std::function<void(AsyncWebSocket *server, AsyncWebSocketClient *client, const AwsFrameInfo *frameInfo, const uint8_t *data, size_t len)> _onFragment;
  std::function<void(AsyncWebSocket *server, uint32_t clientId)> _onDisconnect;
  // clang-format on

  // this handler is meant to only support 1-frame messages (== unfragmented messages)
  AwsEventHandler _handler = [this](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
      if (_onConnect) {
        _onConnect(server, client);
      }
    } else if (type == WS_EVT_DISCONNECT) {
      if (_onDisconnect) {
        _onDisconnect(server, client->id());
      }
    } else if (type == WS_EVT_ERROR) {
      if (_onError) {
        _onError(server, client, *((uint16_t *)arg), (const char *)data, len);
      }
    } else if (type == WS_EVT_DATA) {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (info->opcode == WS_TEXT) {
        data[len] = 0;
      }
      if (info->final && info->index == 0 && info->len == len) {
        if (_onMessage) {
          _onMessage(server, client, data, len);
        }
      } else {
        if (_onFragment) {
          _onFragment(server, client, info, data, len);
        }
      }
    }
  };
};

#endif /* ASYNCWEBSOCKET_H_ */

//#include "WebHandlerImpl.h"
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef ASYNCWEBSERVERHANDLERIMPL_H_
#define ASYNCWEBSERVERHANDLERIMPL_H_

#include <string>
#ifdef ASYNCWEBSERVER_REGEX
#include <regex>
#endif

#include "stddef.h"
#include <time.h>

class AsyncStaticWebHandler : public AsyncWebHandler {
  using File = fs::File;
  using FS = fs::FS;

private:
  bool _getFile(AsyncWebServerRequest *request) const;
  bool _searchFile(AsyncWebServerRequest *request, const String &path);

protected:
  FS _fs;
  String _uri;
  String _path;
  String _default_file;
  String _cache_control;
  String _last_modified;
  AwsTemplateProcessor _callback;
  bool _isDir;
  bool _tryGzipFirst = true;

public:
  AsyncStaticWebHandler(const char *uri, FS &fs, const char *path, const char *cache_control);
  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
  AsyncStaticWebHandler &setTryGzipFirst(bool value);
  AsyncStaticWebHandler &setIsDir(bool isDir);
  AsyncStaticWebHandler &setDefaultFile(const char *filename);
  AsyncStaticWebHandler &setCacheControl(const char *cache_control);

  /**
     * @brief Set the Last-Modified time for the object
     *
     * @param last_modified
     * @return AsyncStaticWebHandler&
     */
  AsyncStaticWebHandler &setLastModified(const char *last_modified);
  AsyncStaticWebHandler &setLastModified(struct tm *last_modified);
  AsyncStaticWebHandler &setLastModified(time_t last_modified);
  // sets to current time. Make sure sntp is running and time is updated
  AsyncStaticWebHandler &setLastModified();

  AsyncStaticWebHandler &setTemplateProcessor(AwsTemplateProcessor newCallback);
};

class AsyncCallbackWebHandler : public AsyncWebHandler {
private:
protected:
  String _uri;
  WebRequestMethodComposite _method;
  ArRequestHandlerFunction _onRequest;
  ArUploadHandlerFunction _onUpload;
  ArBodyHandlerFunction _onBody;
  bool _isRegex;

public:
  AsyncCallbackWebHandler() : _uri(), _method(HTTP_ANY), _onRequest(NULL), _onUpload(NULL), _onBody(NULL), _isRegex(false) {}
  void setUri(const String &uri);
  void setMethod(WebRequestMethodComposite method) {
    _method = method;
  }
  void onRequest(ArRequestHandlerFunction fn) {
    _onRequest = fn;
  }
  void onUpload(ArUploadHandlerFunction fn) {
    _onUpload = fn;
  }
  void onBody(ArBodyHandlerFunction fn) {
    _onBody = fn;
  }

  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
  void handleUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) override final;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override final;
  bool isRequestHandlerTrivial() const override final {
    return !_onRequest;
  }
};

#endif /* ASYNCWEBSERVERHANDLERIMPL_H_ */


//#include "WebResponseImpl.h"
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef ASYNCWEBSERVERRESPONSEIMPL_H_
#define ASYNCWEBSERVERRESPONSEIMPL_H_

#ifdef Arduino_h
// arduino is not compatible with std::vector
#undef min
#undef max
#endif
#include "literals.h"
#include <cbuf.h>
#include <memory>
#include <vector>

// It is possible to restore these defines, but one can use _min and _max instead. Or std::min, std::max.

class AsyncBasicResponse : public AsyncWebServerResponse {
private:
  String _content;

public:
  explicit AsyncBasicResponse(int code, const char *contentType = asyncsrv::empty, const char *content = asyncsrv::empty);
  AsyncBasicResponse(int code, const String &contentType, const String &content = emptyString)
    : AsyncBasicResponse(code, contentType.c_str(), content.c_str()) {}
  void _respond(AsyncWebServerRequest *request) override final;
  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) override final;
  bool _sourceValid() const override final {
    return true;
  }
};

class AsyncAbstractResponse : public AsyncWebServerResponse {
private:
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
  // amount of response data in-flight, i.e. sent, but not acked yet
  size_t _in_flight{0};
  // in-flight queue credits
  size_t _in_flight_credit{2};
#endif
  String _head;
  // Data is inserted into cache at begin().
  // This is inefficient with vector, but if we use some other container,
  // we won't be able to access it as contiguous array of bytes when reading from it,
  // so by gaining performance in one place, we'll lose it in another.
  std::vector<uint8_t> _cache;
  size_t _readDataFromCacheOrContent(uint8_t *data, const size_t len);
  size_t _fillBufferAndProcessTemplates(uint8_t *buf, size_t maxLen);

protected:
  AwsTemplateProcessor _callback;

public:
  AsyncAbstractResponse(AwsTemplateProcessor callback = nullptr);
  virtual ~AsyncAbstractResponse() {}
  void _respond(AsyncWebServerRequest *request) override final;
  size_t _ack(AsyncWebServerRequest *request, size_t len, uint32_t time) override final;
  virtual bool _sourceValid() const {
    return false;
  }
  virtual size_t _fillBuffer(uint8_t *buf __attribute__((unused)), size_t maxLen __attribute__((unused))) {
    return 0;
  }
};

#ifndef TEMPLATE_PLACEHOLDER
#define TEMPLATE_PLACEHOLDER '%'
#endif

#define TEMPLATE_PARAM_NAME_LENGTH 32
class AsyncFileResponse : public AsyncAbstractResponse {
  using File = fs::File;
  using FS = fs::FS;

private:
  File _content;
  String _path;
  void _setContentTypeFromPath(const String &path);

public:
  AsyncFileResponse(FS &fs, const String &path, const char *contentType = asyncsrv::empty, bool download = false, AwsTemplateProcessor callback = nullptr);
  AsyncFileResponse(FS &fs, const String &path, const String &contentType, bool download = false, AwsTemplateProcessor callback = nullptr)
    : AsyncFileResponse(fs, path, contentType.c_str(), download, callback) {}
  AsyncFileResponse(
    File content, const String &path, const char *contentType = asyncsrv::empty, bool download = false, AwsTemplateProcessor callback = nullptr
  );
  AsyncFileResponse(File content, const String &path, const String &contentType, bool download = false, AwsTemplateProcessor callback = nullptr)
    : AsyncFileResponse(content, path, contentType.c_str(), download, callback) {}
  ~AsyncFileResponse() {
    _content.close();
  }
  bool _sourceValid() const override final {
    return !!(_content);
  }
  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override final;
};

class AsyncStreamResponse : public AsyncAbstractResponse {
private:
  Stream *_content;

public:
  AsyncStreamResponse(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback = nullptr);
  AsyncStreamResponse(Stream &stream, const String &contentType, size_t len, AwsTemplateProcessor callback = nullptr)
    : AsyncStreamResponse(stream, contentType.c_str(), len, callback) {}
  bool _sourceValid() const override final {
    return !!(_content);
  }
  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override final;
};

class AsyncCallbackResponse : public AsyncAbstractResponse {
private:
  AwsResponseFiller _content;
  size_t _filledLength;

public:
  AsyncCallbackResponse(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr);
  AsyncCallbackResponse(const String &contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr)
    : AsyncCallbackResponse(contentType.c_str(), len, callback, templateCallback) {}
  bool _sourceValid() const override final {
    return !!(_content);
  }
  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override final;
};

class AsyncChunkedResponse : public AsyncAbstractResponse {
private:
  AwsResponseFiller _content;
  size_t _filledLength;

public:
  AsyncChunkedResponse(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr);
  AsyncChunkedResponse(const String &contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback = nullptr)
    : AsyncChunkedResponse(contentType.c_str(), callback, templateCallback) {}
  bool _sourceValid() const override final {
    return !!(_content);
  }
  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override final;
};

class AsyncProgmemResponse : public AsyncAbstractResponse {
private:
  const uint8_t *_content;
  size_t _readLength;

public:
  AsyncProgmemResponse(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr);
  AsyncProgmemResponse(int code, const String &contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback = nullptr)
    : AsyncProgmemResponse(code, contentType.c_str(), content, len, callback) {}
  bool _sourceValid() const override final {
    return true;
  }
  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override final;
};

class AsyncResponseStream : public AsyncAbstractResponse, public Print {
private:
  std::unique_ptr<cbuf> _content;

public:
  AsyncResponseStream(const char *contentType, size_t bufferSize);
  AsyncResponseStream(const String &contentType, size_t bufferSize) : AsyncResponseStream(contentType.c_str(), bufferSize) {}
  bool _sourceValid() const override final {
    return (_state < RESPONSE_END);
  }
  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override final;
  size_t write(const uint8_t *data, size_t len);
  size_t write(uint8_t data);
  /**
   * @brief Returns the number of bytes available in the stream.
   */
  size_t available() const {
    return _content->available();
  }
  using Print::write;
};

#endif /* ASYNCWEBSERVERRESPONSEIMPL_H_ */

#endif /* _AsyncWebServer_H_ */
