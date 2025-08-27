
// *** WebServer.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#include "ESPAsyncWebServer.h"
//#include "WebHandlerImpl.h"

#if defined(ESP32) || defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(LIBRETINY)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error Platform not supported
#endif

using namespace asyncsrv;

bool ON_STA_FILTER(AsyncWebServerRequest *request) {
#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED || LT_ARD_HAS_WIFI
  return WiFi.localIP() == request->client()->localIP();
#else
  return false;
#endif
}

bool ON_AP_FILTER(AsyncWebServerRequest *request) {
#if SOC_WIFI_SUPPORTED || CONFIG_ESP_WIFI_REMOTE_ENABLED || LT_ARD_HAS_WIFI
  return WiFi.localIP() != request->client()->localIP();
#else
  return false;
#endif
}

#ifndef HAVE_FS_FILE_OPEN_MODE
const char *fs::FileOpenMode::read = "r";
const char *fs::FileOpenMode::write = "w";
const char *fs::FileOpenMode::append = "a";
#endif

AsyncWebServer::AsyncWebServer(uint16_t port) : _server(port) {
  _catchAllHandler = new AsyncCallbackWebHandler();
  _server.onClient(
    [](void *s, AsyncClient *c) {
      if (c == NULL) {
        return;
      }
      c->setRxTimeout(3);
      AsyncWebServerRequest *r = new AsyncWebServerRequest((AsyncWebServer *)s, c);
      if (r == NULL) {
        c->abort();
        delete c;
      }
    },
    this
  );
}

AsyncWebServer::~AsyncWebServer() {
  reset();
  end();
  delete _catchAllHandler;
  _catchAllHandler = nullptr;  // Prevent potential use-after-free
}

AsyncWebRewrite &AsyncWebServer::addRewrite(std::shared_ptr<AsyncWebRewrite> rewrite) {
  _rewrites.emplace_back(rewrite);
  return *_rewrites.back().get();
}

AsyncWebRewrite &AsyncWebServer::addRewrite(AsyncWebRewrite *rewrite) {
  _rewrites.emplace_back(rewrite);
  return *_rewrites.back().get();
}

bool AsyncWebServer::removeRewrite(AsyncWebRewrite *rewrite) {
  return removeRewrite(rewrite->from().c_str(), rewrite->toUrl().c_str());
}

bool AsyncWebServer::removeRewrite(const char *from, const char *to) {
  for (auto r = _rewrites.begin(); r != _rewrites.end(); ++r) {
    if (r->get()->from() == from && r->get()->toUrl() == to) {
      _rewrites.erase(r);
      return true;
    }
  }
  return false;
}

AsyncWebRewrite &AsyncWebServer::rewrite(const char *from, const char *to) {
  _rewrites.emplace_back(std::make_shared<AsyncWebRewrite>(from, to));
  return *_rewrites.back().get();
}

AsyncWebHandler &AsyncWebServer::addHandler(AsyncWebHandler *handler) {
  _handlers.emplace_back(handler);
  return *(_handlers.back().get());
}

bool AsyncWebServer::removeHandler(AsyncWebHandler *handler) {
  for (auto i = _handlers.begin(); i != _handlers.end(); ++i) {
    if (i->get() == handler) {
      _handlers.erase(i);
      return true;
    }
  }
  return false;
}

void AsyncWebServer::begin() {
  _server.setNoDelay(true);
  _server.begin();
}

void AsyncWebServer::end() {
  _server.end();
}

#if ASYNC_TCP_SSL_ENABLED
void AsyncWebServer::onSslFileRequest(AcSSlFileHandler cb, void *arg) {
  _server.onSslFileRequest(cb, arg);
}

void AsyncWebServer::beginSecure(const char *cert, const char *key, const char *password) {
  _server.beginSecure(cert, key, password);
}
#endif

void AsyncWebServer::_handleDisconnect(AsyncWebServerRequest *request) {
  delete request;
}

void AsyncWebServer::_rewriteRequest(AsyncWebServerRequest *request) {
  // the last rewrite that matches the request will be used
  // we do not break the loop to allow for multiple rewrites to be applied and only the last one to be used (allows overriding)
  for (const auto &r : _rewrites) {
    if (r->match(request)) {
      request->_url = r->toUrl();
      request->_addGetParams(r->params());
    }
  }
}

void AsyncWebServer::_attachHandler(AsyncWebServerRequest *request) {
  for (auto &h : _handlers) {
    if (h->filter(request) && h->canHandle(request)) {
      request->setHandler(h.get());
      return;
    }
  }
  // ESP_LOGD("AsyncWebServer", "No handler found for %s, using _catchAllHandler pointer: %p", request->url().c_str(), _catchAllHandler);
  request->setHandler(_catchAllHandler);
}

AsyncCallbackWebHandler &AsyncWebServer::on(
  const char *uri, WebRequestMethodComposite method, ArRequestHandlerFunction onRequest, ArUploadHandlerFunction onUpload, ArBodyHandlerFunction onBody
) {
  AsyncCallbackWebHandler *handler = new AsyncCallbackWebHandler();
  handler->setUri(uri);
  handler->setMethod(method);
  handler->onRequest(onRequest);
  handler->onUpload(onUpload);
  handler->onBody(onBody);
  addHandler(handler);
  return *handler;
}

AsyncStaticWebHandler &AsyncWebServer::serveStatic(const char *uri, fs::FS &fs, const char *path, const char *cache_control) {
  AsyncStaticWebHandler *handler = new AsyncStaticWebHandler(uri, fs, path, cache_control);
  addHandler(handler);
  return *handler;
}

void AsyncWebServer::onNotFound(ArRequestHandlerFunction fn) {
  _catchAllHandler->onRequest(fn);
}

void AsyncWebServer::onFileUpload(ArUploadHandlerFunction fn) {
  _catchAllHandler->onUpload(fn);
}

void AsyncWebServer::onRequestBody(ArBodyHandlerFunction fn) {
  _catchAllHandler->onBody(fn);
}

AsyncWebHandler &AsyncWebServer::catchAllHandler() const {
  return *_catchAllHandler;
}

void AsyncWebServer::reset() {
  _rewrites.clear();
  _handlers.clear();

  _catchAllHandler->onRequest(NULL);
  _catchAllHandler->onUpload(NULL);
  _catchAllHandler->onBody(NULL);
}

// *** WebResponses.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "ESPAsyncWebServer.h"
//#include "WebResponseImpl.h"

using namespace asyncsrv;

// Since ESP8266 does not link memchr by default, here's its implementation.
void *memchr(void *ptr, int ch, size_t count) {
  unsigned char *p = static_cast<unsigned char *>(ptr);
  while (count--) {
    if (*p++ == static_cast<unsigned char>(ch)) {
      return --p;
    }
  }
  return nullptr;
}

/*
 * Abstract Response
 *
 */

const char *AsyncWebServerResponse::responseCodeToString(int code) {
  switch (code) {
    case 100: return T_HTTP_CODE_100;
    case 101: return T_HTTP_CODE_101;
    case 200: return T_HTTP_CODE_200;
    case 201: return T_HTTP_CODE_201;
    case 202: return T_HTTP_CODE_202;
    case 203: return T_HTTP_CODE_203;
    case 204: return T_HTTP_CODE_204;
    case 205: return T_HTTP_CODE_205;
    case 206: return T_HTTP_CODE_206;
    case 300: return T_HTTP_CODE_300;
    case 301: return T_HTTP_CODE_301;
    case 302: return T_HTTP_CODE_302;
    case 303: return T_HTTP_CODE_303;
    case 304: return T_HTTP_CODE_304;
    case 305: return T_HTTP_CODE_305;
    case 307: return T_HTTP_CODE_307;
    case 400: return T_HTTP_CODE_400;
    case 401: return T_HTTP_CODE_401;
    case 402: return T_HTTP_CODE_402;
    case 403: return T_HTTP_CODE_403;
    case 404: return T_HTTP_CODE_404;
    case 405: return T_HTTP_CODE_405;
    case 406: return T_HTTP_CODE_406;
    case 407: return T_HTTP_CODE_407;
    case 408: return T_HTTP_CODE_408;
    case 409: return T_HTTP_CODE_409;
    case 410: return T_HTTP_CODE_410;
    case 411: return T_HTTP_CODE_411;
    case 412: return T_HTTP_CODE_412;
    case 413: return T_HTTP_CODE_413;
    case 414: return T_HTTP_CODE_414;
    case 415: return T_HTTP_CODE_415;
    case 416: return T_HTTP_CODE_416;
    case 417: return T_HTTP_CODE_417;
    case 429: return T_HTTP_CODE_429;
    case 500: return T_HTTP_CODE_500;
    case 501: return T_HTTP_CODE_501;
    case 502: return T_HTTP_CODE_502;
    case 503: return T_HTTP_CODE_503;
    case 504: return T_HTTP_CODE_504;
    case 505: return T_HTTP_CODE_505;
    default:  return T_HTTP_CODE_ANY;
  }
}

AsyncWebServerResponse::AsyncWebServerResponse()
  : _code(0), _contentType(), _contentLength(0), _sendContentLength(true), _chunked(false), _headLength(0), _sentLength(0), _ackedLength(0), _writtenLength(0),
    _state(RESPONSE_SETUP) {
  for (const auto &header : DefaultHeaders::Instance()) {
    _headers.emplace_back(header);
  }
}

void AsyncWebServerResponse::setCode(int code) {
  if (_state == RESPONSE_SETUP) {
    _code = code;
  }
}

void AsyncWebServerResponse::setContentLength(size_t len) {
  if (_state == RESPONSE_SETUP && addHeader(T_Content_Length, len, true)) {
    _contentLength = len;
  }
}

void AsyncWebServerResponse::setContentType(const char *type) {
  if (_state == RESPONSE_SETUP && addHeader(T_Content_Type, type, true)) {
    _contentType = type;
  }
}

bool AsyncWebServerResponse::removeHeader(const char *name) {
  bool h_erased = false;
  for (auto i = _headers.begin(); i != _headers.end();) {
    if (i->name().equalsIgnoreCase(name)) {
      _headers.erase(i);
      h_erased = true;
    } else {
      ++i;
    }
  }
  return h_erased;
}

bool AsyncWebServerResponse::removeHeader(const char *name, const char *value) {
  for (auto i = _headers.begin(); i != _headers.end(); ++i) {
    if (i->name().equalsIgnoreCase(name) && i->value().equalsIgnoreCase(value)) {
      _headers.erase(i);
      return true;
    }
  }
  return false;
}

const AsyncWebHeader *AsyncWebServerResponse::getHeader(const char *name) const {
  auto iter = std::find_if(std::begin(_headers), std::end(_headers), [&name](const AsyncWebHeader &header) {
    return header.name().equalsIgnoreCase(name);
  });
  return (iter == std::end(_headers)) ? nullptr : &(*iter);
}

bool AsyncWebServerResponse::headerMustBePresentOnce(const String &name) {
  for (uint8_t i = 0; i < T_only_once_headers_len; i++) {
    if (name.equalsIgnoreCase(T_only_once_headers[i])) {
      return true;
    }
  }
  return false;
}

bool AsyncWebServerResponse::addHeader(AsyncWebHeader &&header, bool replaceExisting) {
  if (!header) {
    return false;  // invalid header
  }
  for (auto i = _headers.begin(); i != _headers.end(); ++i) {
    if (i->name().equalsIgnoreCase(header.name())) {
      // header already set
      if (replaceExisting) {
        // remove, break and add the new one
        _headers.erase(i);
        break;
      } else if (headerMustBePresentOnce(i->name())) {  // we can have only one header with that name
        // do not update
        return false;
      } else {
        break;  // accept multiple headers with the same name
      }
    }
  }
  // header was not found found, or existing one was removed
  _headers.emplace_back(std::move(header));
  return true;
}

bool AsyncWebServerResponse::addHeader(const char *name, const char *value, bool replaceExisting) {
  for (auto i = _headers.begin(); i != _headers.end(); ++i) {
    if (i->name().equalsIgnoreCase(name)) {
      // header already set
      if (replaceExisting) {
        // remove, break and add the new one
        _headers.erase(i);
        break;
      } else if (headerMustBePresentOnce(i->name())) {  // we can have only one header with that name
        // do not update
        return false;
      } else {
        break;  // accept multiple headers with the same name
      }
    }
  }
  // header was not found found, or existing one was removed
  _headers.emplace_back(name, value);
  return true;
}

void AsyncWebServerResponse::_assembleHead(String &buffer, uint8_t version) {
  if (version) {
    addHeader(T_Accept_Ranges, T_none, false);
    if (_chunked) {
      addHeader(T_Transfer_Encoding, T_chunked, false);
    }
  }

  if (_sendContentLength) {
    addHeader(T_Content_Length, String(_contentLength), false);
  }

  if (_contentType.length()) {
    addHeader(T_Content_Type, _contentType.c_str(), false);
  }

  // precompute buffer size to avoid reallocations by String class
  size_t len = 0;
  len += 50;  // HTTP/1.1 200 <reason>\r\n
  for (const auto &header : _headers) {
    len += header.name().length() + header.value().length() + 4;
  }

  // prepare buffer
  buffer.reserve(len);

  // HTTP header
#ifdef ESP8266
  buffer.concat(PSTR("HTTP/1."));
#else
  buffer.concat("HTTP/1.");
#endif
  buffer.concat(version);
  buffer.concat(' ');
  buffer.concat(_code);
  buffer.concat(' ');
  buffer.concat(responseCodeToString(_code));
  buffer.concat(T_rn);

  // Add headers
  for (const auto &header : _headers) {
    buffer.concat(header.name());
#ifdef ESP8266
    buffer.concat(PSTR(": "));
#else
    buffer.concat(": ");
#endif
    buffer.concat(header.value());
    buffer.concat(T_rn);
  }

  buffer.concat(T_rn);
  _headLength = buffer.length();
}

bool AsyncWebServerResponse::_started() const {
  return _state > RESPONSE_SETUP;
}
bool AsyncWebServerResponse::_finished() const {
  return _state > RESPONSE_WAIT_ACK;
}
bool AsyncWebServerResponse::_failed() const {
  return _state == RESPONSE_FAILED;
}
bool AsyncWebServerResponse::_sourceValid() const {
  return false;
}
void AsyncWebServerResponse::_respond(AsyncWebServerRequest *request) {
  _state = RESPONSE_END;
  request->client()->close();
}
size_t AsyncWebServerResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time) {
  (void)request;
  (void)len;
  (void)time;
  return 0;
}

/*
 * String/Code Response
 * */
AsyncBasicResponse::AsyncBasicResponse(int code, const char *contentType, const char *content) {
  _code = code;
  _content = content;
  _contentType = contentType;
  if (_content.length()) {
    _contentLength = _content.length();
    if (!_contentType.length()) {
      _contentType = T_text_plain;
    }
  }
  addHeader(T_Connection, T_close, false);
}

void AsyncBasicResponse::_respond(AsyncWebServerRequest *request) {
  _state = RESPONSE_HEADERS;
  String out;
  _assembleHead(out, request->version());
  size_t outLen = out.length();
  size_t space = request->client()->space();
  if (!_contentLength && space >= outLen) {
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_WAIT_ACK;
  } else if (_contentLength && space >= outLen + _contentLength) {
    out += _content;
    outLen += _contentLength;
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_WAIT_ACK;
  } else if (space && space < outLen) {
    String partial = out.substring(0, space);
    _content = out.substring(space) + _content;
    _contentLength += outLen - space;
    _writtenLength += request->client()->write(partial.c_str(), partial.length());
    _state = RESPONSE_CONTENT;
  } else if (space > outLen && space < (outLen + _contentLength)) {
    size_t shift = space - outLen;
    outLen += shift;
    _sentLength += shift;
    out += _content.substring(0, shift);
    _content = _content.substring(shift);
    _writtenLength += request->client()->write(out.c_str(), outLen);
    _state = RESPONSE_CONTENT;
  } else {
    _content = out + _content;
    _contentLength += outLen;
    _state = RESPONSE_CONTENT;
  }
}

size_t AsyncBasicResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time) {
  (void)time;
  _ackedLength += len;
  if (_state == RESPONSE_CONTENT) {
    size_t available = _contentLength - _sentLength;
    size_t space = request->client()->space();
    // we can fit in this packet
    if (space > available) {
      _writtenLength += request->client()->write(_content.c_str(), available);
      _content = emptyString;
      _state = RESPONSE_WAIT_ACK;
      return available;
    }
    // send some data, the rest on ack
    String out = _content.substring(0, space);
    _content = _content.substring(space);
    _sentLength += space;
    _writtenLength += request->client()->write(out.c_str(), space);
    return space;
  } else if (_state == RESPONSE_WAIT_ACK) {
    if (_ackedLength >= _writtenLength) {
      _state = RESPONSE_END;
    }
  }
  return 0;
}

/*
 * Abstract Response
 * */

AsyncAbstractResponse::AsyncAbstractResponse(AwsTemplateProcessor callback) : _callback(callback) {
  // In case of template processing, we're unable to determine real response size
  if (callback) {
    _contentLength = 0;
    _sendContentLength = false;
    _chunked = true;
  }
}

void AsyncAbstractResponse::_respond(AsyncWebServerRequest *request) {
  addHeader(T_Connection, T_close, false);
  _assembleHead(_head, request->version());
  _state = RESPONSE_HEADERS;
  _ack(request, 0, 0);
}

size_t AsyncAbstractResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time) {
  (void)time;
  if (!_sourceValid()) {
    _state = RESPONSE_FAILED;
    request->client()->close();
    return 0;
  }

#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
  // return a credit for each chunk of acked data (polls does not give any credits)
  if (len) {
    ++_in_flight_credit;
  }

  // for chunked responses ignore acks if there are no _in_flight_credits left
  if (_chunked && !_in_flight_credit) {
#ifdef ESP32
    log_d("(chunk) out of in-flight credits");
#endif
    return 0;
  }

  _in_flight -= (_in_flight > len) ? len : _in_flight;
  // get the size of available sock space
#endif

  _ackedLength += len;
  size_t space = request->client()->space();

  size_t headLen = _head.length();
  if (_state == RESPONSE_HEADERS) {
    if (space >= headLen) {
      _state = RESPONSE_CONTENT;
      space -= headLen;
    } else {
      String out = _head.substring(0, space);
      _head = _head.substring(space);
      _writtenLength += request->client()->write(out.c_str(), out.length());
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
      _in_flight += out.length();
      --_in_flight_credit;  // take a credit
#endif
      return out.length();
    }
  }

  if (_state == RESPONSE_CONTENT) {
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
    // for response data we need to control the queue and in-flight fragmentation. Sending small chunks could give low latency,
    // but flood asynctcp's queue and fragment socket buffer space for large responses.
    // Let's ignore polled acks and acks in case when we have more in-flight data then the available socket buff space.
    // That way we could balance on having half the buffer in-flight while another half is filling up, while minimizing events in asynctcp q
    if (_in_flight > space) {
      // log_d("defer user call %u/%u", _in_flight, space);
      //  take the credit back since we are ignoring this ack and rely on other inflight data
      if (len) {
        --_in_flight_credit;
      }
      return 0;
    }
#endif

    size_t outLen;
    if (_chunked) {
      if (space <= 8) {
        return 0;
      }

      outLen = space;
    } else if (!_sendContentLength) {
      outLen = space;
    } else {
      outLen = ((_contentLength - _sentLength) > space) ? space : (_contentLength - _sentLength);
    }

    uint8_t *buf = (uint8_t *)malloc(outLen + headLen);
    if (!buf) {
#ifdef ESP32
      log_e("Failed to allocate");
#endif
      request->abort();
      return 0;
    }

    if (headLen) {
      memcpy(buf, _head.c_str(), _head.length());
    }

    size_t readLen = 0;

    if (_chunked) {
      // HTTP 1.1 allows leading zeros in chunk length. Or spaces may be added.
      // See RFC2616 sections 2, 3.6.1.
      readLen = _fillBufferAndProcessTemplates(buf + headLen + 6, outLen - 8);
      if (readLen == RESPONSE_TRY_AGAIN) {
        free(buf);
        return 0;
      }
      outLen = sprintf((char *)buf + headLen, "%04x", readLen) + headLen;
      buf[outLen++] = '\r';
      buf[outLen++] = '\n';
      outLen += readLen;
      buf[outLen++] = '\r';
      buf[outLen++] = '\n';
    } else {
      readLen = _fillBufferAndProcessTemplates(buf + headLen, outLen);
      if (readLen == RESPONSE_TRY_AGAIN) {
        free(buf);
        return 0;
      }
      outLen = readLen + headLen;
    }

    if (headLen) {
      _head = emptyString;
    }

    if (outLen) {
      _writtenLength += request->client()->write((const char *)buf, outLen);
#if ASYNCWEBSERVER_USE_CHUNK_INFLIGHT
      _in_flight += outLen;
      --_in_flight_credit;  // take a credit
#endif
    }

    if (_chunked) {
      _sentLength += readLen;
    } else {
      _sentLength += outLen - headLen;
    }

    free(buf);

    if ((_chunked && readLen == 0) || (!_sendContentLength && outLen == 0) || (!_chunked && _sentLength == _contentLength)) {
      _state = RESPONSE_WAIT_ACK;
    }
    return outLen;

  } else if (_state == RESPONSE_WAIT_ACK) {
    if (!_sendContentLength || _ackedLength >= _writtenLength) {
      _state = RESPONSE_END;
      if (!_chunked && !_sendContentLength) {
        request->client()->close(true);
      }
    }
  }
  return 0;
}

size_t AsyncAbstractResponse::_readDataFromCacheOrContent(uint8_t *data, const size_t len) {
  // If we have something in cache, copy it to buffer
  const size_t readFromCache = std::min(len, _cache.size());
  if (readFromCache) {
    memcpy(data, _cache.data(), readFromCache);
    _cache.erase(_cache.begin(), _cache.begin() + readFromCache);
  }
  // If we need to read more...
  const size_t needFromFile = len - readFromCache;
  const size_t readFromContent = _fillBuffer(data + readFromCache, needFromFile);
  return readFromCache + readFromContent;
}

size_t AsyncAbstractResponse::_fillBufferAndProcessTemplates(uint8_t *data, size_t len) {
  if (!_callback) {
    return _fillBuffer(data, len);
  }

  const size_t originalLen = len;
  len = _readDataFromCacheOrContent(data, len);
  // Now we've read 'len' bytes, either from cache or from file
  // Search for template placeholders
  uint8_t *pTemplateStart = data;
  while ((pTemplateStart < &data[len]) && (pTemplateStart = (uint8_t *)memchr(pTemplateStart, TEMPLATE_PLACEHOLDER, &data[len - 1] - pTemplateStart + 1))
  ) {  // data[0] ... data[len - 1]
    uint8_t *pTemplateEnd =
      (pTemplateStart < &data[len - 1]) ? (uint8_t *)memchr(pTemplateStart + 1, TEMPLATE_PLACEHOLDER, &data[len - 1] - pTemplateStart) : nullptr;
    // temporary buffer to hold parameter name
    uint8_t buf[TEMPLATE_PARAM_NAME_LENGTH + 1];
    String paramName;
    // If closing placeholder is found:
    if (pTemplateEnd) {
      // prepare argument to callback
      const size_t paramNameLength = std::min((size_t)sizeof(buf) - 1, (size_t)(pTemplateEnd - pTemplateStart - 1));
      if (paramNameLength) {
        memcpy(buf, pTemplateStart + 1, paramNameLength);
        buf[paramNameLength] = 0;
        paramName = String(reinterpret_cast<char *>(buf));
      } else {  // double percent sign encountered, this is single percent sign escaped.
        // remove the 2nd percent sign
        memmove(pTemplateEnd, pTemplateEnd + 1, &data[len] - pTemplateEnd - 1);
        len += _readDataFromCacheOrContent(&data[len - 1], 1) - 1;
        ++pTemplateStart;
      }
    } else if (&data[len - 1] - pTemplateStart + 1
               < TEMPLATE_PARAM_NAME_LENGTH + 2) {  // closing placeholder not found, check if it's in the remaining file data
      memcpy(buf, pTemplateStart + 1, &data[len - 1] - pTemplateStart);
      const size_t readFromCacheOrContent =
        _readDataFromCacheOrContent(buf + (&data[len - 1] - pTemplateStart), TEMPLATE_PARAM_NAME_LENGTH + 2 - (&data[len - 1] - pTemplateStart + 1));
      if (readFromCacheOrContent) {
        pTemplateEnd = (uint8_t *)memchr(buf + (&data[len - 1] - pTemplateStart), TEMPLATE_PLACEHOLDER, readFromCacheOrContent);
        if (pTemplateEnd) {
          // prepare argument to callback
          *pTemplateEnd = 0;
          paramName = String(reinterpret_cast<char *>(buf));
          // Copy remaining read-ahead data into cache
          _cache.insert(_cache.begin(), pTemplateEnd + 1, buf + (&data[len - 1] - pTemplateStart) + readFromCacheOrContent);
          pTemplateEnd = &data[len - 1];
        } else  // closing placeholder not found in file data, store found percent symbol as is and advance to the next position
        {
          // but first, store read file data in cache
          _cache.insert(_cache.begin(), buf + (&data[len - 1] - pTemplateStart), buf + (&data[len - 1] - pTemplateStart) + readFromCacheOrContent);
          ++pTemplateStart;
        }
      } else {  // closing placeholder not found in content data, store found percent symbol as is and advance to the next position
        ++pTemplateStart;
      }
    } else {  // closing placeholder not found in content data, store found percent symbol as is and advance to the next position
      ++pTemplateStart;
    }
    if (paramName.length()) {
      // call callback and replace with result.
      // Everything in range [pTemplateStart, pTemplateEnd] can be safely replaced with parameter value.
      // Data after pTemplateEnd may need to be moved.
      // The first byte of data after placeholder is located at pTemplateEnd + 1.
      // It should be located at pTemplateStart + numBytesCopied (to begin right after inserted parameter value).
      const String paramValue(_callback(paramName));
      const char *pvstr = paramValue.c_str();
      const unsigned int pvlen = paramValue.length();
      const size_t numBytesCopied = std::min(pvlen, static_cast<unsigned int>(&data[originalLen - 1] - pTemplateStart + 1));
      // make room for param value
      // 1. move extra data to cache if parameter value is longer than placeholder AND if there is no room to store
      if ((pTemplateEnd + 1 < pTemplateStart + numBytesCopied) && (originalLen - (pTemplateStart + numBytesCopied - pTemplateEnd - 1) < len)) {
        _cache.insert(_cache.begin(), &data[originalLen - (pTemplateStart + numBytesCopied - pTemplateEnd - 1)], &data[len]);
        // 2. parameter value is longer than placeholder text, push the data after placeholder which not saved into cache further to the end
        memmove(pTemplateStart + numBytesCopied, pTemplateEnd + 1, &data[originalLen] - pTemplateStart - numBytesCopied);
        len = originalLen;  // fix issue with truncated data, not sure if it has any side effects
      } else if (pTemplateEnd + 1 != pTemplateStart + numBytesCopied) {
        // 2. Either parameter value is shorter than placeholder text OR there is enough free space in buffer to fit.
        //    Move the entire data after the placeholder
        memmove(pTemplateStart + numBytesCopied, pTemplateEnd + 1, &data[len] - pTemplateEnd - 1);
      }
      // 3. replace placeholder with actual value
      memcpy(pTemplateStart, pvstr, numBytesCopied);
      // If result is longer than buffer, copy the remainder into cache (this could happen only if placeholder text itself did not fit entirely in buffer)
      if (numBytesCopied < pvlen) {
        _cache.insert(_cache.begin(), pvstr + numBytesCopied, pvstr + pvlen);
      } else if (pTemplateStart + numBytesCopied < pTemplateEnd + 1) {  // result is copied fully; if result is shorter than placeholder text...
        // there is some free room, fill it from cache
        const size_t roomFreed = pTemplateEnd + 1 - pTemplateStart - numBytesCopied;
        const size_t totalFreeRoom = originalLen - len + roomFreed;
        len += _readDataFromCacheOrContent(&data[len - roomFreed], totalFreeRoom) - roomFreed;
      } else {  // result is copied fully; it is longer than placeholder text
        const size_t roomTaken = pTemplateStart + numBytesCopied - pTemplateEnd - 1;
        len = std::min(len + roomTaken, originalLen);
      }
    }
  }  // while(pTemplateStart)
  return len;
}

/*
 * File Response
 * */

/**
 * @brief Sets the content type based on the file path extension
 *
 * This method determines the appropriate MIME content type for a file based on its
 * file extension. It supports both external content type functions (if available)
 * and an internal mapping of common file extensions to their corresponding MIME types.
 *
 * @param path The file path string from which to extract the extension
 * @note The method modifies the internal _contentType member variable
 */
void AsyncFileResponse::_setContentTypeFromPath(const String &path) {
#if HAVE_EXTERN_GET_Content_Type_FUNCTION
#ifndef ESP8266
  extern const char *getContentType(const String &path);
#else
  extern const __FlashStringHelper *getContentType(const String &path);
#endif
  _contentType = getContentType(path);
#else
  const char *cpath = path.c_str();
  const char *dot = strrchr(cpath, '.');

  if (!dot) {
    _contentType = T_text_plain;
    return;
  }

  if (strcmp(dot, T__html) == 0 || strcmp(dot, T__htm) == 0) {
    _contentType = T_text_html;
  } else if (strcmp(dot, T__css) == 0) {
    _contentType = T_text_css;
  } else if (strcmp(dot, T__js) == 0) {
    _contentType = T_application_javascript;
  } else if (strcmp(dot, T__json) == 0) {
    _contentType = T_application_json;
  } else if (strcmp(dot, T__png) == 0) {
    _contentType = T_image_png;
  } else if (strcmp(dot, T__ico) == 0) {
    _contentType = T_image_x_icon;
  } else if (strcmp(dot, T__svg) == 0) {
    _contentType = T_image_svg_xml;
  } else if (strcmp(dot, T__jpg) == 0) {
    _contentType = T_image_jpeg;
  } else if (strcmp(dot, T__gif) == 0) {
    _contentType = T_image_gif;
  } else if (strcmp(dot, T__woff2) == 0) {
    _contentType = T_font_woff2;
  } else if (strcmp(dot, T__woff) == 0) {
    _contentType = T_font_woff;
  } else if (strcmp(dot, T__ttf) == 0) {
    _contentType = T_font_ttf;
  } else if (strcmp(dot, T__eot) == 0) {
    _contentType = T_font_eot;
  } else if (strcmp(dot, T__xml) == 0) {
    _contentType = T_text_xml;
  } else if (strcmp(dot, T__pdf) == 0) {
    _contentType = T_application_pdf;
  } else if (strcmp(dot, T__zip) == 0) {
    _contentType = T_application_zip;
  } else if (strcmp(dot, T__gz) == 0) {
    _contentType = T_application_x_gzip;
  } else {
    _contentType = T_text_plain;
  }
#endif
}

/**
 * @brief Constructor for AsyncFileResponse that handles file serving with compression support
 *
 * This constructor creates an AsyncFileResponse object that can serve files from a filesystem,
 * with automatic fallback to gzip-compressed versions if the original file is not found.
 * It also handles ETag generation for caching and supports both inline and download modes.
 *
 * @param fs Reference to the filesystem object used to open files
 * @param path Path to the file to be served (without compression extension)
 * @param contentType MIME type of the file content (empty string for auto-detection)
 * @param download If true, file will be served as download attachment; if false, as inline content
 * @param callback Template processor callback for dynamic content processing
 */
AsyncFileResponse::AsyncFileResponse(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback)
  : AsyncAbstractResponse(callback) {
  // Try to open the uncompressed version first
  _content = fs.open(path, fs::FileOpenMode::read);
  if (_content.available()) {
    _path = path;
    _contentLength = _content.size();
  } else {
    // Try to open the compressed version (.gz)
    _path = path + asyncsrv::T__gz;
    _content = fs.open(_path, fs::FileOpenMode::read);
    _contentLength = _content.size();

    if (_content.seek(_contentLength - 8)) {
      addHeader(T_Content_Encoding, T_gzip, false);
      _callback = nullptr;  // Unable to process zipped templates
      _sendContentLength = true;
      _chunked = false;

      // Add ETag and cache headers
      uint8_t crcInTrailer[4];
      _content.read(crcInTrailer, sizeof(crcInTrailer));
      char serverETag[9];
      AsyncWebServerRequest::_getEtag(crcInTrailer, serverETag);
      addHeader(T_ETag, serverETag, true);
      addHeader(T_Cache_Control, T_no_cache, true);

      _content.seek(0);
    } else {
      // File is corrupted or invalid
      _code = 404;
      return;
    }
  }

  if (*contentType != '\0') {
    _setContentTypeFromPath(path);
  } else {
    _contentType = contentType;
  }

  if (download) {
    // Extract filename from path and set as download attachment
    int filenameStart = path.lastIndexOf('/') + 1;
    char buf[26 + path.length() - filenameStart];
    char *filename = (char *)path.c_str() + filenameStart;
    snprintf_P(buf, sizeof(buf), PSTR("attachment; filename=\"%s\""), filename);
    addHeader(T_Content_Disposition, buf, false);
  } else {
    // Serve file inline (display in browser)
    addHeader(T_Content_Disposition, PSTR("inline"), false);
  }

  _code = 200;
}

AsyncFileResponse::AsyncFileResponse(File content, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback)
  : AsyncAbstractResponse(callback) {
  _code = 200;
  _path = path;

  if (!download && String(content.name()).endsWith(T__gz) && !path.endsWith(T__gz)) {
    addHeader(T_Content_Encoding, T_gzip, false);
    _callback = nullptr;  // Unable to process gzipped templates
    _sendContentLength = true;
    _chunked = false;
  }

  _content = content;
  _contentLength = _content.size();

  if (strlen(contentType) == 0) {
    _setContentTypeFromPath(path);
  } else {
    _contentType = contentType;
  }

  int filenameStart = path.lastIndexOf('/') + 1;
  char buf[26 + path.length() - filenameStart];
  char *filename = (char *)path.c_str() + filenameStart;

  if (download) {
    snprintf_P(buf, sizeof(buf), PSTR("attachment; filename=\"%s\""), filename);
  } else {
    snprintf_P(buf, sizeof(buf), PSTR("inline"));
  }
  addHeader(T_Content_Disposition, buf, false);
}

size_t AsyncFileResponse::_fillBuffer(uint8_t *data, size_t len) {
  return _content.read(data, len);
}

/*
 * Stream Response
 * */

AsyncStreamResponse::AsyncStreamResponse(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback) : AsyncAbstractResponse(callback) {
  _code = 200;
  _content = &stream;
  _contentLength = len;
  _contentType = contentType;
}

size_t AsyncStreamResponse::_fillBuffer(uint8_t *data, size_t len) {
  size_t available = _content->available();
  size_t outLen = (available > len) ? len : available;
  size_t i;
  for (i = 0; i < outLen; i++) {
    data[i] = _content->read();
  }
  return outLen;
}

/*
 * Callback Response
 * */

AsyncCallbackResponse::AsyncCallbackResponse(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback)
  : AsyncAbstractResponse(templateCallback) {
  _code = 200;
  _content = callback;
  _contentLength = len;
  if (!len) {
    _sendContentLength = false;
  }
  _contentType = contentType;
  _filledLength = 0;
}

size_t AsyncCallbackResponse::_fillBuffer(uint8_t *data, size_t len) {
  size_t ret = _content(data, len, _filledLength);
  if (ret != RESPONSE_TRY_AGAIN) {
    _filledLength += ret;
  }
  return ret;
}

/*
 * Chunked Response
 * */

AsyncChunkedResponse::AsyncChunkedResponse(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor processorCallback)
  : AsyncAbstractResponse(processorCallback) {
  _code = 200;
  _content = callback;
  _contentLength = 0;
  _contentType = contentType;
  _sendContentLength = false;
  _chunked = true;
  _filledLength = 0;
}

size_t AsyncChunkedResponse::_fillBuffer(uint8_t *data, size_t len) {
  size_t ret = _content(data, len, _filledLength);
  if (ret != RESPONSE_TRY_AGAIN) {
    _filledLength += ret;
  }
  return ret;
}

/*
 * Progmem Response
 * */

AsyncProgmemResponse::AsyncProgmemResponse(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback)
  : AsyncAbstractResponse(callback) {
  _code = code;
  _content = content;
  _contentType = contentType;
  _contentLength = len;
  _readLength = 0;
}

size_t AsyncProgmemResponse::_fillBuffer(uint8_t *data, size_t len) {
  size_t left = _contentLength - _readLength;
  if (left > len) {
    memcpy_P(data, _content + _readLength, len);
    _readLength += len;
    return len;
  }
  memcpy_P(data, _content + _readLength, left);
  _readLength += left;
  return left;
}

/*
 * Response Stream (You can print/write/printf to it, up to the contentLen bytes)
 * */

AsyncResponseStream::AsyncResponseStream(const char *contentType, size_t bufferSize) {
  _code = 200;
  _contentLength = 0;
  _contentType = contentType;
  // internal buffer will be null on allocation failure
  _content = std::unique_ptr<cbuf>(new cbuf(bufferSize));
  if (bufferSize && _content->size() < bufferSize) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
  }
}

size_t AsyncResponseStream::_fillBuffer(uint8_t *buf, size_t maxLen) {
  return _content->read((char *)buf, maxLen);
}

size_t AsyncResponseStream::write(const uint8_t *data, size_t len) {
  if (_started()) {
    return 0;
  }
  if (len > _content->room()) {
    size_t needed = len - _content->room();
    _content->resizeAdd(needed);
    // log a warning if allocation failed, but do not return: keep writing the bytes we can
    // with _content->write: if len is more than the available size in the buffer, only
    // the available size will be written
    if (len > _content->room()) {
#ifdef ESP32
      log_e("Failed to allocate");
#endif
    }
  }
  size_t written = _content->write((const char *)data, len);
  _contentLength += written;
  return written;
}

size_t AsyncResponseStream::write(uint8_t data) {
  return write(&data, 1);
}

// *** WebRequest.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "ESPAsyncWebServer.h"
//#include "WebAuthentication.h"

// *** WebAuthentication.h ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef WEB_AUTHENTICATION_H_
#define WEB_AUTHENTICATION_H_

#include "Arduino.h"

bool checkBasicAuthentication(const char *header, const char *username, const char *password);

bool checkDigestAuthentication(
  const char *header, const char *method, const char *username, const char *password, const char *realm, bool passwordIsHash, const char *nonce,
  const char *opaque, const char *uri
);

// for storing hashed versions on the device that can be authenticated against
String generateDigestHash(const char *username, const char *password, const char *realm);

String generateBasicHash(const char *username, const char *password);

String genRandomMD5();

#endif


//#include "WebResponseImpl.h"
//#include "literals.h"
//#include <cstring>

#define __is_param_char(c) ((c) && ((c) != '{') && ((c) != '[') && ((c) != '&') && ((c) != '='))

static void doNotDelete(AsyncWebServerRequest *) {}

using namespace asyncsrv;

enum {
  PARSE_REQ_START = 0,
  PARSE_REQ_HEADERS = 1,
  PARSE_REQ_BODY = 2,
  PARSE_REQ_END = 3,
  PARSE_REQ_FAIL = 4
};

AsyncWebServerRequest::AsyncWebServerRequest(AsyncWebServer *s, AsyncClient *c)
  : _client(c), _server(s), _handler(NULL), _response(NULL), _onDisconnectfn(NULL), _temp(), _parseState(PARSE_REQ_START), _version(0), _method(HTTP_ANY),
    _url(), _host(), _contentType(), _boundary(), _authorization(), _reqconntype(RCT_HTTP), _authMethod(AsyncAuthType::AUTH_NONE), _isMultipart(false),
    _isPlainPost(false), _expectingContinue(false), _contentLength(0), _parsedLength(0), _multiParseState(0), _boundaryPosition(0), _itemStartIndex(0),
    _itemSize(0), _itemName(), _itemFilename(), _itemType(), _itemValue(), _itemBuffer(0), _itemBufferIndex(0), _itemIsFile(false), _tempObject(NULL) {
  c->onError(
    [](void *r, AsyncClient *c, int8_t error) {
      (void)c;
      // log_e("AsyncWebServerRequest::_onError");
      AsyncWebServerRequest *req = (AsyncWebServerRequest *)r;
      req->_onError(error);
    },
    this
  );
  c->onAck(
    [](void *r, AsyncClient *c, size_t len, uint32_t time) {
      (void)c;
      // log_e("AsyncWebServerRequest::_onAck");
      AsyncWebServerRequest *req = (AsyncWebServerRequest *)r;
      req->_onAck(len, time);
    },
    this
  );
  c->onDisconnect(
    [](void *r, AsyncClient *c) {
      // log_e("AsyncWebServerRequest::_onDisconnect");
      AsyncWebServerRequest *req = (AsyncWebServerRequest *)r;
      req->_onDisconnect();
      delete c;
    },
    this
  );
  c->onTimeout(
    [](void *r, AsyncClient *c, uint32_t time) {
      (void)c;
      // log_e("AsyncWebServerRequest::_onTimeout");
      AsyncWebServerRequest *req = (AsyncWebServerRequest *)r;
      req->_onTimeout(time);
    },
    this
  );
  c->onData(
    [](void *r, AsyncClient *c, void *buf, size_t len) {
      (void)c;
      // log_e("AsyncWebServerRequest::_onData");
      AsyncWebServerRequest *req = (AsyncWebServerRequest *)r;
      req->_onData(buf, len);
    },
    this
  );
  c->onPoll(
    [](void *r, AsyncClient *c) {
      (void)c;
      // log_e("AsyncWebServerRequest::_onPoll");
      AsyncWebServerRequest *req = (AsyncWebServerRequest *)r;
      req->_onPoll();
    },
    this
  );
}

AsyncWebServerRequest::~AsyncWebServerRequest() {
  // log_e("AsyncWebServerRequest::~AsyncWebServerRequest");

  _this.reset();

  _headers.clear();

  _pathParams.clear();

  AsyncWebServerResponse *r = _response;
  _response = NULL;
  delete r;

  if (_tempObject != NULL) {
    free(_tempObject);
  }

  if (_tempFile) {
    _tempFile.close();
  }

  if (_itemBuffer) {
    free(_itemBuffer);
  }
}

void AsyncWebServerRequest::_onData(void *buf, size_t len) {
  // SSL/TLS handshake detection
#ifndef ASYNC_TCP_SSL_ENABLED
  if (_parseState == PARSE_REQ_START && len && ((uint8_t *)buf)[0] == 0x16) {  // 0x16 indicates a Handshake message (SSL/TLS).
#ifdef ESP32
    log_d("SSL/TLS handshake detected: resetting connection");
#endif
    _parseState = PARSE_REQ_FAIL;
    abort();
    return;
  }
#endif

  size_t i = 0;
  while (true) {

    if (_parseState < PARSE_REQ_BODY) {
      // Find new line in buf
      char *str = (char *)buf;
      for (i = 0; i < len; i++) {
        // Check for null characters in header
        if (!str[i]) {
          _parseState = PARSE_REQ_FAIL;
          abort();
          return;
        }
        if (str[i] == '\n') {
          break;
        }
      }
      if (i == len) {  // No new line, just add the buffer in _temp
        char ch = str[len - 1];
        str[len - 1] = 0;
        if (!_temp.reserve(_temp.length() + len)) {
#ifdef ESP32
          log_e("Failed to allocate");
#endif
          _parseState = PARSE_REQ_FAIL;
          abort();
          return;
        }
        _temp.concat(str);
        _temp.concat(ch);
      } else {       // Found new line - extract it and parse
        str[i] = 0;  // Terminate the string at the end of the line.
        _temp.concat(str);
        _temp.trim();
        _parseLine();
        if (++i < len) {
          // Still have more buffer to process
          buf = str + i;
          len -= i;
          continue;
        }
      }
    } else if (_parseState == PARSE_REQ_BODY) {
      // A handler should be already attached at this point in _parseLine function.
      // If handler does nothing (_onRequest is NULL), we don't need to really parse the body.
      const bool needParse = _handler && !_handler->isRequestHandlerTrivial();
      // Discard any bytes after content length; handlers may overrun their buffers
      len = std::min(len, _contentLength - _parsedLength);
      if (_isMultipart) {
        if (needParse) {
          size_t i;
          for (i = 0; i < len; i++) {
            _parseMultipartPostByte(((uint8_t *)buf)[i], i == len - 1);
            _parsedLength++;
          }
        } else {
          _parsedLength += len;
        }
      } else {
        if (_parsedLength == 0) {
          if (_contentType.startsWith(T_app_xform_urlencoded)) {
            _isPlainPost = true;
          } else if (_contentType == T_text_plain && __is_param_char(((char *)buf)[0])) {
            size_t i = 0;
            while (i < len && __is_param_char(((char *)buf)[i++]));
            if (i < len && ((char *)buf)[i - 1] == '=') {
              _isPlainPost = true;
            }
          }
        }
        if (!_isPlainPost) {
          // ESP_LOGD("AsyncWebServer", "_isPlainPost: %d, _handler: %p", _isPlainPost, _handler);
          if (_handler) {
            _handler->handleBody(this, (uint8_t *)buf, len, _parsedLength, _contentLength);
          }
          _parsedLength += len;
        } else if (needParse) {
          size_t i;
          for (i = 0; i < len; i++) {
            _parsedLength++;
            _parsePlainPostChar(((uint8_t *)buf)[i]);
          }
        } else {
          _parsedLength += len;
        }
      }
      if (_parsedLength == _contentLength) {
        _parseState = PARSE_REQ_END;
        _runMiddlewareChain();
        _send();
      }
    }
    break;
  }
}

void AsyncWebServerRequest::_onPoll() {
  // os_printf("p\n");
  if (_response != NULL && _client != NULL && _client->canSend()) {
    if (!_response->_finished()) {
      _response->_ack(this, 0, 0);
    } else {
      AsyncWebServerResponse *r = _response;
      _response = NULL;
      delete r;

      _client->close();
    }
  }
}

void AsyncWebServerRequest::_onAck(size_t len, uint32_t time) {
  // os_printf("a:%u:%u\n", len, time);
  if (_response != NULL) {
    if (!_response->_finished()) {
      _response->_ack(this, len, time);
    } else if (_response->_finished()) {
      AsyncWebServerResponse *r = _response;
      _response = NULL;
      delete r;

      _client->close();
    }
  }
}

void AsyncWebServerRequest::_onError(int8_t error) {
  (void)error;
}

void AsyncWebServerRequest::_onTimeout(uint32_t time) {
  (void)time;
  // os_printf("TIMEOUT: %u, state: %s\n", time, _client->stateToString());
  _client->close();
}

void AsyncWebServerRequest::onDisconnect(ArDisconnectHandler fn) {
  _onDisconnectfn = fn;
}

void AsyncWebServerRequest::_onDisconnect() {
  // os_printf("d\n");
  if (_onDisconnectfn) {
    _onDisconnectfn();
  }
  _server->_handleDisconnect(this);
}

void AsyncWebServerRequest::_addPathParam(const char *p) {
  _pathParams.emplace_back(p);
}

void AsyncWebServerRequest::_addGetParams(const String &params) {
  size_t start = 0;
  while (start < params.length()) {
    int end = params.indexOf('&', start);
    if (end < 0) {
      end = params.length();
    }
    int equal = params.indexOf('=', start);
    if (equal < 0 || equal > end) {
      equal = end;
    }
    String name = urlDecode(params.substring(start, equal));
    String value = urlDecode(equal + 1 < end ? params.substring(equal + 1, end) : emptyString);
    if (name.length()) {
      _params.emplace_back(name, value);
    }
    start = end + 1;
  }
}

bool AsyncWebServerRequest::_parseReqHead() {
  // Split the head into method, url and version
  int index = _temp.indexOf(' ');
  String m = _temp.substring(0, index);
  index = _temp.indexOf(' ', index + 1);
  String u = _temp.substring(m.length() + 1, index);
  _temp = _temp.substring(index + 1);

  if (m == T_GET) {
    _method = HTTP_GET;
  } else if (m == T_POST) {
    _method = HTTP_POST;
  } else if (m == T_DELETE) {
    _method = HTTP_DELETE;
  } else if (m == T_PUT) {
    _method = HTTP_PUT;
  } else if (m == T_PATCH) {
    _method = HTTP_PATCH;
  } else if (m == T_HEAD) {
    _method = HTTP_HEAD;
  } else if (m == T_OPTIONS) {
    _method = HTTP_OPTIONS;
  } else if (m == T_PROPFIND) {
    _method = HTTP_PROPFIND;
  } else if (m == T_LOCK) {
    _method = HTTP_LOCK;
  } else if (m == T_UNLOCK) {
    _method = HTTP_UNLOCK;
  } else if (m == T_PROPPATCH) {
    _method = HTTP_PROPPATCH;
  } else if (m == T_MKCOL) {
    _method = HTTP_MKCOL;
  } else if (m == T_MOVE) {
    _method = HTTP_MOVE;
  } else if (m == T_COPY) {
    _method = HTTP_COPY;
  } else if (m == T_RESERVED) {
    _method = HTTP_RESERVED;
  } else {
    return false;
  }

  String g;
  index = u.indexOf('?');
  if (index > 0) {
    g = u.substring(index + 1);
    u = u.substring(0, index);
  }
  _url = urlDecode(u);
  _addGetParams(g);

  if (!_url.length()) {
    return false;
  }

  if (!_temp.startsWith(T_HTTP_1_0)) {
    _version = 1;
  }

  _temp = emptyString;
  return true;
}

bool AsyncWebServerRequest::_parseReqHeader() {
  AsyncWebHeader header = AsyncWebHeader::parse(_temp);
  if (header) {
    const String &name = header.name();
    const String &value = header.value();
    if (name.equalsIgnoreCase(T_Host)) {
      _host = value;
    } else if (name.equalsIgnoreCase(T_Content_Type)) {
      _contentType = value.substring(0, value.indexOf(';'));
      if (value.startsWith(T_MULTIPART_)) {
        _boundary = value.substring(value.indexOf('=') + 1);
        _boundary.replace(String('"'), String());
        _isMultipart = true;
      }
    } else if (name.equalsIgnoreCase(T_Content_Length)) {
      _contentLength = atoi(value.c_str());
    } else if (name.equalsIgnoreCase(T_EXPECT) && value.equalsIgnoreCase(T_100_CONTINUE)) {
      _expectingContinue = true;
    } else if (name.equalsIgnoreCase(T_AUTH)) {
      int space = value.indexOf(' ');
      if (space == -1) {
        _authorization = value;
        _authMethod = AsyncAuthType::AUTH_OTHER;
      } else {
        String method = value.substring(0, space);
        if (method.equalsIgnoreCase(T_BASIC)) {
          _authMethod = AsyncAuthType::AUTH_BASIC;
        } else if (method.equalsIgnoreCase(T_DIGEST)) {
          _authMethod = AsyncAuthType::AUTH_DIGEST;
        } else if (method.equalsIgnoreCase(T_BEARER)) {
          _authMethod = AsyncAuthType::AUTH_BEARER;
        } else {
          _authMethod = AsyncAuthType::AUTH_OTHER;
        }
        _authorization = value.substring(space + 1);
      }
    } else if (name.equalsIgnoreCase(T_UPGRADE) && value.equalsIgnoreCase(T_WS)) {
      // WebSocket request can be uniquely identified by header: [Upgrade: websocket]
      _reqconntype = RCT_WS;
    } else if (name.equalsIgnoreCase(T_ACCEPT)) {
      String lowcase(value);
      lowcase.toLowerCase();
#ifndef ESP8266
      const char *substr = strstr(lowcase.c_str(), T_text_event_stream);
#else
      const char *substr = std::strstr(lowcase.c_str(), String(T_text_event_stream).c_str());
#endif
      if (substr != NULL) {
        // WebEvent request can be uniquely identified by header:  [Accept: text/event-stream]
        _reqconntype = RCT_EVENT;
      }
    }
    _headers.emplace_back(std::move(header));
  }
#if defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(LIBRETINY)
  // Ancient PRI core does not have String::clear() method 8-()
  _temp = emptyString;
#else
  _temp.clear();
#endif
  return true;
}

void AsyncWebServerRequest::_parsePlainPostChar(uint8_t data) {
  if (data && (char)data != '&') {
    _temp += (char)data;
  }
  if (!data || (char)data == '&' || _parsedLength == _contentLength) {
    String name(T_BODY);
    String value(_temp);
    if (!(_temp.charAt(0) == '{') && !(_temp.charAt(0) == '[') && _temp.indexOf('=') > 0) {
      name = _temp.substring(0, _temp.indexOf('='));
      value = _temp.substring(_temp.indexOf('=') + 1);
    }
    name = urlDecode(name);
    if (name.length()) {
      _params.emplace_back(name, urlDecode(value), true);
    }

#if defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(LIBRETINY)
    // Ancient PRI core does not have String::clear() method 8-()
    _temp = emptyString;
#else
    _temp.clear();
#endif
  }
}

void AsyncWebServerRequest::_handleUploadByte(uint8_t data, bool last) {
  _itemBuffer[_itemBufferIndex++] = data;

  if (last || _itemBufferIndex == RESPONSE_STREAM_BUFFER_SIZE) {
    // check if authenticated before calling the upload
    if (_handler) {
      _handler->handleUpload(this, _itemFilename, _itemSize - _itemBufferIndex, _itemBuffer, _itemBufferIndex, false);
    }
    _itemBufferIndex = 0;
  }
}

enum {
  EXPECT_BOUNDARY,
  PARSE_HEADERS,
  WAIT_FOR_RETURN1,
  EXPECT_FEED1,
  EXPECT_DASH1,
  EXPECT_DASH2,
  BOUNDARY_OR_DATA,
  DASH3_OR_RETURN2,
  EXPECT_FEED2,
  PARSING_FINISHED,
  PARSE_ERROR
};

void AsyncWebServerRequest::_parseMultipartPostByte(uint8_t data, bool last) {
#define itemWriteByte(b)          \
  do {                            \
    _itemSize++;                  \
    if (_itemIsFile)              \
      _handleUploadByte(b, last); \
    else                          \
      _itemValue += (char)(b);    \
  } while (0)

  if (!_parsedLength) {
    _multiParseState = EXPECT_BOUNDARY;
    _temp = emptyString;
    _itemName = emptyString;
    _itemFilename = emptyString;
    _itemType = emptyString;
  }

  if (_multiParseState == WAIT_FOR_RETURN1) {
    if (data != '\r') {
      itemWriteByte(data);
    } else {
      _multiParseState = EXPECT_FEED1;
    }
  } else if (_multiParseState == EXPECT_BOUNDARY) {
    if (_parsedLength < 2 && data != '-') {
      _multiParseState = PARSE_ERROR;
      return;
    } else if (_parsedLength - 2 < _boundary.length() && _boundary.c_str()[_parsedLength - 2] != data) {
      _multiParseState = PARSE_ERROR;
      return;
    } else if (_parsedLength - 2 == _boundary.length() && data != '\r') {
      _multiParseState = PARSE_ERROR;
      return;
    } else if (_parsedLength - 3 == _boundary.length()) {
      if (data != '\n') {
        _multiParseState = PARSE_ERROR;
        return;
      }
      _multiParseState = PARSE_HEADERS;
      _itemIsFile = false;
    }
  } else if (_multiParseState == PARSE_HEADERS) {
    if ((char)data != '\r' && (char)data != '\n') {
      _temp += (char)data;
    }
    if ((char)data == '\n') {
      if (_temp.length()) {
        if (_temp.length() > 12 && _temp.substring(0, 12).equalsIgnoreCase(T_Content_Type)) {
          _itemType = _temp.substring(14);
          _itemIsFile = true;
        } else if (_temp.length() > 19 && _temp.substring(0, 19).equalsIgnoreCase(T_Content_Disposition)) {
          _temp = _temp.substring(_temp.indexOf(';') + 2);
          while (_temp.indexOf(';') > 0) {
            String name = _temp.substring(0, _temp.indexOf('='));
            String nameVal = _temp.substring(_temp.indexOf('=') + 2, _temp.indexOf(';') - 1);
            if (name == T_name) {
              _itemName = nameVal;
            } else if (name == T_filename) {
              _itemFilename = nameVal;
              _itemIsFile = true;
            }
            _temp = _temp.substring(_temp.indexOf(';') + 2);
          }
          String name = _temp.substring(0, _temp.indexOf('='));
          String nameVal = _temp.substring(_temp.indexOf('=') + 2, _temp.length() - 1);
          if (name == T_name) {
            _itemName = nameVal;
          } else if (name == T_filename) {
            _itemFilename = nameVal;
            _itemIsFile = true;
          }
        }
        _temp = emptyString;
      } else {
        _multiParseState = WAIT_FOR_RETURN1;
        // value starts from here
        _itemSize = 0;
        _itemStartIndex = _parsedLength;
        _itemValue = emptyString;
        if (_itemIsFile) {
          if (_itemBuffer) {
            free(_itemBuffer);
          }
          _itemBuffer = (uint8_t *)malloc(RESPONSE_STREAM_BUFFER_SIZE);
          if (_itemBuffer == NULL) {
#ifdef ESP32
            log_e("Failed to allocate");
#endif
            _multiParseState = PARSE_ERROR;
            abort();
            return;
          }
          _itemBufferIndex = 0;
        }
      }
    }
  } else if (_multiParseState == EXPECT_FEED1) {
    if (data != '\n') {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r');
      _parseMultipartPostByte(data, last);
    } else {
      _multiParseState = EXPECT_DASH1;
    }
  } else if (_multiParseState == EXPECT_DASH1) {
    if (data != '-') {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r');
      itemWriteByte('\n');
      _parseMultipartPostByte(data, last);
    } else {
      _multiParseState = EXPECT_DASH2;
    }
  } else if (_multiParseState == EXPECT_DASH2) {
    if (data != '-') {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r');
      itemWriteByte('\n');
      itemWriteByte('-');
      _parseMultipartPostByte(data, last);
    } else {
      _multiParseState = BOUNDARY_OR_DATA;
      _boundaryPosition = 0;
    }
  } else if (_multiParseState == BOUNDARY_OR_DATA) {
    if (_boundaryPosition < _boundary.length() && _boundary.c_str()[_boundaryPosition] != data) {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r');
      itemWriteByte('\n');
      itemWriteByte('-');
      itemWriteByte('-');
      uint8_t i;
      for (i = 0; i < _boundaryPosition; i++) {
        itemWriteByte(_boundary.c_str()[i]);
      }
      _parseMultipartPostByte(data, last);
    } else if (_boundaryPosition == _boundary.length() - 1) {
      _multiParseState = DASH3_OR_RETURN2;
      if (!_itemIsFile) {
        _params.emplace_back(_itemName, _itemValue, true);
      } else {
        if (_itemSize) {
          if (_handler) {
            _handler->handleUpload(this, _itemFilename, _itemSize - _itemBufferIndex, _itemBuffer, _itemBufferIndex, true);
          }
          _itemBufferIndex = 0;
          _params.emplace_back(_itemName, _itemFilename, true, true, _itemSize);
        }
        free(_itemBuffer);
        _itemBuffer = NULL;
      }

    } else {
      _boundaryPosition++;
    }
  } else if (_multiParseState == DASH3_OR_RETURN2) {
    if (data == '-' && (_contentLength - _parsedLength - 4) != 0) {
      // os_printf("ERROR: The parser got to the end of the POST but is expecting %u bytes more!\nDrop an issue so we can have more info on the matter!\n", _contentLength - _parsedLength - 4);
      _contentLength = _parsedLength + 4;  // lets close the request gracefully
    }
    if (data == '\r') {
      _multiParseState = EXPECT_FEED2;
    } else if (data == '-' && _contentLength == (_parsedLength + 4)) {
      _multiParseState = PARSING_FINISHED;
    } else {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r');
      itemWriteByte('\n');
      itemWriteByte('-');
      itemWriteByte('-');
      uint8_t i;
      for (i = 0; i < _boundary.length(); i++) {
        itemWriteByte(_boundary.c_str()[i]);
      }
      _parseMultipartPostByte(data, last);
    }
  } else if (_multiParseState == EXPECT_FEED2) {
    if (data == '\n') {
      _multiParseState = PARSE_HEADERS;
      _itemIsFile = false;
    } else {
      _multiParseState = WAIT_FOR_RETURN1;
      itemWriteByte('\r');
      itemWriteByte('\n');
      itemWriteByte('-');
      itemWriteByte('-');
      uint8_t i;
      for (i = 0; i < _boundary.length(); i++) {
        itemWriteByte(_boundary.c_str()[i]);
      }
      itemWriteByte('\r');
      _parseMultipartPostByte(data, last);
    }
  }
}

void AsyncWebServerRequest::_parseLine() {
  if (_parseState == PARSE_REQ_START) {
    if (!_temp.length()) {
      _parseState = PARSE_REQ_FAIL;
      abort();
    } else {
      if (_parseReqHead()) {
        _parseState = PARSE_REQ_HEADERS;
      } else {
        _parseState = PARSE_REQ_FAIL;
        abort();
      }
    }
    return;
  }

  if (_parseState == PARSE_REQ_HEADERS) {
    if (!_temp.length()) {
      // end of headers
      _server->_rewriteRequest(this);
      _server->_attachHandler(this);
      if (_expectingContinue) {
        String response(T_HTTP_100_CONT);
        _client->write(response.c_str(), response.length());
      }
      if (_contentLength) {
        _parseState = PARSE_REQ_BODY;
      } else {
        _parseState = PARSE_REQ_END;
        _runMiddlewareChain();
        _send();
      }
    } else {
      _parseReqHeader();
    }
  }
}

void AsyncWebServerRequest::_runMiddlewareChain() {
  if (_handler && _handler->mustSkipServerMiddlewares()) {
    _handler->_runChain(this, [this]() {
      _handler->handleRequest(this);
    });
  } else {
    _server->_runChain(this, [this]() {
      if (_handler) {
        _handler->_runChain(this, [this]() {
          _handler->handleRequest(this);
        });
      }
    });
  }
}

void AsyncWebServerRequest::_send() {
  if (!_sent && !_paused) {
    // log_d("AsyncWebServerRequest::_send()");

    // user did not create a response ?
    if (!_response) {
      send(501, T_text_plain, "Handler did not handle the request");
    }

    // response is not valid ?
    if (!_response->_sourceValid()) {
      send(500, T_text_plain, "Invalid data in handler");
    }

    // here, we either have a response give nfrom user or one of the two above
    _client->setRxTimeout(0);
    _response->_respond(this);
    _sent = true;
  }
}

AsyncWebServerRequestPtr AsyncWebServerRequest::pause() {
  if (_paused) {
    return _this;
  }
  client()->setRxTimeout(0);
  // this shared ptr will hold the request pointer until it gets destroyed following a disconnect.
  // this is just used as a holder providing weak observers, so the deleter is a no-op.
  _this = std::shared_ptr<AsyncWebServerRequest>(this, doNotDelete);
  _paused = true;
  return _this;
}

void AsyncWebServerRequest::abort() {
  if (!_sent) {
    _sent = true;
    _paused = false;
    _this.reset();
    // log_e("AsyncWebServerRequest::abort");
    _client->abort();
  }
}

size_t AsyncWebServerRequest::headers() const {
  return _headers.size();
}

bool AsyncWebServerRequest::hasHeader(const char *name) const {
  for (const auto &h : _headers) {
    if (h.name().equalsIgnoreCase(name)) {
      return true;
    }
  }
  return false;
}

#ifdef ESP8266
bool AsyncWebServerRequest::hasHeader(const __FlashStringHelper *data) const {
  return hasHeader(String(data));
}
#endif

const AsyncWebHeader *AsyncWebServerRequest::getHeader(const char *name) const {
  auto iter = std::find_if(std::begin(_headers), std::end(_headers), [&name](const AsyncWebHeader &header) {
    return header.name().equalsIgnoreCase(name);
  });
  return (iter == std::end(_headers)) ? nullptr : &(*iter);
}

#ifdef ESP8266
const AsyncWebHeader *AsyncWebServerRequest::getHeader(const __FlashStringHelper *data) const {
  PGM_P p = reinterpret_cast<PGM_P>(data);
  size_t n = strlen_P(p);
  char *name = (char *)malloc(n + 1);
  if (name) {
    strcpy_P(name, p);
    const AsyncWebHeader *result = getHeader(String(name));
    free(name);
    return result;
  } else {
    return nullptr;
  }
}
#endif

const AsyncWebHeader *AsyncWebServerRequest::getHeader(size_t num) const {
  if (num >= _headers.size()) {
    return nullptr;
  }
  return &(*std::next(_headers.cbegin(), num));
}

size_t AsyncWebServerRequest::getHeaderNames(std::vector<const char *> &names) const {
  const size_t size = names.size();
  for (const auto &h : _headers) {
    names.push_back(h.name().c_str());
  }
  return names.size() - size;
}

bool AsyncWebServerRequest::removeHeader(const char *name) {
  const size_t size = _headers.size();
  _headers.remove_if([name](const AsyncWebHeader &header) {
    return header.name().equalsIgnoreCase(name);
  });
  return size != _headers.size();
}

size_t AsyncWebServerRequest::params() const {
  return _params.size();
}

bool AsyncWebServerRequest::hasParam(const char *name, bool post, bool file) const {
  for (const auto &p : _params) {
    if (p.name().equals(name) && p.isPost() == post && p.isFile() == file) {
      return true;
    }
  }
  return false;
}

const AsyncWebParameter *AsyncWebServerRequest::getParam(const char *name, bool post, bool file) const {
  for (const auto &p : _params) {
    if (p.name() == name && p.isPost() == post && p.isFile() == file) {
      return &p;
    }
  }
  return nullptr;
}

#ifdef ESP8266
const AsyncWebParameter *AsyncWebServerRequest::getParam(const __FlashStringHelper *data, bool post, bool file) const {
  return getParam(String(data), post, file);
}
#endif

const AsyncWebParameter *AsyncWebServerRequest::getParam(size_t num) const {
  if (num >= _params.size()) {
    return nullptr;
  }
  return &(*std::next(_params.cbegin(), num));
}

const String &AsyncWebServerRequest::getAttribute(const char *name, const String &defaultValue) const {
  auto it = _attributes.find(name);
  return it != _attributes.end() ? it->second : defaultValue;
}
bool AsyncWebServerRequest::getAttribute(const char *name, bool defaultValue) const {
  auto it = _attributes.find(name);
  return it != _attributes.end() ? it->second == "1" : defaultValue;
}
long AsyncWebServerRequest::getAttribute(const char *name, long defaultValue) const {
  auto it = _attributes.find(name);
  return it != _attributes.end() ? it->second.toInt() : defaultValue;
}
float AsyncWebServerRequest::getAttribute(const char *name, float defaultValue) const {
  auto it = _attributes.find(name);
  return it != _attributes.end() ? it->second.toFloat() : defaultValue;
}
double AsyncWebServerRequest::getAttribute(const char *name, double defaultValue) const {
  auto it = _attributes.find(name);
  return it != _attributes.end() ? it->second.toDouble() : defaultValue;
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse(int code, const char *contentType, const char *content, AwsTemplateProcessor callback) {
  if (callback) {
    return new AsyncProgmemResponse(code, contentType, (const uint8_t *)content, strlen(content), callback);
  }
  return new AsyncBasicResponse(code, contentType, content);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(int code, const char *contentType, const uint8_t *content, size_t len, AwsTemplateProcessor callback) {
  return new AsyncProgmemResponse(code, contentType, content, len, callback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  if (fs.exists(path) || (!download && fs.exists(path + T__gz))) {
    return new AsyncFileResponse(fs, path, contentType, download, callback);
  }
  return NULL;
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(File content, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  if (content == true) {
    return new AsyncFileResponse(content, path, contentType, download, callback);
  }
  return NULL;
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse(Stream &stream, const char *contentType, size_t len, AwsTemplateProcessor callback) {
  return new AsyncStreamResponse(stream, contentType, len, callback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginResponse(const char *contentType, size_t len, AwsResponseFiller callback, AwsTemplateProcessor templateCallback) {
  return new AsyncCallbackResponse(contentType, len, callback, templateCallback);
}

AsyncWebServerResponse *
  AsyncWebServerRequest::beginChunkedResponse(const char *contentType, AwsResponseFiller callback, AwsTemplateProcessor templateCallback) {
  if (_version) {
    return new AsyncChunkedResponse(contentType, callback, templateCallback);
  }
  return new AsyncCallbackResponse(contentType, 0, callback, templateCallback);
}

AsyncResponseStream *AsyncWebServerRequest::beginResponseStream(const char *contentType, size_t bufferSize) {
  return new AsyncResponseStream(contentType, bufferSize);
}

AsyncWebServerResponse *AsyncWebServerRequest::beginResponse_P(int code, const String &contentType, PGM_P content, AwsTemplateProcessor callback) {
  return new AsyncProgmemResponse(code, contentType, (const uint8_t *)content, strlen_P(content), callback);
}

void AsyncWebServerRequest::send(AsyncWebServerResponse *response) {
  // request is already sent on the wire ?
  if (_sent) {
    return;
  }

  // if we already had a response, delete it and replace it with the new one
  if (_response) {
    delete _response;
  }
  _response = response;

  // if request was paused, we need to send the response now
  if (_paused) {
    _paused = false;
    _send();
  }
}

void AsyncWebServerRequest::redirect(const char *url, int code) {
  AsyncWebServerResponse *response = beginResponse(code);
  response->addHeader(T_LOCATION, url);
  send(response);
}

bool AsyncWebServerRequest::authenticate(const char *username, const char *password, const char *realm, bool passwordIsHash) const {
  if (_authorization.length()) {
    if (_authMethod == AsyncAuthType::AUTH_DIGEST) {
      return checkDigestAuthentication(_authorization.c_str(), methodToString(), username, password, realm, passwordIsHash, NULL, NULL, NULL);
    } else if (!passwordIsHash) {
      return checkBasicAuthentication(_authorization.c_str(), username, password);
    } else {
      return _authorization.equals(password);
    }
  }
  return false;
}

bool AsyncWebServerRequest::authenticate(const char *hash) const {
  if (!_authorization.length() || hash == NULL) {
    return false;
  }

  if (_authMethod == AsyncAuthType::AUTH_DIGEST) {
    String hStr = String(hash);
    int separator = hStr.indexOf(':');
    if (separator <= 0) {
      return false;
    }
    String username = hStr.substring(0, separator);
    hStr = hStr.substring(separator + 1);
    separator = hStr.indexOf(':');
    if (separator <= 0) {
      return false;
    }
    String realm = hStr.substring(0, separator);
    hStr = hStr.substring(separator + 1);
    return checkDigestAuthentication(_authorization.c_str(), methodToString(), username.c_str(), hStr.c_str(), realm.c_str(), true, NULL, NULL, NULL);
  }

  // Basic Auth, Bearer Auth, or other
  return (_authorization.equals(hash));
}

void AsyncWebServerRequest::requestAuthentication(AsyncAuthType method, const char *realm, const char *_authFailMsg) {
  if (!realm) {
    realm = T_LOGIN_REQ;
  }

  AsyncWebServerResponse *r = _authFailMsg ? beginResponse(401, T_text_html, _authFailMsg) : beginResponse(401);

  switch (method) {
    case AsyncAuthType::AUTH_BASIC:
    {
      String header;
      if (header.reserve(strlen(T_BASIC_REALM) + strlen(realm) + 1)) {
        header.concat(T_BASIC_REALM);
        header.concat(realm);
        header.concat('"');
        r->addHeader(T_WWW_AUTH, header.c_str());
      } else {
#ifdef ESP32
        log_e("Failed to allocate");
#endif
        abort();
      }

      break;
    }
    case AsyncAuthType::AUTH_DIGEST:
    {
      size_t len = strlen(T_DIGEST_) + strlen(T_realm__) + strlen(T_auth_nonce) + 32 + strlen(T__opaque) + 32 + 1;
      String header;
      if (header.reserve(len + strlen(realm))) {
        const String nonce = genRandomMD5();
        const String opaque = genRandomMD5();
        if (nonce.length() && opaque.length()) {
          header.concat(T_DIGEST_);
          header.concat(T_realm__);
          header.concat(realm);
          header.concat(T_auth_nonce);
          header.concat(nonce);
          header.concat(T__opaque);
          header.concat(opaque);
          header.concat((char)0x22);  // '"'
          r->addHeader(T_WWW_AUTH, header.c_str());
        } else {
#ifdef ESP32
          log_e("Failed to allocate");
#endif
          abort();
        }
      }
      break;
    }
    default: break;
  }

  send(r);
}

bool AsyncWebServerRequest::hasArg(const char *name) const {
  for (const auto &arg : _params) {
    if (arg.name() == name) {
      return true;
    }
  }
  return false;
}

#ifdef ESP8266
bool AsyncWebServerRequest::hasArg(const __FlashStringHelper *data) const {
  return hasArg(String(data).c_str());
}
#endif

const String &AsyncWebServerRequest::arg(const char *name) const {
  for (const auto &arg : _params) {
    if (arg.name() == name) {
      return arg.value();
    }
  }
  return emptyString;
}

#ifdef ESP8266
const String &AsyncWebServerRequest::arg(const __FlashStringHelper *data) const {
  return arg(String(data).c_str());
}
#endif

const String &AsyncWebServerRequest::arg(size_t i) const {
  return getParam(i)->value();
}

const String &AsyncWebServerRequest::argName(size_t i) const {
  return getParam(i)->name();
}

const String &AsyncWebServerRequest::pathArg(size_t i) const {
  if (i >= _pathParams.size()) {
    return emptyString;
  }
  auto it = _pathParams.begin();
  std::advance(it, i);
  return *it;
}

const String &AsyncWebServerRequest::header(const char *name) const {
  const AsyncWebHeader *h = getHeader(name);
  return h ? h->value() : emptyString;
}

#ifdef ESP8266
const String &AsyncWebServerRequest::header(const __FlashStringHelper *data) const {
  return header(String(data).c_str());
};
#endif

const String &AsyncWebServerRequest::header(size_t i) const {
  const AsyncWebHeader *h = getHeader(i);
  return h ? h->value() : emptyString;
}

const String &AsyncWebServerRequest::headerName(size_t i) const {
  const AsyncWebHeader *h = getHeader(i);
  return h ? h->name() : emptyString;
}

String AsyncWebServerRequest::urlDecode(const String &text) const {
  char temp[] = "0x00";
  unsigned int len = text.length();
  unsigned int i = 0;
  String decoded;
  // Allocate the string internal buffer - never longer from source text
  if (!decoded.reserve(len)) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    return emptyString;
  }
  while (i < len) {
    char decodedChar;
    char encodedChar = text.charAt(i++);
    if ((encodedChar == '%') && (i + 1 < len)) {
      temp[2] = text.charAt(i++);
      temp[3] = text.charAt(i++);
      decodedChar = strtol(temp, NULL, 16);
    } else if (encodedChar == '+') {
      decodedChar = ' ';
    } else {
      decodedChar = encodedChar;  // normal ascii char
    }
    decoded.concat(decodedChar);
  }
  return decoded;
}

const char *AsyncWebServerRequest::methodToString() const {
  if (_method == HTTP_ANY) {
    return T_ANY;
  }
  if (_method & HTTP_GET) {
    return T_GET;
  }
  if (_method & HTTP_POST) {
    return T_POST;
  }
  if (_method & HTTP_DELETE) {
    return T_DELETE;
  }
  if (_method & HTTP_PUT) {
    return T_PUT;
  }
  if (_method & HTTP_PATCH) {
    return T_PATCH;
  }
  if (_method & HTTP_HEAD) {
    return T_HEAD;
  }
  if (_method & HTTP_OPTIONS) {
    return T_OPTIONS;
  }
  if(_method & HTTP_PROPFIND){
	  return T_PROPFIND;
  }
  if(_method & HTTP_LOCK){
	  return T_LOCK;
  }
  if(_method & HTTP_UNLOCK){
	  return T_UNLOCK;
  }
  if(_method & HTTP_PROPPATCH){
	  return T_PROPPATCH;
  }
  if(_method & HTTP_MKCOL){
	  return T_MKCOL;
  }
  if(_method & HTTP_MOVE){
	  return T_MOVE;
  }
  if(_method & HTTP_COPY){
	  return T_COPY;
  }
  if(_method & HTTP_RESERVED){
	  return T_RESERVED;
  }
  return T_UNKNOWN;
}

const char *AsyncWebServerRequest::requestedConnTypeToString() const {
  switch (_reqconntype) {
    case RCT_NOT_USED: return T_RCT_NOT_USED;
    case RCT_DEFAULT:  return T_RCT_DEFAULT;
    case RCT_HTTP:     return T_RCT_HTTP;
    case RCT_WS:       return T_RCT_WS;
    case RCT_EVENT:    return T_RCT_EVENT;
    default:           return T_ERROR;
  }
}

bool AsyncWebServerRequest::isExpectedRequestedConnType(RequestedConnectionType erct1, RequestedConnectionType erct2, RequestedConnectionType erct3) const {
  return ((erct1 != RCT_NOT_USED) && (erct1 == _reqconntype)) || ((erct2 != RCT_NOT_USED) && (erct2 == _reqconntype))
         || ((erct3 != RCT_NOT_USED) && (erct3 == _reqconntype));
}

// *** WebHandlers.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "ESPAsyncWebServer.h"
//#include "WebHandlerImpl.h"

using namespace asyncsrv;

AsyncWebHandler &AsyncWebHandler::setFilter(ArRequestFilterFunction fn) {
  _filter = fn;
  return *this;
}
AsyncWebHandler &AsyncWebHandler::setAuthentication(const char *username, const char *password, AsyncAuthType authMethod) {
  if (!_authMiddleware) {
    _authMiddleware = new AsyncAuthenticationMiddleware();
    _authMiddleware->_freeOnRemoval = true;
    addMiddleware(_authMiddleware);
  }
  _authMiddleware->setUsername(username);
  _authMiddleware->setPassword(password);
  _authMiddleware->setAuthType(authMethod);
  return *this;
};

AsyncStaticWebHandler::AsyncStaticWebHandler(const char *uri, FS &fs, const char *path, const char *cache_control)
  : _fs(fs), _uri(uri), _path(path), _default_file(F("index.htm")), _cache_control(cache_control), _last_modified(), _callback(nullptr) {
  // Ensure leading '/'
  if (_uri.length() == 0 || _uri[0] != '/') {
    _uri = String('/') + _uri;
  }
  if (_path.length() == 0 || _path[0] != '/') {
    _path = String('/') + _path;
  }

  // If path ends with '/' we assume a hint that this is a directory to improve performance.
  // However - if it does not end with '/' we, can't assume a file, path can still be a directory.
  _isDir = _path[_path.length() - 1] == '/';

  // Remove the trailing '/' so we can handle default file
  // Notice that root will be "" not "/"
  if (_uri[_uri.length() - 1] == '/') {
    _uri = _uri.substring(0, _uri.length() - 1);
  }
  if (_path[_path.length() - 1] == '/') {
    _path = _path.substring(0, _path.length() - 1);
  }
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setTryGzipFirst(bool value) {
  _tryGzipFirst = value;
  return *this;
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setIsDir(bool isDir) {
  _isDir = isDir;
  return *this;
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setDefaultFile(const char *filename) {
  _default_file = filename;
  return *this;
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setCacheControl(const char *cache_control) {
  _cache_control = cache_control;
  return *this;
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setLastModified(const char *last_modified) {
  _last_modified = last_modified;
  return *this;
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setLastModified(struct tm *last_modified) {
  char result[30];
#ifdef ESP8266
  auto formatP = PSTR("%a, %d %b %Y %H:%M:%S GMT");
  char format[strlen_P(formatP) + 1];
  strcpy_P(format, formatP);
#else
  static constexpr const char *format = "%a, %d %b %Y %H:%M:%S GMT";
#endif

  strftime(result, sizeof(result), format, last_modified);
  _last_modified = result;
  return *this;
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setLastModified(time_t last_modified) {
  return setLastModified((struct tm *)gmtime(&last_modified));
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setLastModified() {
  time_t last_modified;
  if (time(&last_modified) == 0) {  // time is not yet set
    return *this;
  }
  return setLastModified(last_modified);
}

bool AsyncStaticWebHandler::canHandle(AsyncWebServerRequest *request) const {
  return request->isHTTP() && request->method() == HTTP_GET && request->url().startsWith(_uri) && _getFile(request);
}

bool AsyncStaticWebHandler::_getFile(AsyncWebServerRequest *request) const {
  // Remove the found uri
  String path = request->url().substring(_uri.length());

  // We can skip the file check and look for default if request is to the root of a directory or that request path ends with '/'
  bool canSkipFileCheck = (_isDir && path.length() == 0) || (path.length() && path[path.length() - 1] == '/');

  path = _path + path;

  // Do we have a file or .gz file
  if (!canSkipFileCheck && const_cast<AsyncStaticWebHandler *>(this)->_searchFile(request, path)) {
    return true;
  }

  // Can't handle if not default file
  if (_default_file.length() == 0) {
    return false;
  }

  // Try to add default file, ensure there is a trailing '/' to the path.
  if (path.length() == 0 || path[path.length() - 1] != '/') {
    path += String('/');
  }
  path += _default_file;

  return const_cast<AsyncStaticWebHandler *>(this)->_searchFile(request, path);
}

#ifdef ESP32
#define FILE_IS_REAL(f) (f == true && !f.isDirectory())
#else
#define FILE_IS_REAL(f) (f == true)
#endif

bool AsyncStaticWebHandler::_searchFile(AsyncWebServerRequest *request, const String &path) {
  bool fileFound = false;
  bool gzipFound = false;

  String gzip = path + T__gz;

  if (_tryGzipFirst) {
    if (_fs.exists(gzip)) {
      request->_tempFile = _fs.open(gzip, fs::FileOpenMode::read);
      gzipFound = FILE_IS_REAL(request->_tempFile);
    }
    if (!gzipFound) {
      if (_fs.exists(path)) {
        request->_tempFile = _fs.open(path, fs::FileOpenMode::read);
        fileFound = FILE_IS_REAL(request->_tempFile);
      }
    }
  } else {
    if (_fs.exists(path)) {
      request->_tempFile = _fs.open(path, fs::FileOpenMode::read);
      fileFound = FILE_IS_REAL(request->_tempFile);
    }
    if (!fileFound) {
      if (_fs.exists(gzip)) {
        request->_tempFile = _fs.open(gzip, fs::FileOpenMode::read);
        gzipFound = FILE_IS_REAL(request->_tempFile);
      }
    }
  }

  bool found = fileFound || gzipFound;

  if (found) {
    // Extract the file name from the path and keep it in _tempObject
    size_t pathLen = path.length();
    char *_tempPath = (char *)malloc(pathLen + 1);
    if (_tempPath == NULL) {
#ifdef ESP32
      log_e("Failed to allocate");
#endif
      request->abort();
      request->_tempFile.close();
      return false;
    }
    snprintf_P(_tempPath, pathLen + 1, PSTR("%s"), path.c_str());
    request->_tempObject = (void *)_tempPath;
  }

  return found;
}

void AsyncStaticWebHandler::handleRequest(AsyncWebServerRequest *request) {
  // Get the filename from request->_tempObject and free it
  String filename((char *)request->_tempObject);
  free(request->_tempObject);
  request->_tempObject = NULL;

  if (request->_tempFile != true) {
    request->send(404);
    return;
  }

  time_t lw = request->_tempFile.getLastWrite();  // get last file mod time (if supported by FS)
  // set etag to lastmod timestamp if available, otherwise to size
  String etag;
  if (lw) {
    setLastModified(lw);
#if defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
    // time_t == long long int
    constexpr size_t len = 1 + 8 * sizeof(time_t);
    char buf[len];
    char *ret = lltoa(lw ^ request->_tempFile.size(), buf, len, 10);
    etag = ret ? String(ret) : String(request->_tempFile.size());
#elif defined(LIBRETINY)
    long val = lw ^ request->_tempFile.size();
    etag = String(val);
#else
    etag = lw ^ request->_tempFile.size();  // etag combines file size and lastmod timestamp
#endif
  } else {
#if defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(LIBRETINY)
    etag = String(request->_tempFile.size());
#else
    etag = request->_tempFile.size();
#endif
  }

  bool not_modified = false;

  // if-none-match has precedence over if-modified-since
  if (request->hasHeader(T_INM)) {
    not_modified = request->header(T_INM).equals(etag);
  } else if (_last_modified.length()) {
    not_modified = request->header(T_IMS).equals(_last_modified);
  }

  AsyncWebServerResponse *response;

  if (not_modified) {
    request->_tempFile.close();
    response = new AsyncBasicResponse(304);  // Not modified
  } else {
    response = new AsyncFileResponse(request->_tempFile, filename, emptyString, false, _callback);
  }

  if (!response) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    request->abort();
    return;
  }

  response->addHeader(T_ETag, etag.c_str());

  if (_last_modified.length()) {
    response->addHeader(T_Last_Modified, _last_modified.c_str());
  }
  if (_cache_control.length()) {
    response->addHeader(T_Cache_Control, _cache_control.c_str());
  }

  request->send(response);
}

AsyncStaticWebHandler &AsyncStaticWebHandler::setTemplateProcessor(AwsTemplateProcessor newCallback) {
  _callback = newCallback;
  return *this;
}

void AsyncCallbackWebHandler::setUri(const String &uri) {
  _uri = uri;
  _isRegex = uri.startsWith("^") && uri.endsWith("$");
}

bool AsyncCallbackWebHandler::canHandle(AsyncWebServerRequest *request) const {
  if (!_onRequest || !request->isHTTP() || !(_method & request->method())) {
    return false;
  }

#ifdef ASYNCWEBSERVER_REGEX
  if (_isRegex) {
    std::regex pattern(_uri.c_str());
    std::smatch matches;
    std::string s(request->url().c_str());
    if (std::regex_search(s, matches, pattern)) {
      for (size_t i = 1; i < matches.size(); ++i) {  // start from 1
        request->_addPathParam(matches[i].str().c_str());
      }
    } else {
      return false;
    }
  } else
#endif
    if (_uri.length() && _uri.startsWith("/*.")) {
    String uriTemplate = String(_uri);
    uriTemplate = uriTemplate.substring(uriTemplate.lastIndexOf("."));
    if (!request->url().endsWith(uriTemplate)) {
      return false;
    }
  } else if (_uri.length() && _uri.endsWith("*")) {
    String uriTemplate = String(_uri);
    uriTemplate = uriTemplate.substring(0, uriTemplate.length() - 1);
    if (!request->url().startsWith(uriTemplate)) {
      return false;
    }
  } else if (_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri + "/"))) {
    return false;
  }

  return true;
}

void AsyncCallbackWebHandler::handleRequest(AsyncWebServerRequest *request) {
  if (_onRequest) {
    _onRequest(request);
  } else {
    request->send(404, T_text_plain, "Not found");
  }
}
void AsyncCallbackWebHandler::handleUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (_onUpload) {
    _onUpload(request, filename, index, data, len, final);
  }
}
void AsyncCallbackWebHandler::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  // ESP_LOGD("AsyncWebServer", "AsyncCallbackWebHandler::handleBody");
  if (_onBody) {
    _onBody(request, data, len, index, total);
  }
}

// *** WebAuthentication.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "WebAuthentication.h"
#include <libb64/cencode.h>
#if defined(ESP32) || defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
#include <MD5Builder.h>
#else
//#include "md5.h"
#endif
//#include "literals.h"

using namespace asyncsrv;

// Basic Auth hash = base64("username:password")

bool checkBasicAuthentication(const char *hash, const char *username, const char *password) {
  if (username == NULL || password == NULL || hash == NULL) {
    return false;
  }
  return generateBasicHash(username, password).equalsIgnoreCase(hash);
}

String generateBasicHash(const char *username, const char *password) {
  if (username == NULL || password == NULL) {
    return emptyString;
  }

  size_t toencodeLen = strlen(username) + strlen(password) + 1;

  char *toencode = new char[toencodeLen + 1];
  if (toencode == NULL) {
    return emptyString;
  }
  char *encoded = new char[base64_encode_expected_len(toencodeLen) + 1];
  if (encoded == NULL) {
    delete[] toencode;
    return emptyString;
  }
  sprintf_P(toencode, PSTR("%s:%s"), username, password);
  if (base64_encode_chars(toencode, toencodeLen, encoded) > 0) {
    String res = String(encoded);
    delete[] toencode;
    delete[] encoded;
    return res;
  }
  delete[] toencode;
  delete[] encoded;
  return emptyString;
}

static bool getMD5(uint8_t *data, uint16_t len, char *output) {  // 33 bytes or more
#if defined(ESP32) || defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350)
  MD5Builder md5;
  md5.begin();
  md5.add(data, len);
  md5.calculate();
  md5.getChars(output);
#else
  md5_context_t _ctx;

  uint8_t *_buf = (uint8_t *)malloc(16);
  if (_buf == NULL) {
    return false;
  }
  memset(_buf, 0x00, 16);

  MD5Init(&_ctx);
  MD5Update(&_ctx, data, len);
  MD5Final(_buf, &_ctx);

  for (uint8_t i = 0; i < 16; i++) {
    sprintf_P(output + (i * 2), PSTR("%02x"), _buf[i]);
  }

  free(_buf);
#endif
  return true;
}

String genRandomMD5() {
#ifdef ESP8266
  uint32_t r = RANDOM_REG32;
#else
  uint32_t r = rand();
#endif
  char *out = (char *)malloc(33);
  if (out == NULL || !getMD5((uint8_t *)(&r), 4, out)) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    return emptyString;
  }
  String res = String(out);
  free(out);
  return res;
}

static String stringMD5(const String &in) {
  char *out = (char *)malloc(33);
  if (out == NULL || !getMD5((uint8_t *)(in.c_str()), in.length(), out)) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    return emptyString;
  }
  String res = String(out);
  free(out);
  return res;
}

String generateDigestHash(const char *username, const char *password, const char *realm) {
  if (username == NULL || password == NULL || realm == NULL) {
    return emptyString;
  }
  char *out = (char *)malloc(33);
  if (out == NULL) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    return emptyString;
  }

  String in;
  if (!in.reserve(strlen(username) + strlen(realm) + strlen(password) + 2)) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    free(out);
    return emptyString;
  }

  in.concat(username);
  in.concat(':');
  in.concat(realm);
  in.concat(':');
  in.concat(password);

  if (!getMD5((uint8_t *)(in.c_str()), in.length(), out)) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    free(out);
    return emptyString;
  }

  in = String(out);
  free(out);
  return in;
}

bool checkDigestAuthentication(
  const char *header, const char *method, const char *username, const char *password, const char *realm, bool passwordIsHash, const char *nonce,
  const char *opaque, const char *uri
) {
  if (username == NULL || password == NULL || header == NULL || method == NULL) {
    // os_printf("AUTH FAIL: missing required fields\n");
    return false;
  }

  String myHeader(header);
  int nextBreak = myHeader.indexOf(',');
  if (nextBreak < 0) {
    // os_printf("AUTH FAIL: no variables\n");
    return false;
  }

  String myUsername;
  String myRealm;
  String myNonce;
  String myUri;
  String myResponse;
  String myQop;
  String myNc;
  String myCnonce;

  myHeader += (char)0x2c;  // ','
  myHeader += (char)0x20;  // ' '
  do {
    String avLine(myHeader.substring(0, nextBreak));
    avLine.trim();
    myHeader = myHeader.substring(nextBreak + 1);
    nextBreak = myHeader.indexOf(',');

    int eqSign = avLine.indexOf('=');
    if (eqSign < 0) {
      // os_printf("AUTH FAIL: no = sign\n");
      return false;
    }
    String varName(avLine.substring(0, eqSign));
    avLine = avLine.substring(eqSign + 1);
    if (avLine.startsWith(String('"'))) {
      avLine = avLine.substring(1, avLine.length() - 1);
    }

    if (varName.equals(T_username)) {
      if (!avLine.equals(username)) {
        // os_printf("AUTH FAIL: username\n");
        return false;
      }
      myUsername = avLine;
    } else if (varName.equals(T_realm)) {
      if (realm != NULL && !avLine.equals(realm)) {
        // os_printf("AUTH FAIL: realm\n");
        return false;
      }
      myRealm = avLine;
    } else if (varName.equals(T_nonce)) {
      if (nonce != NULL && !avLine.equals(nonce)) {
        // os_printf("AUTH FAIL: nonce\n");
        return false;
      }
      myNonce = avLine;
    } else if (varName.equals(T_opaque)) {
      if (opaque != NULL && !avLine.equals(opaque)) {
        // os_printf("AUTH FAIL: opaque\n");
        return false;
      }
    } else if (varName.equals(T_uri)) {
      if (uri != NULL && !avLine.equals(uri)) {
        // os_printf("AUTH FAIL: uri\n");
        return false;
      }
      myUri = avLine;
    } else if (varName.equals(T_response)) {
      myResponse = avLine;
    } else if (varName.equals(T_qop)) {
      myQop = avLine;
    } else if (varName.equals(T_nc)) {
      myNc = avLine;
    } else if (varName.equals(T_cnonce)) {
      myCnonce = avLine;
    }
  } while (nextBreak > 0);

  String ha1 = passwordIsHash ? password : stringMD5(myUsername + ':' + myRealm + ':' + password).c_str();
  String ha2 = stringMD5(String(method) + ':' + myUri);
  String response = ha1 + ':' + myNonce + ':' + myNc + ':' + myCnonce + ':' + myQop + ':' + ha2;

  if (myResponse.equals(stringMD5(response))) {
    // os_printf("AUTH SUCCESS\n");
    return true;
  }

  // os_printf("AUTH FAIL: password\n");
  return false;
}


// *** Middleware.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "WebAuthentication.h"
//#include <ESPAsyncWebServer.h>

AsyncMiddlewareChain::~AsyncMiddlewareChain() {
  for (AsyncMiddleware *m : _middlewares) {
    if (m->_freeOnRemoval) {
      delete m;
    }
  }
}

void AsyncMiddlewareChain::addMiddleware(ArMiddlewareCallback fn) {
  AsyncMiddlewareFunction *m = new AsyncMiddlewareFunction(fn);
  m->_freeOnRemoval = true;
  _middlewares.emplace_back(m);
}

void AsyncMiddlewareChain::addMiddleware(AsyncMiddleware *middleware) {
  if (middleware) {
    _middlewares.emplace_back(middleware);
  }
}

void AsyncMiddlewareChain::addMiddlewares(std::vector<AsyncMiddleware *> middlewares) {
  for (AsyncMiddleware *m : middlewares) {
    addMiddleware(m);
  }
}

bool AsyncMiddlewareChain::removeMiddleware(AsyncMiddleware *middleware) {
  // remove all middlewares from _middlewares vector being equal to middleware, delete them having _freeOnRemoval flag to true and resize the vector.
  const size_t size = _middlewares.size();
  _middlewares.erase(
    std::remove_if(
      _middlewares.begin(), _middlewares.end(),
      [middleware](AsyncMiddleware *m) {
        if (m == middleware) {
          if (m->_freeOnRemoval) {
            delete m;
          }
          return true;
        }
        return false;
      }
    ),
    _middlewares.end()
  );
  return size != _middlewares.size();
}

void AsyncMiddlewareChain::_runChain(AsyncWebServerRequest *request, ArMiddlewareNext finalizer) {
  if (!_middlewares.size()) {
    return finalizer();
  }
  ArMiddlewareNext next;
  std::list<AsyncMiddleware *>::iterator it = _middlewares.begin();
  next = [this, &next, &it, request, finalizer]() {
    if (it == _middlewares.end()) {
      return finalizer();
    }
    AsyncMiddleware *m = *it;
    it++;
    return m->run(request, next);
  };
  return next();
}

void AsyncAuthenticationMiddleware::setUsername(const char *username) {
  _username = username;
  _hasCreds = _username.length() && _credentials.length();
}

void AsyncAuthenticationMiddleware::setPassword(const char *password) {
  _credentials = password;
  _hash = false;
  _hasCreds = _username.length() && _credentials.length();
}

void AsyncAuthenticationMiddleware::setPasswordHash(const char *hash) {
  _credentials = hash;
  _hash = _credentials.length();
  _hasCreds = _username.length() && _credentials.length();
}

bool AsyncAuthenticationMiddleware::generateHash() {
  // ensure we have all the necessary data
  if (!_hasCreds) {
    return false;
  }

  // if we already have a hash, do nothing
  if (_hash) {
    return false;
  }

  switch (_authMethod) {
    case AsyncAuthType::AUTH_DIGEST:
      _credentials = generateDigestHash(_username.c_str(), _credentials.c_str(), _realm.c_str());
      if (_credentials.length()) {
        _hash = true;
        return true;
      } else {
        return false;
      }

    case AsyncAuthType::AUTH_BASIC:
      _credentials = generateBasicHash(_username.c_str(), _credentials.c_str());
      if (_credentials.length()) {
        _hash = true;
        return true;
      } else {
        return false;
      }

    default: return false;
  }
}

bool AsyncAuthenticationMiddleware::allowed(AsyncWebServerRequest *request) const {
  if (_authMethod == AsyncAuthType::AUTH_NONE) {
    return true;
  }

  if (_authMethod == AsyncAuthType::AUTH_DENIED) {
    return false;
  }

  if (!_hasCreds) {
    return true;
  }

  return request->authenticate(_username.c_str(), _credentials.c_str(), _realm.c_str(), _hash);
}

void AsyncAuthenticationMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  return allowed(request) ? next() : request->requestAuthentication(_authMethod, _realm.c_str(), _authFailMsg.c_str());
}

void AsyncHeaderFreeMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  std::list<const char *> toRemove;
  for (auto &h : request->getHeaders()) {
    bool keep = false;
    for (const char *k : _toKeep) {
      if (strcasecmp(h.name().c_str(), k) == 0) {
        keep = true;
        break;
      }
    }
    if (!keep) {
      toRemove.push_back(h.name().c_str());
    }
  }
  for (const char *h : toRemove) {
    request->removeHeader(h);
  }
  next();
}

void AsyncHeaderFilterMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  for (auto it = _toRemove.begin(); it != _toRemove.end(); ++it) {
    request->removeHeader(*it);
  }
  next();
}

void AsyncLoggingMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  if (!isEnabled()) {
    next();
    return;
  }
  _out->print(F("* Connection from "));
#ifndef LIBRETINY
  _out->print(request->client()->remoteIP().toString());
#else
  _out->print(request->client()->remoteIP());
#endif
  _out->print(':');
  _out->println(request->client()->remotePort());
  _out->print('>');
  _out->print(' ');
  _out->print(request->methodToString());
  _out->print(' ');
  _out->print(request->url().c_str());
  _out->print(F(" HTTP/1."));
  _out->println(request->version());
  for (auto &h : request->getHeaders()) {
    if (h.value().length()) {
      _out->print('>');
      _out->print(' ');
      _out->print(h.name());
      _out->print(':');
      _out->print(' ');
      _out->println(h.value());
    }
  }
  _out->println(F(">"));
  uint32_t elapsed = millis();
  next();
  elapsed = millis() - elapsed;
  AsyncWebServerResponse *response = request->getResponse();
  if (response) {
    _out->print(F("* Processed in "));
    _out->print(elapsed);
    _out->println(F(" ms"));
    _out->print('<');
    _out->print(F(" HTTP/1."));
    _out->print(request->version());
    _out->print(' ');
    _out->print(response->code());
    _out->print(' ');
    _out->println(AsyncWebServerResponse::responseCodeToString(response->code()));
    for (auto &h : response->getHeaders()) {
      if (h.value().length()) {
        _out->print('<');
        _out->print(' ');
        _out->print(h.name());
        _out->print(':');
        _out->print(' ');
        _out->println(h.value());
      }
    }
    _out->println('<');
  } else {
    _out->println(F("* Connection closed!"));
  }
}

void AsyncCorsMiddleware::addCORSHeaders(AsyncWebServerResponse *response) {
  response->addHeader(asyncsrv::T_CORS_ACAO, _origin.c_str());
  response->addHeader(asyncsrv::T_CORS_ACAM, _methods.c_str());
  response->addHeader(asyncsrv::T_CORS_ACAH, _headers.c_str());
  response->addHeader(asyncsrv::T_CORS_ACAC, _credentials ? asyncsrv::T_TRUE : asyncsrv::T_FALSE);
  response->addHeader(asyncsrv::T_CORS_ACMA, String(_maxAge).c_str());
}

void AsyncCorsMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  // Origin header ? => CORS handling
  if (request->hasHeader(asyncsrv::T_CORS_O)) {
    // check if this is a preflight request => handle it and return
    if (request->method() == HTTP_OPTIONS) {
      AsyncWebServerResponse *response = request->beginResponse(200);
      addCORSHeaders(response);
      request->send(response);
      return;
    }

    // CORS request, no options => let the request pass and add CORS headers after
    next();
    AsyncWebServerResponse *response = request->getResponse();
    if (response) {
      addCORSHeaders(response);
    }

  } else {
    // NO Origin header => no CORS handling
    next();
  }
}

bool AsyncRateLimitMiddleware::isRequestAllowed(uint32_t &retryAfterSeconds) {
  uint32_t now = millis();

  while (!_requestTimes.empty() && _requestTimes.front() <= now - _windowSizeMillis) {
    _requestTimes.pop_front();
  }

  _requestTimes.push_back(now);

  if (_requestTimes.size() > _maxRequests) {
    _requestTimes.pop_front();
    retryAfterSeconds = (_windowSizeMillis - (now - _requestTimes.front())) / 1000 + 1;
    return false;
  }

  retryAfterSeconds = 0;
  return true;
}

void AsyncRateLimitMiddleware::run(AsyncWebServerRequest *request, ArMiddlewareNext next) {
  uint32_t retryAfterSeconds;
  if (isRequestAllowed(retryAfterSeconds)) {
    next();
  } else {
    AsyncWebServerResponse *response = request->beginResponse(429);
    response->addHeader(asyncsrv::T_retry_after, retryAfterSeconds);
    request->send(response);
  }
}

// *** ChunkPrint.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include <ChunkPrint.h>
// *** ChunkPrint.h ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef CHUNKPRINT_H
#define CHUNKPRINT_H

#include <Print.h>

class ChunkPrint : public Print {
private:
  uint8_t *_destination;
  size_t _to_skip;
  size_t _to_write;
  size_t _pos;

public:
  ChunkPrint(uint8_t *destination, size_t from, size_t len);
  size_t write(uint8_t c);
  size_t write(const uint8_t *buffer, size_t size) {
    return this->Print::write(buffer, size);
  }
};
#endif

ChunkPrint::ChunkPrint(uint8_t *destination, size_t from, size_t len) : _destination(destination), _to_skip(from), _to_write(len), _pos{0} {}

size_t ChunkPrint::write(uint8_t c) {
  if (_to_skip > 0) {
    _to_skip--;
    return 1;
  } else if (_to_write > 0) {
    _to_write--;
    _destination[_pos++] = c;
    return 1;
  }
  return 0;
}



// *** BackPort_SHA1Builder.cpp ***

/*
 *  FIPS-180-1 compliant SHA-1 implementation
 *
 *  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 *  Modified for esp32 by Lucas Saavedra Vaz on 11 Jan 2024
 */

//#include <Arduino.h>
#if ESP_IDF_VERSION_MAJOR < 5

//#include "BackPort_SHA1Builder.h"

// *** BackPort_SHA1Builder.h ***
// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//#include <Arduino.h>
#if ESP_IDF_VERSION_MAJOR < 5

#ifndef SHA1Builder_h
#define SHA1Builder_h

//#include <Stream.h>
//#include <WString.h>

#define SHA1_HASH_SIZE 20

class SHA1Builder {
private:
  uint32_t total[2];            /* number of bytes processed  */
  uint32_t state[5];            /* intermediate digest state  */
  unsigned char buffer[64];     /* data block being processed */
  uint8_t hash[SHA1_HASH_SIZE]; /* SHA-1 result               */

  void process(const uint8_t *data);

public:
  void begin();
  void add(const uint8_t *data, size_t len);
  void calculate();
  void getBytes(uint8_t *output);
};

#endif  // SHA1Builder_h

#endif  // ESP_IDF_VERSION_MAJOR < 5

// 32-bit integer manipulation macros (big endian)

#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n, b, i) \
  { (n) = ((uint32_t)(b)[(i)] << 24) | ((uint32_t)(b)[(i) + 1] << 16) | ((uint32_t)(b)[(i) + 2] << 8) | ((uint32_t)(b)[(i) + 3]); }
#endif

#ifndef PUT_UINT32_BE
#define PUT_UINT32_BE(n, b, i)           \
  {                                      \
    (b)[(i)] = (uint8_t)((n) >> 24);     \
    (b)[(i) + 1] = (uint8_t)((n) >> 16); \
    (b)[(i) + 2] = (uint8_t)((n) >> 8);  \
    (b)[(i) + 3] = (uint8_t)((n));       \
  }
#endif

// Constants

static const uint8_t sha1_padding[64] = {0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Private methods

void SHA1Builder::process(const uint8_t *data) {
  uint32_t temp, W[16], A, B, C, D, E;

  GET_UINT32_BE(W[0], data, 0);
  GET_UINT32_BE(W[1], data, 4);
  GET_UINT32_BE(W[2], data, 8);
  GET_UINT32_BE(W[3], data, 12);
  GET_UINT32_BE(W[4], data, 16);
  GET_UINT32_BE(W[5], data, 20);
  GET_UINT32_BE(W[6], data, 24);
  GET_UINT32_BE(W[7], data, 28);
  GET_UINT32_BE(W[8], data, 32);
  GET_UINT32_BE(W[9], data, 36);
  GET_UINT32_BE(W[10], data, 40);
  GET_UINT32_BE(W[11], data, 44);
  GET_UINT32_BE(W[12], data, 48);
  GET_UINT32_BE(W[13], data, 52);
  GET_UINT32_BE(W[14], data, 56);
  GET_UINT32_BE(W[15], data, 60);

#define sha1_S(x, n) ((x << n) | ((x & 0xFFFFFFFF) >> (32 - n)))

#define sha1_R(t) (temp = W[(t - 3) & 0x0F] ^ W[(t - 8) & 0x0F] ^ W[(t - 14) & 0x0F] ^ W[t & 0x0F], (W[t & 0x0F] = sha1_S(temp, 1)))

#define sha1_P(a, b, c, d, e, x)                      \
  {                                                   \
    e += sha1_S(a, 5) + sha1_F(b, c, d) + sha1_K + x; \
    b = sha1_S(b, 30);                                \
  }

  A = state[0];
  B = state[1];
  C = state[2];
  D = state[3];
  E = state[4];

#define sha1_F(x, y, z) (z ^ (x & (y ^ z)))
#define sha1_K          0x5A827999

  sha1_P(A, B, C, D, E, W[0]);
  sha1_P(E, A, B, C, D, W[1]);
  sha1_P(D, E, A, B, C, W[2]);
  sha1_P(C, D, E, A, B, W[3]);
  sha1_P(B, C, D, E, A, W[4]);
  sha1_P(A, B, C, D, E, W[5]);
  sha1_P(E, A, B, C, D, W[6]);
  sha1_P(D, E, A, B, C, W[7]);
  sha1_P(C, D, E, A, B, W[8]);
  sha1_P(B, C, D, E, A, W[9]);
  sha1_P(A, B, C, D, E, W[10]);
  sha1_P(E, A, B, C, D, W[11]);
  sha1_P(D, E, A, B, C, W[12]);
  sha1_P(C, D, E, A, B, W[13]);
  sha1_P(B, C, D, E, A, W[14]);
  sha1_P(A, B, C, D, E, W[15]);
  sha1_P(E, A, B, C, D, sha1_R(16));
  sha1_P(D, E, A, B, C, sha1_R(17));
  sha1_P(C, D, E, A, B, sha1_R(18));
  sha1_P(B, C, D, E, A, sha1_R(19));

#undef sha1_K
#undef sha1_F

#define sha1_F(x, y, z) (x ^ y ^ z)
#define sha1_K          0x6ED9EBA1

  sha1_P(A, B, C, D, E, sha1_R(20));
  sha1_P(E, A, B, C, D, sha1_R(21));
  sha1_P(D, E, A, B, C, sha1_R(22));
  sha1_P(C, D, E, A, B, sha1_R(23));
  sha1_P(B, C, D, E, A, sha1_R(24));
  sha1_P(A, B, C, D, E, sha1_R(25));
  sha1_P(E, A, B, C, D, sha1_R(26));
  sha1_P(D, E, A, B, C, sha1_R(27));
  sha1_P(C, D, E, A, B, sha1_R(28));
  sha1_P(B, C, D, E, A, sha1_R(29));
  sha1_P(A, B, C, D, E, sha1_R(30));
  sha1_P(E, A, B, C, D, sha1_R(31));
  sha1_P(D, E, A, B, C, sha1_R(32));
  sha1_P(C, D, E, A, B, sha1_R(33));
  sha1_P(B, C, D, E, A, sha1_R(34));
  sha1_P(A, B, C, D, E, sha1_R(35));
  sha1_P(E, A, B, C, D, sha1_R(36));
  sha1_P(D, E, A, B, C, sha1_R(37));
  sha1_P(C, D, E, A, B, sha1_R(38));
  sha1_P(B, C, D, E, A, sha1_R(39));

#undef sha1_K
#undef sha1_F

#define sha1_F(x, y, z) ((x & y) | (z & (x | y)))
#define sha1_K          0x8F1BBCDC

  sha1_P(A, B, C, D, E, sha1_R(40));
  sha1_P(E, A, B, C, D, sha1_R(41));
  sha1_P(D, E, A, B, C, sha1_R(42));
  sha1_P(C, D, E, A, B, sha1_R(43));
  sha1_P(B, C, D, E, A, sha1_R(44));
  sha1_P(A, B, C, D, E, sha1_R(45));
  sha1_P(E, A, B, C, D, sha1_R(46));
  sha1_P(D, E, A, B, C, sha1_R(47));
  sha1_P(C, D, E, A, B, sha1_R(48));
  sha1_P(B, C, D, E, A, sha1_R(49));
  sha1_P(A, B, C, D, E, sha1_R(50));
  sha1_P(E, A, B, C, D, sha1_R(51));
  sha1_P(D, E, A, B, C, sha1_R(52));
  sha1_P(C, D, E, A, B, sha1_R(53));
  sha1_P(B, C, D, E, A, sha1_R(54));
  sha1_P(A, B, C, D, E, sha1_R(55));
  sha1_P(E, A, B, C, D, sha1_R(56));
  sha1_P(D, E, A, B, C, sha1_R(57));
  sha1_P(C, D, E, A, B, sha1_R(58));
  sha1_P(B, C, D, E, A, sha1_R(59));

#undef sha1_K
#undef sha1_F

#define sha1_F(x, y, z) (x ^ y ^ z)
#define sha1_K          0xCA62C1D6

  sha1_P(A, B, C, D, E, sha1_R(60));
  sha1_P(E, A, B, C, D, sha1_R(61));
  sha1_P(D, E, A, B, C, sha1_R(62));
  sha1_P(C, D, E, A, B, sha1_R(63));
  sha1_P(B, C, D, E, A, sha1_R(64));
  sha1_P(A, B, C, D, E, sha1_R(65));
  sha1_P(E, A, B, C, D, sha1_R(66));
  sha1_P(D, E, A, B, C, sha1_R(67));
  sha1_P(C, D, E, A, B, sha1_R(68));
  sha1_P(B, C, D, E, A, sha1_R(69));
  sha1_P(A, B, C, D, E, sha1_R(70));
  sha1_P(E, A, B, C, D, sha1_R(71));
  sha1_P(D, E, A, B, C, sha1_R(72));
  sha1_P(C, D, E, A, B, sha1_R(73));
  sha1_P(B, C, D, E, A, sha1_R(74));
  sha1_P(A, B, C, D, E, sha1_R(75));
  sha1_P(E, A, B, C, D, sha1_R(76));
  sha1_P(D, E, A, B, C, sha1_R(77));
  sha1_P(C, D, E, A, B, sha1_R(78));
  sha1_P(B, C, D, E, A, sha1_R(79));

#undef sha1_K
#undef sha1_F

  state[0] += A;
  state[1] += B;
  state[2] += C;
  state[3] += D;
  state[4] += E;
}

// Public methods

void SHA1Builder::begin() {
  total[0] = 0;
  total[1] = 0;

  state[0] = 0x67452301;
  state[1] = 0xEFCDAB89;
  state[2] = 0x98BADCFE;
  state[3] = 0x10325476;
  state[4] = 0xC3D2E1F0;

  memset(buffer, 0x00, sizeof(buffer));
  memset(hash, 0x00, sizeof(hash));
}

void SHA1Builder::add(const uint8_t *data, size_t len) {
  size_t fill;
  uint32_t left;

  if (len == 0) {
    return;
  }

  left = total[0] & 0x3F;
  fill = 64 - left;

  total[0] += (uint32_t)len;
  total[0] &= 0xFFFFFFFF;

  if (total[0] < (uint32_t)len) {
    total[1]++;
  }

  if (left && len >= fill) {
    memcpy((void *)(buffer + left), data, fill);
    process(buffer);
    data += fill;
    len -= fill;
    left = 0;
  }

  while (len >= 64) {
    process(data);
    data += 64;
    len -= 64;
  }

  if (len > 0) {
    memcpy((void *)(buffer + left), data, len);
  }
}

void SHA1Builder::calculate(void) {
  uint32_t last, padn;
  uint32_t high, low;
  uint8_t msglen[8];

  high = (total[0] >> 29) | (total[1] << 3);
  low = (total[0] << 3);

  PUT_UINT32_BE(high, msglen, 0);
  PUT_UINT32_BE(low, msglen, 4);

  last = total[0] & 0x3F;
  padn = (last < 56) ? (56 - last) : (120 - last);

  add((uint8_t *)sha1_padding, padn);
  add(msglen, 8);

  PUT_UINT32_BE(state[0], hash, 0);
  PUT_UINT32_BE(state[1], hash, 4);
  PUT_UINT32_BE(state[2], hash, 8);
  PUT_UINT32_BE(state[3], hash, 12);
  PUT_UINT32_BE(state[4], hash, 16);
}

void SHA1Builder::getBytes(uint8_t *output) {
  memcpy(output, hash, SHA1_HASH_SIZE);
}

#endif  // ESP_IDF_VERSION_MAJOR < 5


// *** AsyncWebSocket.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "AsyncWebSocket.h"
//#include "Arduino.h"

//#include <cstring>

#include <libb64/cencode.h>

#if defined(ESP32)
#if ESP_IDF_VERSION_MAJOR < 5
//#include "BackPort_SHA1Builder.h"
#else
#include <SHA1Builder.h>
#endif
#include <rom/ets_sys.h>
#elif defined(TARGET_RP2040) || defined(TARGET_RP2350) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(ESP8266)
#include <Hash.h>
#elif defined(LIBRETINY)
#include <mbedtls/sha1.h>
#endif

using namespace asyncsrv;

size_t webSocketSendFrameWindow(AsyncClient *client) {
  if (!client || !client->canSend()) {
    return 0;
  }
  size_t space = client->space();
  if (space < 9) {
    return 0;
  }
  return space - 8;
}

size_t webSocketSendFrame(AsyncClient *client, bool final, uint8_t opcode, bool mask, uint8_t *data, size_t len) {
  if (!client || !client->canSend()) {
    // Serial.println("SF 1");
    return 0;
  }
  size_t space = client->space();
  if (space < 2) {
    // Serial.println("SF 2");
    return 0;
  }
  uint8_t mbuf[4] = {0, 0, 0, 0};
  uint8_t headLen = 2;
  if (len && mask) {
    headLen += 4;
    mbuf[0] = rand() % 0xFF;
    mbuf[1] = rand() % 0xFF;
    mbuf[2] = rand() % 0xFF;
    mbuf[3] = rand() % 0xFF;
  }
  if (len > 125) {
    headLen += 2;
  }
  if (space < headLen) {
    // Serial.println("SF 2");
    return 0;
  }
  space -= headLen;

  if (len > space) {
    len = space;
  }

  uint8_t *buf = (uint8_t *)malloc(headLen);
  if (buf == NULL) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    client->abort();
    return 0;
  }

  buf[0] = opcode & 0x0F;
  if (final) {
    buf[0] |= 0x80;
  }
  if (len < 126) {
    buf[1] = len & 0x7F;
  } else {
    buf[1] = 126;
    buf[2] = (uint8_t)((len >> 8) & 0xFF);
    buf[3] = (uint8_t)(len & 0xFF);
  }
  if (len && mask) {
    buf[1] |= 0x80;
    memcpy(buf + (headLen - 4), mbuf, 4);
  }
  if (client->add((const char *)buf, headLen) != headLen) {
    // os_printf("error adding %lu header bytes\n", headLen);
    free(buf);
    // Serial.println("SF 4");
    return 0;
  }
  free(buf);

  if (len) {
    if (len && mask) {
      size_t i;
      for (i = 0; i < len; i++) {
        data[i] = data[i] ^ mbuf[i % 4];
      }
    }
    if (client->add((const char *)data, len) != len) {
      // os_printf("error adding %lu data bytes\n", len);
      //  Serial.println("SF 5");
      return 0;
    }
  }
  if (!client->send()) {
    // os_printf("error sending frame: %lu\n", headLen+len);
    //  Serial.println("SF 6");
    return 0;
  }
  // Serial.println("SF");
  return len;
}

/*
 *    AsyncWebSocketMessageBuffer
 */

AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer(const uint8_t *data, size_t size) : _buffer(std::make_shared<std::vector<uint8_t>>(size)) {
  if (_buffer->capacity() < size) {
    _buffer->reserve(size);
  } else {
    memcpy(_buffer->data(), data, size);
  }
}

AsyncWebSocketMessageBuffer::AsyncWebSocketMessageBuffer(size_t size) : _buffer(std::make_shared<std::vector<uint8_t>>(size)) {
  if (_buffer->capacity() < size) {
    _buffer->reserve(size);
  }
}

bool AsyncWebSocketMessageBuffer::reserve(size_t size) {
  if (_buffer->capacity() >= size) {
    return true;
  }
  _buffer->reserve(size);
  return _buffer->capacity() >= size;
}

/*
 * Control Frame
 */

class AsyncWebSocketControl {
private:
  uint8_t _opcode;
  uint8_t *_data;
  size_t _len;
  bool _mask;
  bool _finished;

public:
  AsyncWebSocketControl(uint8_t opcode, const uint8_t *data = NULL, size_t len = 0, bool mask = false)
    : _opcode(opcode), _len(len), _mask(len && mask), _finished(false) {
    if (data == NULL) {
      _len = 0;
    }
    if (_len) {
      if (_len > 125) {
        _len = 125;
      }

      _data = (uint8_t *)malloc(_len);

      if (_data == NULL) {
#ifdef ESP32
        log_e("Failed to allocate");
#endif
        _len = 0;
      } else {
        memcpy(_data, data, len);
      }
    } else {
      _data = NULL;
    }
  }

  ~AsyncWebSocketControl() {
    if (_data != NULL) {
      free(_data);
    }
  }

  bool finished() const {
    return _finished;
  }
  uint8_t opcode() {
    return _opcode;
  }
  uint8_t len() {
    return _len + 2;
  }
  size_t send(AsyncClient *client) {
    _finished = true;
    return webSocketSendFrame(client, true, _opcode & 0x0F, _mask, _data, _len);
  }
};

/*
 * AsyncWebSocketMessage Message
 */

AsyncWebSocketMessage::AsyncWebSocketMessage(AsyncWebSocketSharedBuffer buffer, uint8_t opcode, bool mask)
  : _WSbuffer{buffer}, _opcode(opcode & 0x07), _mask{mask}, _status{_WSbuffer ? WS_MSG_SENDING : WS_MSG_ERROR} {}

void AsyncWebSocketMessage::ack(size_t len, uint32_t time) {
  (void)time;
  _acked += len;
  if (_sent >= _WSbuffer->size() && _acked >= _ack) {
    _status = WS_MSG_SENT;
  }
  // ets_printf("A: %u\n", len);
}

size_t AsyncWebSocketMessage::send(AsyncClient *client) {
  if (!client) {
    return 0;
  }

  if (_status != WS_MSG_SENDING) {
    return 0;
  }
  if (_acked < _ack) {
    return 0;
  }
  if (_sent == _WSbuffer->size()) {
    if (_acked == _ack) {
      _status = WS_MSG_SENT;
    }
    return 0;
  }
  if (_sent > _WSbuffer->size()) {
    _status = WS_MSG_ERROR;
    // ets_printf("E: %u > %u\n", _sent, _WSbuffer->length());
    return 0;
  }

  size_t toSend = _WSbuffer->size() - _sent;
  size_t window = webSocketSendFrameWindow(client);

  if (window < toSend) {
    toSend = window;
  }

  _sent += toSend;
  _ack += toSend + ((toSend < 126) ? 2 : 4) + (_mask * 4);

  // ets_printf("W: %u %u\n", _sent - toSend, toSend);

  bool final = (_sent == _WSbuffer->size());
  uint8_t *dPtr = (uint8_t *)(_WSbuffer->data() + (_sent - toSend));
  uint8_t opCode = (toSend && _sent == toSend) ? _opcode : (uint8_t)WS_CONTINUATION;

  size_t sent = webSocketSendFrame(client, final, opCode, _mask, dPtr, toSend);
  _status = WS_MSG_SENDING;
  if (toSend && sent != toSend) {
    // ets_printf("E: %u != %u\n", toSend, sent);
    _sent -= (toSend - sent);
    _ack -= (toSend - sent);
  }
  // ets_printf("S: %u %u\n", _sent, sent);
  return sent;
}

/*
 * Async WebSocket Client
 */
const char *AWSC_PING_PAYLOAD = "ESPAsyncWebServer-PING";
const size_t AWSC_PING_PAYLOAD_LEN = 22;

AsyncWebSocketClient::AsyncWebSocketClient(AsyncWebServerRequest *request, AsyncWebSocket *server) : _tempObject(NULL) {
  _client = request->client();
  _server = server;
  _clientId = _server->_getNextId();
  _status = WS_CONNECTED;
  _pstate = 0;
  _lastMessageTime = millis();
  _keepAlivePeriod = 0;
  _client->setRxTimeout(0);
  _client->onError(
    [](void *r, AsyncClient *c, int8_t error) {
      (void)c;
      ((AsyncWebSocketClient *)(r))->_onError(error);
    },
    this
  );
  _client->onAck(
    [](void *r, AsyncClient *c, size_t len, uint32_t time) {
      (void)c;
      ((AsyncWebSocketClient *)(r))->_onAck(len, time);
    },
    this
  );
  _client->onDisconnect(
    [](void *r, AsyncClient *c) {
      ((AsyncWebSocketClient *)(r))->_onDisconnect();
      delete c;
    },
    this
  );
  _client->onTimeout(
    [](void *r, AsyncClient *c, uint32_t time) {
      (void)c;
      ((AsyncWebSocketClient *)(r))->_onTimeout(time);
    },
    this
  );
  _client->onData(
    [](void *r, AsyncClient *c, void *buf, size_t len) {
      (void)c;
      ((AsyncWebSocketClient *)(r))->_onData(buf, len);
    },
    this
  );
  _client->onPoll(
    [](void *r, AsyncClient *c) {
      (void)c;
      ((AsyncWebSocketClient *)(r))->_onPoll();
    },
    this
  );
  delete request;
  memset(&_pinfo, 0, sizeof(_pinfo));
}

AsyncWebSocketClient::~AsyncWebSocketClient() {
  {
#ifdef ESP32
    std::lock_guard<std::recursive_mutex> lock(_lock);
#endif
    _messageQueue.clear();
    _controlQueue.clear();
  }
  _server->_handleEvent(this, WS_EVT_DISCONNECT, NULL, NULL, 0);
}

void AsyncWebSocketClient::_clearQueue() {
  while (!_messageQueue.empty() && _messageQueue.front().finished()) {
    _messageQueue.pop_front();
  }
}

void AsyncWebSocketClient::_onAck(size_t len, uint32_t time) {
  _lastMessageTime = millis();

#ifdef ESP32
  std::unique_lock<std::recursive_mutex> lock(_lock);
#endif

  if (!_controlQueue.empty()) {
    auto &head = _controlQueue.front();
    if (head.finished()) {
      len -= head.len();
      if (_status == WS_DISCONNECTING && head.opcode() == WS_DISCONNECT) {
        _controlQueue.pop_front();
        _status = WS_DISCONNECTED;
        if (_client) {
#ifdef ESP32
          /*
            Unlocking has to be called before return execution otherwise std::unique_lock ::~unique_lock() will get an exception pthread_mutex_unlock.
            Due to _client->close(true) shall call the callback function _onDisconnect()
            The calling flow _onDisconnect() --> _handleDisconnect() --> ~AsyncWebSocketClient()
          */
          lock.unlock();
#endif
          _client->close(true);
        }
        return;
      }
      _controlQueue.pop_front();
    }
  }

  if (len && !_messageQueue.empty()) {
    _messageQueue.front().ack(len, time);
  }

  _clearQueue();

  _runQueue();
}

void AsyncWebSocketClient::_onPoll() {
  if (!_client) {
    return;
  }

#ifdef ESP32
  std::unique_lock<std::recursive_mutex> lock(_lock);
#endif
  if (_client && _client->canSend() && (!_controlQueue.empty() || !_messageQueue.empty())) {
    _runQueue();
  } else if (_keepAlivePeriod > 0 && (millis() - _lastMessageTime) >= _keepAlivePeriod && (_controlQueue.empty() && _messageQueue.empty())) {
#ifdef ESP32
    lock.unlock();
#endif
    ping((uint8_t *)AWSC_PING_PAYLOAD, AWSC_PING_PAYLOAD_LEN);
  }
}

void AsyncWebSocketClient::_runQueue() {
  // all calls to this method MUST be protected by a mutex lock!
  if (!_client) {
    return;
  }

  _clearQueue();

  if (!_controlQueue.empty() && (_messageQueue.empty() || _messageQueue.front().betweenFrames())
      && webSocketSendFrameWindow(_client) > (size_t)(_controlQueue.front().len() - 1)) {
    _controlQueue.front().send(_client);
  } else if (!_messageQueue.empty() && _messageQueue.front().betweenFrames() && webSocketSendFrameWindow(_client)) {
    _messageQueue.front().send(_client);
  }
}

bool AsyncWebSocketClient::queueIsFull() const {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_lock);
#endif
  return (_messageQueue.size() >= WS_MAX_QUEUED_MESSAGES) || (_status != WS_CONNECTED);
}

size_t AsyncWebSocketClient::queueLen() const {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_lock);
#endif
  return _messageQueue.size();
}

bool AsyncWebSocketClient::canSend() const {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_lock);
#endif
  return _messageQueue.size() < WS_MAX_QUEUED_MESSAGES;
}

bool AsyncWebSocketClient::_queueControl(uint8_t opcode, const uint8_t *data, size_t len, bool mask) {
  if (!_client) {
    return false;
  }

#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_lock);
#endif

  _controlQueue.emplace_back(opcode, data, len, mask);

  if (_client && _client->canSend()) {
    _runQueue();
  }

  return true;
}

bool AsyncWebSocketClient::_queueMessage(AsyncWebSocketSharedBuffer buffer, uint8_t opcode, bool mask) {
  if (!_client || buffer->size() == 0 || _status != WS_CONNECTED) {
    return false;
  }

#ifdef ESP32
  std::unique_lock<std::recursive_mutex> lock(_lock);
#endif

  if (_messageQueue.size() >= WS_MAX_QUEUED_MESSAGES) {
    if (closeWhenFull) {
      _status = WS_DISCONNECTED;

      if (_client) {
#ifdef ESP32
        /*
          Unlocking has to be called before return execution otherwise std::unique_lock ::~unique_lock() will get an exception pthread_mutex_unlock.
          Due to _client->close(true) shall call the callback function _onDisconnect()
          The calling flow _onDisconnect() --> _handleDisconnect() --> ~AsyncWebSocketClient()
        */
        lock.unlock();
#endif
        _client->close(true);
      }

#ifdef ESP8266
      ets_printf("AsyncWebSocketClient::_queueMessage: Too many messages queued: closing connection\n");
#elif defined(ESP32)
      log_e("Too many messages queued: closing connection");
#endif

    } else {
#ifdef ESP8266
      ets_printf("AsyncWebSocketClient::_queueMessage: Too many messages queued: discarding new message\n");
#elif defined(ESP32)
      log_e("Too many messages queued: discarding new message");
#endif
    }

    return false;
  }

  _messageQueue.emplace_back(buffer, opcode, mask);

  if (_client && _client->canSend()) {
    _runQueue();
  }

  return true;
}

void AsyncWebSocketClient::close(uint16_t code, const char *message) {
  if (_status != WS_CONNECTED) {
    return;
  }

  _status = WS_DISCONNECTING;

  if (code) {
    uint8_t packetLen = 2;
    if (message != NULL) {
      size_t mlen = strlen(message);
      if (mlen > 123) {
        mlen = 123;
      }
      packetLen += mlen;
    }
    char *buf = (char *)malloc(packetLen);
    if (buf != NULL) {
      buf[0] = (uint8_t)(code >> 8);
      buf[1] = (uint8_t)(code & 0xFF);
      if (message != NULL) {
        memcpy(buf + 2, message, packetLen - 2);
      }
      _queueControl(WS_DISCONNECT, (uint8_t *)buf, packetLen);
      free(buf);
      return;
    } else {
#ifdef ESP32
      log_e("Failed to allocate");
      _client->abort();
#endif
    }
  }
  _queueControl(WS_DISCONNECT);
}

bool AsyncWebSocketClient::ping(const uint8_t *data, size_t len) {
  return _status == WS_CONNECTED && _queueControl(WS_PING, data, len);
}

void AsyncWebSocketClient::_onError(int8_t) {
  // Serial.println("onErr");
}

void AsyncWebSocketClient::_onTimeout(uint32_t time) {
  if (!_client) {
    return;
  }
  // Serial.println("onTime");
  (void)time;
  _client->close(true);
}

void AsyncWebSocketClient::_onDisconnect() {
  // Serial.println("onDis");
  _client = nullptr;
  _server->_handleDisconnect(this);
}

void AsyncWebSocketClient::_onData(void *pbuf, size_t plen) {
  _lastMessageTime = millis();
  uint8_t *data = (uint8_t *)pbuf;
  while (plen > 0) {
    if (!_pstate) {
      const uint8_t *fdata = data;

      _pinfo.index = 0;
      _pinfo.final = (fdata[0] & 0x80) != 0;
      _pinfo.opcode = fdata[0] & 0x0F;
      _pinfo.masked = (fdata[1] & 0x80) != 0;
      _pinfo.len = fdata[1] & 0x7F;

      // log_d("WS[%" PRIu32 "]: _onData: %" PRIu32, _clientId, plen);
      // log_d("WS[%" PRIu32 "]: _status = %" PRIu32, _clientId, _status);
      // log_d("WS[%" PRIu32 "]: _pinfo: index: %" PRIu64 ", final: %" PRIu8 ", opcode: %" PRIu8 ", masked: %" PRIu8 ", len: %" PRIu64, _clientId, _pinfo.index, _pinfo.final, _pinfo.opcode, _pinfo.masked, _pinfo.len);

      data += 2;
      plen -= 2;

      if (_pinfo.len == 126 && plen >= 2) {
        _pinfo.len = fdata[3] | (uint16_t)(fdata[2]) << 8;
        data += 2;
        plen -= 2;

      } else if (_pinfo.len == 127 && plen >= 8) {
        _pinfo.len = fdata[9] | (uint16_t)(fdata[8]) << 8 | (uint32_t)(fdata[7]) << 16 | (uint32_t)(fdata[6]) << 24 | (uint64_t)(fdata[5]) << 32
                     | (uint64_t)(fdata[4]) << 40 | (uint64_t)(fdata[3]) << 48 | (uint64_t)(fdata[2]) << 56;
        data += 8;
        plen -= 8;
      }

      if (_pinfo.masked
          && plen >= 4) {  // if ws.close() is called, Safari sends a close frame with plen 2 and masked bit set. We must not decrement plen which is already 0.
        memcpy(_pinfo.mask, data, 4);
        data += 4;
        plen -= 4;
      }
    }

    const size_t datalen = std::min((size_t)(_pinfo.len - _pinfo.index), plen);
    const auto datalast = data[datalen];

    if (_pinfo.masked) {
      for (size_t i = 0; i < datalen; i++) {
        data[i] ^= _pinfo.mask[(_pinfo.index + i) % 4];
      }
    }

    if ((datalen + _pinfo.index) < _pinfo.len) {
      _pstate = 1;

      if (_pinfo.index == 0) {
        if (_pinfo.opcode) {
          _pinfo.message_opcode = _pinfo.opcode;
          _pinfo.num = 0;
        }
      }
      if (datalen > 0) {
        _server->_handleEvent(this, WS_EVT_DATA, (void *)&_pinfo, data, datalen);
      }

      _pinfo.index += datalen;
    } else if ((datalen + _pinfo.index) == _pinfo.len) {
      _pstate = 0;
      if (_pinfo.opcode == WS_DISCONNECT) {
        if (datalen) {
          uint16_t reasonCode = (uint16_t)(data[0] << 8) + data[1];
          char *reasonString = (char *)(data + 2);
          if (reasonCode > 1001) {
            _server->_handleEvent(this, WS_EVT_ERROR, (void *)&reasonCode, (uint8_t *)reasonString, strlen(reasonString));
          }
        }
        if (_status == WS_DISCONNECTING) {
          _status = WS_DISCONNECTED;
          if (_client) {
            _client->close(true);
          }
        } else {
          _status = WS_DISCONNECTING;
          if (_client) {
            _client->ackLater();
          }
          _queueControl(WS_DISCONNECT, data, datalen);
        }
      } else if (_pinfo.opcode == WS_PING) {
        _server->_handleEvent(this, WS_EVT_PING, NULL, NULL, 0);
        _queueControl(WS_PONG, data, datalen);
      } else if (_pinfo.opcode == WS_PONG) {
        if (datalen != AWSC_PING_PAYLOAD_LEN || memcmp(AWSC_PING_PAYLOAD, data, AWSC_PING_PAYLOAD_LEN) != 0) {
          _server->_handleEvent(this, WS_EVT_PONG, NULL, NULL, 0);
        }
      } else if (_pinfo.opcode < WS_DISCONNECT) {  // continuation or text/binary frame
        _server->_handleEvent(this, WS_EVT_DATA, (void *)&_pinfo, data, datalen);
        if (_pinfo.final) {
          _pinfo.num = 0;
        } else {
          _pinfo.num += 1;
        }
      }
    } else {
      // os_printf("frame error: len: %u, index: %llu, total: %llu\n", datalen, _pinfo.index, _pinfo.len);
      // what should we do?
      break;
    }

    // restore byte as _handleEvent may have added a null terminator i.e., data[len] = 0;
    if (datalen) {
      data[datalen] = datalast;
    }

    data += datalen;
    plen -= datalen;
  }
}

size_t AsyncWebSocketClient::printf(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  size_t len = vsnprintf(nullptr, 0, format, arg);
  va_end(arg);

  if (len == 0) {
    return 0;
  }

  char *buffer = new char[len + 1];

  if (!buffer) {
    return 0;
  }

  va_start(arg, format);
  len = vsnprintf(buffer, len + 1, format, arg);
  va_end(arg);

  bool enqueued = text(buffer, len);
  delete[] buffer;
  return enqueued ? len : 0;
}

#ifdef ESP8266
size_t AsyncWebSocketClient::printf_P(PGM_P formatP, ...) {
  va_list arg;
  va_start(arg, formatP);
  size_t len = vsnprintf_P(nullptr, 0, formatP, arg);
  va_end(arg);

  if (len == 0) {
    return 0;
  }

  char *buffer = new char[len + 1];

  if (!buffer) {
    return 0;
  }

  va_start(arg, formatP);
  len = vsnprintf_P(buffer, len + 1, formatP, arg);
  va_end(arg);

  bool enqueued = text(buffer, len);
  delete[] buffer;
  return enqueued ? len : 0;
}
#endif

namespace {
AsyncWebSocketSharedBuffer makeSharedBuffer(const uint8_t *message, size_t len) {
  auto buffer = std::make_shared<std::vector<uint8_t>>(len);
  memcpy(buffer->data(), message, len);
  return buffer;
}
}  // namespace

bool AsyncWebSocketClient::text(AsyncWebSocketMessageBuffer *buffer) {
  bool enqueued = false;
  if (buffer) {
    enqueued = text(std::move(buffer->_buffer));
    delete buffer;
  }
  return enqueued;
}

bool AsyncWebSocketClient::text(AsyncWebSocketSharedBuffer buffer) {
  return _queueMessage(buffer);
}

bool AsyncWebSocketClient::text(const uint8_t *message, size_t len) {
  return text(makeSharedBuffer(message, len));
}

bool AsyncWebSocketClient::text(const char *message, size_t len) {
  return text((const uint8_t *)message, len);
}

bool AsyncWebSocketClient::text(const char *message) {
  return text(message, strlen(message));
}

bool AsyncWebSocketClient::text(const String &message) {
  return text(message.c_str(), message.length());
}

#ifdef ESP8266
bool AsyncWebSocketClient::text(const __FlashStringHelper *data) {
  PGM_P p = reinterpret_cast<PGM_P>(data);

  size_t n = 0;
  while (1) {
    if (pgm_read_byte(p + n) == 0) {
      break;
    }
    n += 1;
  }

  char *message = (char *)malloc(n + 1);
  bool enqueued = false;
  if (message) {
    memcpy_P(message, p, n);
    message[n] = 0;
    enqueued = text(message, n);
    free(message);
  }
  return enqueued;
}
#endif  // ESP8266

bool AsyncWebSocketClient::binary(AsyncWebSocketMessageBuffer *buffer) {
  bool enqueued = false;
  if (buffer) {
    enqueued = binary(std::move(buffer->_buffer));
    delete buffer;
  }
  return enqueued;
}

bool AsyncWebSocketClient::binary(AsyncWebSocketSharedBuffer buffer) {
  return _queueMessage(buffer, WS_BINARY);
}

bool AsyncWebSocketClient::binary(const uint8_t *message, size_t len) {
  return binary(makeSharedBuffer(message, len));
}

bool AsyncWebSocketClient::binary(const char *message, size_t len) {
  return binary((const uint8_t *)message, len);
}

bool AsyncWebSocketClient::binary(const char *message) {
  return binary(message, strlen(message));
}

bool AsyncWebSocketClient::binary(const String &message) {
  return binary(message.c_str(), message.length());
}

#ifdef ESP8266
bool AsyncWebSocketClient::binary(const __FlashStringHelper *data, size_t len) {
  PGM_P p = reinterpret_cast<PGM_P>(data);
  char *message = (char *)malloc(len);
  bool enqueued = false;
  if (message) {
    memcpy_P(message, p, len);
    enqueued = binary(message, len);
    free(message);
  }
  return enqueued;
}
#endif

IPAddress AsyncWebSocketClient::remoteIP() const {
  if (!_client) {
    return IPAddress((uint32_t)0U);
  }

  return _client->remoteIP();
}

uint16_t AsyncWebSocketClient::remotePort() const {
  if (!_client) {
    return 0;
  }

  return _client->remotePort();
}

/*
 * Async Web Socket - Each separate socket location
 */

void AsyncWebSocket::_handleEvent(AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (_eventHandler != NULL) {
    _eventHandler(this, client, type, arg, data, len);
  }
}

AsyncWebSocketClient *AsyncWebSocket::_newClient(AsyncWebServerRequest *request) {
  _clients.emplace_back(request, this);
  _handleEvent(&_clients.back(), WS_EVT_CONNECT, request, NULL, 0);
  return &_clients.back();
}

void AsyncWebSocket::_handleDisconnect(AsyncWebSocketClient *client) {
  const auto client_id = client->id();
  const auto iter = std::find_if(std::begin(_clients), std::end(_clients), [client_id](const AsyncWebSocketClient &c) {
    return c.id() == client_id;
  });
  if (iter != std::end(_clients)) {
    _clients.erase(iter);
  }
}

bool AsyncWebSocket::availableForWriteAll() {
  return std::none_of(std::begin(_clients), std::end(_clients), [](const AsyncWebSocketClient &c) {
    return c.queueIsFull();
  });
}

bool AsyncWebSocket::availableForWrite(uint32_t id) {
  const auto iter = std::find_if(std::begin(_clients), std::end(_clients), [id](const AsyncWebSocketClient &c) {
    return c.id() == id;
  });
  if (iter == std::end(_clients)) {
    return true;
  }
  return !iter->queueIsFull();
}

size_t AsyncWebSocket::count() const {
  return std::count_if(std::begin(_clients), std::end(_clients), [](const AsyncWebSocketClient &c) {
    return c.status() == WS_CONNECTED;
  });
}

AsyncWebSocketClient *AsyncWebSocket::client(uint32_t id) {
  const auto iter = std::find_if(_clients.begin(), _clients.end(), [id](const AsyncWebSocketClient &c) {
    return c.id() == id && c.status() == WS_CONNECTED;
  });
  if (iter == std::end(_clients)) {
    return nullptr;
  }

  return &(*iter);
}

void AsyncWebSocket::close(uint32_t id, uint16_t code, const char *message) {
  if (AsyncWebSocketClient *c = client(id)) {
    c->close(code, message);
  }
}

void AsyncWebSocket::closeAll(uint16_t code, const char *message) {
  for (auto &c : _clients) {
    if (c.status() == WS_CONNECTED) {
      c.close(code, message);
    }
  }
}

void AsyncWebSocket::cleanupClients(uint16_t maxClients) {
  if (count() > maxClients) {
    _clients.front().close();
  }

  for (auto i = _clients.begin(); i != _clients.end(); ++i) {
    if (i->shouldBeDeleted()) {
      _clients.erase(i);
      break;
    }
  }
}

bool AsyncWebSocket::ping(uint32_t id, const uint8_t *data, size_t len) {
  AsyncWebSocketClient *c = client(id);
  return c && c->ping(data, len);
}

AsyncWebSocket::SendStatus AsyncWebSocket::pingAll(const uint8_t *data, size_t len) {
  size_t hit = 0;
  size_t miss = 0;
  for (auto &c : _clients) {
    if (c.status() == WS_CONNECTED && c.ping(data, len)) {
      hit++;
    } else {
      miss++;
    }
  }
  return hit == 0 ? DISCARDED : (miss == 0 ? ENQUEUED : PARTIALLY_ENQUEUED);
}

bool AsyncWebSocket::text(uint32_t id, const uint8_t *message, size_t len) {
  AsyncWebSocketClient *c = client(id);
  return c && c->text(makeSharedBuffer(message, len));
}
bool AsyncWebSocket::text(uint32_t id, const char *message, size_t len) {
  return text(id, (const uint8_t *)message, len);
}
bool AsyncWebSocket::text(uint32_t id, const char *message) {
  return text(id, message, strlen(message));
}
bool AsyncWebSocket::text(uint32_t id, const String &message) {
  return text(id, message.c_str(), message.length());
}

#ifdef ESP8266
bool AsyncWebSocket::text(uint32_t id, const __FlashStringHelper *data) {
  PGM_P p = reinterpret_cast<PGM_P>(data);

  size_t n = 0;
  while (true) {
    if (pgm_read_byte(p + n) == 0) {
      break;
    }
    n += 1;
  }

  char *message = (char *)malloc(n + 1);
  bool enqueued = false;
  if (message) {
    memcpy_P(message, p, n);
    message[n] = 0;
    enqueued = text(id, message, n);
    free(message);
  }
  return enqueued;
}
#endif  // ESP8266

bool AsyncWebSocket::text(uint32_t id, AsyncWebSocketMessageBuffer *buffer) {
  bool enqueued = false;
  if (buffer) {
    enqueued = text(id, std::move(buffer->_buffer));
    delete buffer;
  }
  return enqueued;
}
bool AsyncWebSocket::text(uint32_t id, AsyncWebSocketSharedBuffer buffer) {
  AsyncWebSocketClient *c = client(id);
  return c && c->text(buffer);
}

AsyncWebSocket::SendStatus AsyncWebSocket::textAll(const uint8_t *message, size_t len) {
  return textAll(makeSharedBuffer(message, len));
}
AsyncWebSocket::SendStatus AsyncWebSocket::textAll(const char *message, size_t len) {
  return textAll((const uint8_t *)message, len);
}
AsyncWebSocket::SendStatus AsyncWebSocket::textAll(const char *message) {
  return textAll(message, strlen(message));
}
AsyncWebSocket::SendStatus AsyncWebSocket::textAll(const String &message) {
  return textAll(message.c_str(), message.length());
}
#ifdef ESP8266
AsyncWebSocket::SendStatus AsyncWebSocket::textAll(const __FlashStringHelper *data) {
  PGM_P p = reinterpret_cast<PGM_P>(data);

  size_t n = 0;
  while (1) {
    if (pgm_read_byte(p + n) == 0) {
      break;
    }
    n += 1;
  }

  char *message = (char *)malloc(n + 1);
  AsyncWebSocket::SendStatus status = DISCARDED;
  if (message) {
    memcpy_P(message, p, n);
    message[n] = 0;
    status = textAll(message, n);
    free(message);
  }
  return status;
}
#endif  // ESP8266
AsyncWebSocket::SendStatus AsyncWebSocket::textAll(AsyncWebSocketMessageBuffer *buffer) {
  AsyncWebSocket::SendStatus status = DISCARDED;
  if (buffer) {
    status = textAll(std::move(buffer->_buffer));
    delete buffer;
  }
  return status;
}

AsyncWebSocket::SendStatus AsyncWebSocket::textAll(AsyncWebSocketSharedBuffer buffer) {
  size_t hit = 0;
  size_t miss = 0;
  for (auto &c : _clients) {
    if (c.status() == WS_CONNECTED && c.text(buffer)) {
      hit++;
    } else {
      miss++;
    }
  }
  return hit == 0 ? DISCARDED : (miss == 0 ? ENQUEUED : PARTIALLY_ENQUEUED);
}

bool AsyncWebSocket::binary(uint32_t id, const uint8_t *message, size_t len) {
  AsyncWebSocketClient *c = client(id);
  return c && c->binary(makeSharedBuffer(message, len));
}
bool AsyncWebSocket::binary(uint32_t id, const char *message, size_t len) {
  return binary(id, (const uint8_t *)message, len);
}
bool AsyncWebSocket::binary(uint32_t id, const char *message) {
  return binary(id, message, strlen(message));
}
bool AsyncWebSocket::binary(uint32_t id, const String &message) {
  return binary(id, message.c_str(), message.length());
}

#ifdef ESP8266
bool AsyncWebSocket::binary(uint32_t id, const __FlashStringHelper *data, size_t len) {
  PGM_P p = reinterpret_cast<PGM_P>(data);
  char *message = (char *)malloc(len);
  bool enqueued = false;
  if (message) {
    memcpy_P(message, p, len);
    enqueued = binary(id, message, len);
    free(message);
  }
  return enqueued;
}
#endif  // ESP8266

bool AsyncWebSocket::binary(uint32_t id, AsyncWebSocketMessageBuffer *buffer) {
  bool enqueued = false;
  if (buffer) {
    enqueued = binary(id, std::move(buffer->_buffer));
    delete buffer;
  }
  return enqueued;
}
bool AsyncWebSocket::binary(uint32_t id, AsyncWebSocketSharedBuffer buffer) {
  AsyncWebSocketClient *c = client(id);
  return c && c->binary(buffer);
}

AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(const uint8_t *message, size_t len) {
  return binaryAll(makeSharedBuffer(message, len));
}
AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(const char *message, size_t len) {
  return binaryAll((const uint8_t *)message, len);
}
AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(const char *message) {
  return binaryAll(message, strlen(message));
}
AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(const String &message) {
  return binaryAll(message.c_str(), message.length());
}

#ifdef ESP8266
AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(const __FlashStringHelper *data, size_t len) {
  PGM_P p = reinterpret_cast<PGM_P>(data);
  char *message = (char *)malloc(len);
  AsyncWebSocket::SendStatus status = DISCARDED;
  if (message) {
    memcpy_P(message, p, len);
    status = binaryAll(message, len);
    free(message);
  }
  return status;
}
#endif  // ESP8266

AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(AsyncWebSocketMessageBuffer *buffer) {
  AsyncWebSocket::SendStatus status = DISCARDED;
  if (buffer) {
    status = binaryAll(std::move(buffer->_buffer));
    delete buffer;
  }
  return status;
}
AsyncWebSocket::SendStatus AsyncWebSocket::binaryAll(AsyncWebSocketSharedBuffer buffer) {
  size_t hit = 0;
  size_t miss = 0;
  for (auto &c : _clients) {
    if (c.status() == WS_CONNECTED && c.binary(buffer)) {
      hit++;
    } else {
      miss++;
    }
  }
  return hit == 0 ? DISCARDED : (miss == 0 ? ENQUEUED : PARTIALLY_ENQUEUED);
}

size_t AsyncWebSocket::printf(uint32_t id, const char *format, ...) {
  AsyncWebSocketClient *c = client(id);
  if (c) {
    va_list arg;
    va_start(arg, format);
    size_t len = c->printf(format, arg);
    va_end(arg);
    return len;
  }
  return 0;
}

size_t AsyncWebSocket::printfAll(const char *format, ...) {
  va_list arg;
  va_start(arg, format);
  size_t len = vsnprintf(nullptr, 0, format, arg);
  va_end(arg);

  if (len == 0) {
    return 0;
  }

  char *buffer = new char[len + 1];

  if (!buffer) {
    return 0;
  }

  va_start(arg, format);
  len = vsnprintf(buffer, len + 1, format, arg);
  va_end(arg);

  AsyncWebSocket::SendStatus status = textAll(buffer, len);
  delete[] buffer;
  return status == DISCARDED ? 0 : len;
}

#ifdef ESP8266
size_t AsyncWebSocket::printf_P(uint32_t id, PGM_P formatP, ...) {
  AsyncWebSocketClient *c = client(id);
  if (c != NULL) {
    va_list arg;
    va_start(arg, formatP);
    size_t len = c->printf_P(formatP, arg);
    va_end(arg);
    return len;
  }
  return 0;
}

size_t AsyncWebSocket::printfAll_P(PGM_P formatP, ...) {
  va_list arg;
  va_start(arg, formatP);
  size_t len = vsnprintf_P(nullptr, 0, formatP, arg);
  va_end(arg);

  if (len == 0) {
    return 0;
  }

  char *buffer = new char[len + 1];

  if (!buffer) {
    return 0;
  }

  va_start(arg, formatP);
  len = vsnprintf_P(buffer, len + 1, formatP, arg);
  va_end(arg);

  AsyncWebSocket::SendStatus status = textAll(buffer, len);
  delete[] buffer;
  return status == DISCARDED ? 0 : len;
}
#endif

const char __WS_STR_CONNECTION[] PROGMEM = {"Connection"};
const char __WS_STR_UPGRADE[] PROGMEM = {"Upgrade"};
const char __WS_STR_ORIGIN[] PROGMEM = {"Origin"};
const char __WS_STR_COOKIE[] PROGMEM = {"Cookie"};
const char __WS_STR_VERSION[] PROGMEM = {"Sec-WebSocket-Version"};
const char __WS_STR_KEY[] PROGMEM = {"Sec-WebSocket-Key"};
const char __WS_STR_PROTOCOL[] PROGMEM = {"Sec-WebSocket-Protocol"};
const char __WS_STR_ACCEPT[] PROGMEM = {"Sec-WebSocket-Accept"};
const char __WS_STR_UUID[] PROGMEM = {"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"};

#define WS_STR_UUID_LEN 36

#define WS_STR_CONNECTION FPSTR(__WS_STR_CONNECTION)
#define WS_STR_UPGRADE    FPSTR(__WS_STR_UPGRADE)
#define WS_STR_ORIGIN     FPSTR(__WS_STR_ORIGIN)
#define WS_STR_COOKIE     FPSTR(__WS_STR_COOKIE)
#define WS_STR_VERSION    FPSTR(__WS_STR_VERSION)
#define WS_STR_KEY        FPSTR(__WS_STR_KEY)
#define WS_STR_PROTOCOL   FPSTR(__WS_STR_PROTOCOL)
#define WS_STR_ACCEPT     FPSTR(__WS_STR_ACCEPT)
#define WS_STR_UUID       FPSTR(__WS_STR_UUID)

bool AsyncWebSocket::canHandle(AsyncWebServerRequest *request) const {
  return _enabled && request->isWebSocketUpgrade() && request->url().equals(_url);
}

void AsyncWebSocket::handleRequest(AsyncWebServerRequest *request) {
  if (!request->hasHeader(WS_STR_VERSION) || !request->hasHeader(WS_STR_KEY)) {
    request->send(400);
    return;
  }
  if (_handshakeHandler != nullptr) {
    if (!_handshakeHandler(request)) {
      request->send(401);
      return;
    }
  }
  const AsyncWebHeader *version = request->getHeader(WS_STR_VERSION);
  if (version->value().toInt() != 13) {
    AsyncWebServerResponse *response = request->beginResponse(400);
    response->addHeader(WS_STR_VERSION, T_13);
    request->send(response);
    return;
  }
  const AsyncWebHeader *key = request->getHeader(WS_STR_KEY);
  AsyncWebServerResponse *response = new AsyncWebSocketResponse(key->value(), this);
  if (response == NULL) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    request->abort();
    return;
  }
  if (request->hasHeader(WS_STR_PROTOCOL)) {
    const AsyncWebHeader *protocol = request->getHeader(WS_STR_PROTOCOL);
    // ToDo: check protocol
    response->addHeader(WS_STR_PROTOCOL, protocol->value());
  }
  request->send(response);
}

AsyncWebSocketMessageBuffer *AsyncWebSocket::makeBuffer(size_t size) {
  return new AsyncWebSocketMessageBuffer(size);
}

AsyncWebSocketMessageBuffer *AsyncWebSocket::makeBuffer(const uint8_t *data, size_t size) {
  return new AsyncWebSocketMessageBuffer(data, size);
}

/*
 * Response to Web Socket request - sends the authorization and detaches the TCP Client from the web server
 * Authentication code from https://github.com/Links2004/arduinoWebSockets/blob/master/src/WebSockets.cpp#L480
 */

AsyncWebSocketResponse::AsyncWebSocketResponse(const String &key, AsyncWebSocket *server) {
  _server = server;
  _code = 101;
  _sendContentLength = false;

  uint8_t hash[20];
  char buffer[33];

#if defined(ESP8266) || defined(TARGET_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(TARGET_RP2350)
  sha1(key + WS_STR_UUID, hash);
#else
  String k;
  if (!k.reserve(key.length() + WS_STR_UUID_LEN)) {
    log_e("Failed to allocate");
    return;
  }
  k.concat(key);
  k.concat(WS_STR_UUID);
#ifdef LIBRETINY
  mbedtls_sha1_context ctx;
  mbedtls_sha1_init(&ctx);
  mbedtls_sha1_starts(&ctx);
  mbedtls_sha1_update(&ctx, (const uint8_t *)k.c_str(), k.length());
  mbedtls_sha1_finish(&ctx, hash);
  mbedtls_sha1_free(&ctx);
#else
  SHA1Builder sha1;
  sha1.begin();
  sha1.add((const uint8_t *)k.c_str(), k.length());
  sha1.calculate();
  sha1.getBytes(hash);
#endif
#endif
  base64_encodestate _state;
  base64_init_encodestate(&_state);
  int len = base64_encode_block((const char *)hash, 20, buffer, &_state);
  len = base64_encode_blockend((buffer + len), &_state);
  addHeader(WS_STR_CONNECTION, WS_STR_UPGRADE);
  addHeader(WS_STR_UPGRADE, T_WS);
  addHeader(WS_STR_ACCEPT, buffer);
}

void AsyncWebSocketResponse::_respond(AsyncWebServerRequest *request) {
  if (_state == RESPONSE_FAILED) {
    request->client()->close(true);
    return;
  }
  String out;
  _assembleHead(out, request->version());
  request->client()->write(out.c_str(), _headLength);
  _state = RESPONSE_WAIT_ACK;
}

size_t AsyncWebSocketResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time) {
  (void)time;

  if (len) {
    _server->_newClient(request);
  }

  return 0;
}

// *** AsyncWebServerRequest.cpp ***
//#include <ESPAsyncWebServer.h>

/**
 * @brief Sends a file from the filesystem to the client, with optional gzip compression and ETag-based caching.
 *
 * This method serves files over HTTP from the provided filesystem. If a compressed version of the file
 * (with a `.gz` extension) exists and uncompressed version does not exist, it serves the compressed file.
 * It also handles ETag caching using the CRC32 value from the gzip trailer, responding with `304 Not Modified`
 * if the client's `If-None-Match` header matches the generated ETag.
 *
 * @param fs Reference to the filesystem (SPIFFS, LittleFS, etc.).
 * @param path Path to the file to be served.
 * @param contentType Optional MIME type of the file to be sent.
 *                    If contentType is "" it will be obtained from the file extension
 * @param download If true, forces the file to be sent as a download.
 * @param callback Optional template processor for dynamic content generation.
 *                 Templates will not be processed in compressed files.
 *
 * @note If neither the file nor its compressed version exists, responds with `404 Not Found`.
 */
void AsyncWebServerRequest::send(FS &fs, const String &path, const char *contentType, bool download, AwsTemplateProcessor callback) {
  // Check uncompressed file first
  if (fs.exists(path)) {
    send(beginResponse(fs, path, contentType, download, callback));
    return;
  }

  // Handle compressed version
  const String gzPath = path + asyncsrv::T__gz;
  File gzFile = fs.open(gzPath, "r");

  // Compressed file not found or invalid
  if (!gzFile.seek(gzFile.size() - 8)) {
    send(404);
    gzFile.close();
    return;
  }

  // ETag validation
  if (this->hasHeader(asyncsrv::T_INM)) {
    // Generate server ETag from CRC in gzip trailer
    uint8_t crcInTrailer[4];
    gzFile.read(crcInTrailer, 4);
    char serverETag[9];
    _getEtag(crcInTrailer, serverETag);

    // Compare with client's ETag
    const AsyncWebHeader *inmHeader = this->getHeader(asyncsrv::T_INM);
    if (inmHeader && inmHeader->value() == serverETag) {
      gzFile.close();
      this->send(304);  // Not Modified
      return;
    }
  }

  // Send compressed file response
  gzFile.close();
  send(beginResponse(fs, path, contentType, download, callback));
}

/**
 * @brief Generates an ETag string from a 4-byte trailer
 *
 * This function converts a 4-byte array into a hexadecimal ETag string enclosed in quotes.
 *
 * @param trailer[4] Input array of 4 bytes to convert to hexadecimal
 * @param serverETag Output buffer to store the ETag
 *                   Must be pre-allocated with minimum 9 bytes (8 hex + 1 null terminator)
 */
void AsyncWebServerRequest::_getEtag(uint8_t trailer[4], char *serverETag) {
  static constexpr char hexChars[] = "0123456789ABCDEF";

  uint32_t data;
  memcpy(&data, trailer, 4);

  serverETag[0] = hexChars[(data >> 4) & 0x0F];
  serverETag[1] = hexChars[data & 0x0F];
  serverETag[2] = hexChars[(data >> 12) & 0x0F];
  serverETag[3] = hexChars[(data >> 8) & 0x0F];
  serverETag[4] = hexChars[(data >> 20) & 0x0F];
  serverETag[5] = hexChars[(data >> 16) & 0x0F];
  serverETag[6] = hexChars[(data >> 28)];
  serverETag[7] = hexChars[(data >> 24) & 0x0F];
  serverETag[8] = '\0';
}

// *** AsyncWebHeader.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include <ESPAsyncWebServer.h>

const AsyncWebHeader AsyncWebHeader::parse(const char *data) {
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers
  // In HTTP/1.X, a header is a case-insensitive name followed by a colon, then optional whitespace which will be ignored, and finally by its value
  if (!data) {
    return AsyncWebHeader();  // nullptr
  }
  if (data[0] == '\0') {
    return AsyncWebHeader();  // empty string
  }
  if (strchr(data, '\n') || strchr(data, '\r')) {
    return AsyncWebHeader();  // Invalid header format
  }
  char *colon = strchr(data, ':');
  if (!colon) {
    return AsyncWebHeader();  // separator not found
  }
  if (colon == data) {
    return AsyncWebHeader();  // Header name cannot be empty
  }
  char *startOfValue = colon + 1;  // Skip the colon
  // skip one optional whitespace after the colon
  if (*startOfValue == ' ') {
    startOfValue++;
  }
  String name;
  name.reserve(colon - data);
  name.concat(data, colon - data);
  return AsyncWebHeader(name, String(startOfValue));
}


// *** AsyncMessagePack.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "AsyncMessagePack.h"
// *** AsyncMessagePack.h ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#pragma once

/*
   server.on("/msg_pack", HTTP_ANY, [](AsyncWebServerRequest * request) {
    AsyncMessagePackResponse * response = new AsyncMessagePackResponse();
    JsonObject& root = response->getRoot();
    root["key1"] = "key number one";
    JsonObject& nested = root.createNestedObject("nested");
    nested["key1"] = "key number one";
    response->setLength();
    request->send(response);
  });

  --------------------

  AsyncCallbackMessagePackWebHandler* handler = new AsyncCallbackMessagePackWebHandler("/msg_pack/endpoint");
  handler->onRequest([](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonObject jsonObj = json.as<JsonObject>();
    // ...
  });
  server.addHandler(handler);
*/

#if __has_include("ArduinoJson.h")
#include <ArduinoJson.h>
#if ARDUINOJSON_VERSION_MAJOR >= 6
#define ASYNC_MSG_PACK_SUPPORT 1
#else
#define ASYNC_MSG_PACK_SUPPORT 0
#endif  // ARDUINOJSON_VERSION_MAJOR >= 6
#endif  // __has_include("ArduinoJson.h")

#if ASYNC_MSG_PACK_SUPPORT == 1
//#include <ESPAsyncWebServer.h>

//#include "ChunkPrint.h"

#if ARDUINOJSON_VERSION_MAJOR == 6
#ifndef DYNAMIC_JSON_DOCUMENT_SIZE
#define DYNAMIC_JSON_DOCUMENT_SIZE 1024
#endif
#endif

class AsyncMessagePackResponse : public AsyncAbstractResponse {
protected:
#if ARDUINOJSON_VERSION_MAJOR == 6
  DynamicJsonDocument _jsonBuffer;
#else
  JsonDocument _jsonBuffer;
#endif

  JsonVariant _root;
  bool _isValid;

public:
#if ARDUINOJSON_VERSION_MAJOR == 6
  AsyncMessagePackResponse(bool isArray = false, size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE);
#else
  AsyncMessagePackResponse(bool isArray = false);
#endif
  JsonVariant &getRoot() {
    return _root;
  }
  bool _sourceValid() const {
    return _isValid;
  }
  size_t setLength();
  size_t getSize() const {
    return _jsonBuffer.size();
  }
  size_t _fillBuffer(uint8_t *data, size_t len);
#if ARDUINOJSON_VERSION_MAJOR >= 6
  bool overflowed() const {
    return _jsonBuffer.overflowed();
  }
#endif
};

typedef std::function<void(AsyncWebServerRequest *request, JsonVariant &json)> ArMessagePackRequestHandlerFunction;

class AsyncCallbackMessagePackWebHandler : public AsyncWebHandler {
protected:
  String _uri;
  WebRequestMethodComposite _method;
  ArMessagePackRequestHandlerFunction _onRequest;
  size_t _contentLength;
#if ARDUINOJSON_VERSION_MAJOR == 6
  size_t maxJsonBufferSize;
#endif
  size_t _maxContentLength;

public:
#if ARDUINOJSON_VERSION_MAJOR == 6
  AsyncCallbackMessagePackWebHandler(
    const String &uri, ArMessagePackRequestHandlerFunction onRequest = nullptr, size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE
  );
#else
  AsyncCallbackMessagePackWebHandler(const String &uri, ArMessagePackRequestHandlerFunction onRequest = nullptr);
#endif

  void setMethod(WebRequestMethodComposite method) {
    _method = method;
  }
  void setMaxContentLength(int maxContentLength) {
    _maxContentLength = maxContentLength;
  }
  void onRequest(ArMessagePackRequestHandlerFunction fn) {
    _onRequest = fn;
  }

  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
  void handleUpload(
    __unused AsyncWebServerRequest *request, __unused const String &filename, __unused size_t index, __unused uint8_t *data, __unused size_t len,
    __unused bool final
  ) override final {}
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override final;
  bool isRequestHandlerTrivial() const override final {
    return !_onRequest;
  }
};

#endif  // ASYNC_MSG_PACK_SUPPORT == 1



#if ASYNC_MSG_PACK_SUPPORT == 1

#if ARDUINOJSON_VERSION_MAJOR == 6
AsyncMessagePackResponse::AsyncMessagePackResponse(bool isArray, size_t maxJsonBufferSize) : _jsonBuffer(maxJsonBufferSize), _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_msgpack;
  if (isArray) {
    _root = _jsonBuffer.createNestedArray();
  } else {
    _root = _jsonBuffer.createNestedObject();
  }
}
#else
AsyncMessagePackResponse::AsyncMessagePackResponse(bool isArray) : _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_msgpack;
  if (isArray) {
    _root = _jsonBuffer.add<JsonArray>();
  } else {
    _root = _jsonBuffer.add<JsonObject>();
  }
}
#endif

size_t AsyncMessagePackResponse::setLength() {
  _contentLength = measureMsgPack(_root);
  if (_contentLength) {
    _isValid = true;
  }
  return _contentLength;
}

size_t AsyncMessagePackResponse::_fillBuffer(uint8_t *data, size_t len) {
  ChunkPrint dest(data, _sentLength, len);
  serializeMsgPack(_root, dest);
  return len;
}

#if ARDUINOJSON_VERSION_MAJOR == 6
AsyncCallbackMessagePackWebHandler::AsyncCallbackMessagePackWebHandler(
  const String &uri, ArMessagePackRequestHandlerFunction onRequest, size_t maxJsonBufferSize
)
  : _uri(uri), _method(HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), maxJsonBufferSize(maxJsonBufferSize), _maxContentLength(16384) {}
#else
AsyncCallbackMessagePackWebHandler::AsyncCallbackMessagePackWebHandler(const String &uri, ArMessagePackRequestHandlerFunction onRequest)
  : _uri(uri), _method(HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), _maxContentLength(16384) {}
#endif

bool AsyncCallbackMessagePackWebHandler::canHandle(AsyncWebServerRequest *request) const {
  if (!_onRequest || !request->isHTTP() || !(_method & request->method())) {
    return false;
  }

  if (_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri + "/"))) {
    return false;
  }

  if (request->method() != HTTP_GET && !request->contentType().equalsIgnoreCase(asyncsrv::T_application_msgpack)) {
    return false;
  }

  return true;
}

void AsyncCallbackMessagePackWebHandler::handleRequest(AsyncWebServerRequest *request) {
  if (_onRequest) {
    if (request->method() == HTTP_GET) {
      JsonVariant json;
      _onRequest(request, json);
      return;
    } else if (request->_tempObject != NULL) {

#if ARDUINOJSON_VERSION_MAJOR == 6
      DynamicJsonDocument jsonBuffer(this->maxJsonBufferSize);
      DeserializationError error = deserializeMsgPack(jsonBuffer, (uint8_t *)(request->_tempObject));
      if (!error) {
        JsonVariant json = jsonBuffer.as<JsonVariant>();
#else
      JsonDocument jsonBuffer;
      DeserializationError error = deserializeMsgPack(jsonBuffer, (uint8_t *)(request->_tempObject));
      if (!error) {
        JsonVariant json = jsonBuffer.as<JsonVariant>();
#endif

        _onRequest(request, json);
        return;
      }
    }
    request->send(_contentLength > _maxContentLength ? 413 : 400);
  } else {
    request->send(500);
  }
}

void AsyncCallbackMessagePackWebHandler::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (_onRequest) {
    _contentLength = total;
    if (total > 0 && request->_tempObject == NULL && total < _maxContentLength) {
      request->_tempObject = malloc(total);
      if (request->_tempObject == NULL) {
#ifdef ESP32
        log_e("Failed to allocate");
#endif
        request->abort();
        return;
      }
    }
    if (request->_tempObject != NULL) {
      memcpy((uint8_t *)(request->_tempObject) + index, data, len);
    }
  }
}

#endif  // ASYNC_MSG_PACK_SUPPORT




// *** AsyncJson.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "AsyncJson.h"
// *** AsyncJson.h ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

#ifndef ASYNC_JSON_H_
#define ASYNC_JSON_H_

#if __has_include("ArduinoJson.h")
#include <ArduinoJson.h>
#if ARDUINOJSON_VERSION_MAJOR >= 5
#define ASYNC_JSON_SUPPORT 1
#else
#define ASYNC_JSON_SUPPORT 0
#endif  // ARDUINOJSON_VERSION_MAJOR >= 5
#endif  // __has_include("ArduinoJson.h")

#if ASYNC_JSON_SUPPORT == 1
//#include <ESPAsyncWebServer.h>

//#include "ChunkPrint.h"

#if ARDUINOJSON_VERSION_MAJOR == 6
#ifndef DYNAMIC_JSON_DOCUMENT_SIZE
#define DYNAMIC_JSON_DOCUMENT_SIZE 1024
#endif
#endif

class AsyncJsonResponse : public AsyncAbstractResponse {
protected:
#if ARDUINOJSON_VERSION_MAJOR == 5
  DynamicJsonBuffer _jsonBuffer;
#elif ARDUINOJSON_VERSION_MAJOR == 6
  DynamicJsonDocument _jsonBuffer;
#else
  JsonDocument _jsonBuffer;
#endif

  JsonVariant _root;
  bool _isValid;

public:
#if ARDUINOJSON_VERSION_MAJOR == 6
  AsyncJsonResponse(bool isArray = false, size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE);
#else
  AsyncJsonResponse(bool isArray = false);
#endif
  JsonVariant &getRoot() {
    return _root;
  }
  bool _sourceValid() const {
    return _isValid;
  }
  size_t setLength();
  size_t getSize() const {
    return _jsonBuffer.size();
  }
  size_t _fillBuffer(uint8_t *data, size_t len);
#if ARDUINOJSON_VERSION_MAJOR >= 6
  bool overflowed() const {
    return _jsonBuffer.overflowed();
  }
#endif
};

class PrettyAsyncJsonResponse : public AsyncJsonResponse {
public:
#if ARDUINOJSON_VERSION_MAJOR == 6
  PrettyAsyncJsonResponse(bool isArray = false, size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE);
#else
  PrettyAsyncJsonResponse(bool isArray = false);
#endif
  size_t setLength();
  size_t _fillBuffer(uint8_t *data, size_t len);
};

typedef std::function<void(AsyncWebServerRequest *request, JsonVariant &json)> ArJsonRequestHandlerFunction;

class AsyncCallbackJsonWebHandler : public AsyncWebHandler {
protected:
  String _uri;
  WebRequestMethodComposite _method;
  ArJsonRequestHandlerFunction _onRequest;
#if ARDUINOJSON_VERSION_MAJOR == 6
  size_t maxJsonBufferSize;
#endif
  size_t _maxContentLength;

public:
#if ARDUINOJSON_VERSION_MAJOR == 6
  AsyncCallbackJsonWebHandler(const String &uri, ArJsonRequestHandlerFunction onRequest = nullptr, size_t maxJsonBufferSize = DYNAMIC_JSON_DOCUMENT_SIZE);
#else
  AsyncCallbackJsonWebHandler(const String &uri, ArJsonRequestHandlerFunction onRequest = nullptr);
#endif

  void setMethod(WebRequestMethodComposite method) {
    _method = method;
  }
  void setMaxContentLength(int maxContentLength) {
    _maxContentLength = maxContentLength;
  }
  void onRequest(ArJsonRequestHandlerFunction fn) {
    _onRequest = fn;
  }

  bool canHandle(AsyncWebServerRequest *request) const override final;
  void handleRequest(AsyncWebServerRequest *request) override final;
  void handleUpload(
    __unused AsyncWebServerRequest *request, __unused const String &filename, __unused size_t index, __unused uint8_t *data, __unused size_t len,
    __unused bool final
  ) override final {}
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override final;
  bool isRequestHandlerTrivial() const override final {
    return !_onRequest;
  }
};

#endif  // ASYNC_JSON_SUPPORT == 1

#endif  // ASYNC_JSON_H_

#if ASYNC_JSON_SUPPORT == 1

#if ARDUINOJSON_VERSION_MAJOR == 5
AsyncJsonResponse::AsyncJsonResponse(bool isArray) : _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_json;
  if (isArray) {
    _root = _jsonBuffer.createArray();
  } else {
    _root = _jsonBuffer.createObject();
  }
}
#elif ARDUINOJSON_VERSION_MAJOR == 6
AsyncJsonResponse::AsyncJsonResponse(bool isArray, size_t maxJsonBufferSize) : _jsonBuffer(maxJsonBufferSize), _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_json;
  if (isArray) {
    _root = _jsonBuffer.createNestedArray();
  } else {
    _root = _jsonBuffer.createNestedObject();
  }
}
#else
AsyncJsonResponse::AsyncJsonResponse(bool isArray) : _isValid{false} {
  _code = 200;
  _contentType = asyncsrv::T_application_json;
  if (isArray) {
    _root = _jsonBuffer.add<JsonArray>();
  } else {
    _root = _jsonBuffer.add<JsonObject>();
  }
}
#endif

size_t AsyncJsonResponse::setLength() {
#if ARDUINOJSON_VERSION_MAJOR == 5
  _contentLength = _root.measureLength();
#else
  _contentLength = measureJson(_root);
#endif
  if (_contentLength) {
    _isValid = true;
  }
  return _contentLength;
}

size_t AsyncJsonResponse::_fillBuffer(uint8_t *data, size_t len) {
  ChunkPrint dest(data, _sentLength, len);
#if ARDUINOJSON_VERSION_MAJOR == 5
  _root.printTo(dest);
#else
  serializeJson(_root, dest);
#endif
  return len;
}

#if ARDUINOJSON_VERSION_MAJOR == 6
PrettyAsyncJsonResponse::PrettyAsyncJsonResponse(bool isArray, size_t maxJsonBufferSize) : AsyncJsonResponse{isArray, maxJsonBufferSize} {}
#else
PrettyAsyncJsonResponse::PrettyAsyncJsonResponse(bool isArray) : AsyncJsonResponse{isArray} {}
#endif

size_t PrettyAsyncJsonResponse::setLength() {
#if ARDUINOJSON_VERSION_MAJOR == 5
  _contentLength = _root.measurePrettyLength();
#else
  _contentLength = measureJsonPretty(_root);
#endif
  if (_contentLength) {
    _isValid = true;
  }
  return _contentLength;
}

size_t PrettyAsyncJsonResponse::_fillBuffer(uint8_t *data, size_t len) {
  ChunkPrint dest(data, _sentLength, len);
#if ARDUINOJSON_VERSION_MAJOR == 5
  _root.prettyPrintTo(dest);
#else
  serializeJsonPretty(_root, dest);
#endif
  return len;
}

#if ARDUINOJSON_VERSION_MAJOR == 6
AsyncCallbackJsonWebHandler::AsyncCallbackJsonWebHandler(const String &uri, ArJsonRequestHandlerFunction onRequest, size_t maxJsonBufferSize)
  : _uri(uri), _method(HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), maxJsonBufferSize(maxJsonBufferSize), _maxContentLength(16384) {}
#else
AsyncCallbackJsonWebHandler::AsyncCallbackJsonWebHandler(const String &uri, ArJsonRequestHandlerFunction onRequest)
  : _uri(uri), _method(HTTP_GET | HTTP_POST | HTTP_PUT | HTTP_PATCH), _onRequest(onRequest), _maxContentLength(16384) {}
#endif

bool AsyncCallbackJsonWebHandler::canHandle(AsyncWebServerRequest *request) const {
  if (!_onRequest || !request->isHTTP() || !(_method & request->method())) {
    return false;
  }

  if (_uri.length() && (_uri != request->url() && !request->url().startsWith(_uri + "/"))) {
    return false;
  }

  if (request->method() != HTTP_GET && !request->contentType().equalsIgnoreCase(asyncsrv::T_application_json)) {
    return false;
  }

  return true;
}

void AsyncCallbackJsonWebHandler::handleRequest(AsyncWebServerRequest *request) {
  if (_onRequest) {
    // GET request:
    if (request->method() == HTTP_GET) {
      JsonVariant json;
      _onRequest(request, json);
      return;
    }

    // POST / PUT / ... requests:
    // check if JSON body is too large, if it is, don't deserialize
    if (request->contentLength() > _maxContentLength) {
#ifdef ESP32
      log_e("Content length exceeds maximum allowed");
#endif
      request->send(413);
      return;
    }

    if (request->_tempObject == NULL) {
      // there is no body
      request->send(400);
      return;
    }

#if ARDUINOJSON_VERSION_MAJOR == 5
    DynamicJsonBuffer jsonBuffer;
    JsonVariant json = jsonBuffer.parse((const char *)request->_tempObject);
    if (json.success()) {
#elif ARDUINOJSON_VERSION_MAJOR == 6
    DynamicJsonDocument jsonBuffer(this->maxJsonBufferSize);
    DeserializationError error = deserializeJson(jsonBuffer, (const char *)request->_tempObject);
    if (!error) {
      JsonVariant json = jsonBuffer.as<JsonVariant>();
#else
    JsonDocument jsonBuffer;
    DeserializationError error = deserializeJson(jsonBuffer, (const char *)request->_tempObject);
    if (!error) {
      JsonVariant json = jsonBuffer.as<JsonVariant>();
#endif

      _onRequest(request, json);
    } else {
      // error parsing the body
      request->send(400);
    }
  }
}

void AsyncCallbackJsonWebHandler::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (_onRequest) {
    // ignore callback if size is larger than maxContentLength
    if (total > _maxContentLength) {
      return;
    }

    if (index == 0) {
      // this check allows request->_tempObject to be initialized from a middleware
      if (request->_tempObject == NULL) {
        request->_tempObject = calloc(total + 1, sizeof(uint8_t));  // null-terminated string
        if (request->_tempObject == NULL) {
#ifdef ESP32
          log_e("Failed to allocate");
#endif
          request->abort();
          return;
        }
      }
    }

    if (request->_tempObject != NULL) {
      uint8_t *buffer = (uint8_t *)request->_tempObject;
      memcpy(buffer + index, data, len);
    }
  }
}

#endif  // ASYNC_JSON_SUPPORT

// *** AsyncEventSource.cpp ***
// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2025 Hristo Gochkov, Mathieu Carbou, Emil Muratov

//#include "Arduino.h"
#if defined(ESP32)
#include <rom/ets_sys.h>
#endif
//#include "AsyncEventSource.h"

#define ASYNC_SSE_NEW_LINE_CHAR (char)0xa

using namespace asyncsrv;

static String generateEventMessage(const char *message, const char *event, uint32_t id, uint32_t reconnect) {
  String str;
  size_t len{0};
  if (message) {
    len += strlen(message);
  }

  if (event) {
    len += strlen(event);
  }

  len += 42;  // give it some overhead

  if (!str.reserve(len)) {
#ifdef ESP32
    log_e("Failed to allocate");
#endif
    return emptyString;
  }

  if (reconnect) {
    str += T_retry_;
    str += reconnect;
    str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'
  }

  if (id) {
    str += T_id__;
    str += id;
    str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'
  }

  if (event != NULL) {
    str += T_event_;
    str += event;
    str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'
  }

  if (!message) {
    return str;
  }

  size_t messageLen = strlen(message);
  char *lineStart = (char *)message;
  char *lineEnd;
  do {
    char *nextN = strchr(lineStart, '\n');
    char *nextR = strchr(lineStart, '\r');
    if (nextN == NULL && nextR == NULL) {
      // a message is a single-line string
      str += T_data_;
      str += message;
      str += T_nn;
      return str;
    }

    // a message is a multi-line string
    char *nextLine = NULL;
    if (nextN != NULL && nextR != NULL) {  // windows line-ending \r\n
      if (nextR + 1 == nextN) {
        // normal \r\n sequence
        lineEnd = nextR;
        nextLine = nextN + 1;
      } else {
        // some abnormal \n \r mixed sequence
        lineEnd = std::min(nextR, nextN);
        nextLine = lineEnd + 1;
      }
    } else if (nextN != NULL) {  // Unix/Mac OS X LF
      lineEnd = nextN;
      nextLine = nextN + 1;
    } else {  // some ancient garbage
      lineEnd = nextR;
      nextLine = nextR + 1;
    }

    str += T_data_;
    str.concat(lineStart, lineEnd - lineStart);
    str += ASYNC_SSE_NEW_LINE_CHAR;  // \n

    lineStart = nextLine;
  } while (lineStart < ((char *)message + messageLen));

  // append another \n to terminate message
  str += ASYNC_SSE_NEW_LINE_CHAR;  // '\n'

  return str;
}

// Message

size_t AsyncEventSourceMessage::ack(size_t len, __attribute__((unused)) uint32_t time) {
  // If the whole message is now acked...
  if (_acked + len > _data->length()) {
    // Return the number of extra bytes acked (they will be carried on to the next message)
    const size_t extra = _acked + len - _data->length();
    _acked = _data->length();
    return extra;
  }
  // Return that no extra bytes left.
  _acked += len;
  return 0;
}

size_t AsyncEventSourceMessage::write(AsyncClient *client) {
  if (!client) {
    return 0;
  }

  if (_sent >= _data->length() || !client->canSend()) {
    return 0;
  }

  size_t len = std::min(_data->length() - _sent, client->space());
  /*
    add() would call lwip's tcp_write() under the AsyncTCP hood with apiflags argument.
    By default apiflags=ASYNC_WRITE_FLAG_COPY
    we could have used apiflags with this flag unset to pass data by reference and avoid copy to socket buffer,
    but looks like it does not work for Arduino's lwip in ESP32/IDF
    it is enforced in https://github.com/espressif/esp-lwip/blob/0606eed9d8b98a797514fdf6eabb4daf1c8c8cd9/src/core/tcp_out.c#L422C5-L422C30
    if LWIP_NETIF_TX_SINGLE_PBUF is set, and it is set indeed in IDF
    https://github.com/espressif/esp-idf/blob/a0f798cfc4bbd624aab52b2c194d219e242d80c1/components/lwip/port/include/lwipopts.h#L744

    So let's just keep it enforced ASYNC_WRITE_FLAG_COPY and keep in mind that there is no zero-copy
  */
  size_t written = client->add(_data->c_str() + _sent, len, ASYNC_WRITE_FLAG_COPY);  //  ASYNC_WRITE_FLAG_MORE
  _sent += written;
  return written;
}

size_t AsyncEventSourceMessage::send(AsyncClient *client) {
  size_t sent = write(client);
  return sent && client->send() ? sent : 0;
}

// Client

AsyncEventSourceClient::AsyncEventSourceClient(AsyncWebServerRequest *request, AsyncEventSource *server) : _client(request->client()), _server(server) {

  if (request->hasHeader(T_Last_Event_ID)) {
    _lastId = atoi(request->getHeader(T_Last_Event_ID)->value().c_str());
  }

  _client->setRxTimeout(0);
  _client->onError(NULL, NULL);
  _client->onAck(
    [](void *r, AsyncClient *c, size_t len, uint32_t time) {
      (void)c;
      static_cast<AsyncEventSourceClient *>(r)->_onAck(len, time);
    },
    this
  );
  _client->onPoll(
    [](void *r, AsyncClient *c) {
      (void)c;
      static_cast<AsyncEventSourceClient *>(r)->_onPoll();
    },
    this
  );
  _client->onData(NULL, NULL);
  _client->onTimeout(
    [this](void *r, AsyncClient *c __attribute__((unused)), uint32_t time) {
      static_cast<AsyncEventSourceClient *>(r)->_onTimeout(time);
    },
    this
  );
  _client->onDisconnect(
    [this](void *r, AsyncClient *c) {
      static_cast<AsyncEventSourceClient *>(r)->_onDisconnect();
      delete c;
    },
    this
  );

  _server->_addClient(this);
  delete request;

  _client->setNoDelay(true);
}

AsyncEventSourceClient::~AsyncEventSourceClient() {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_lockmq);
#endif
  _messageQueue.clear();
  close();
}

bool AsyncEventSourceClient::_queueMessage(const char *message, size_t len) {
  if (_messageQueue.size() >= SSE_MAX_QUEUED_MESSAGES) {
#ifdef ESP8266
    ets_printf(String(F("ERROR: Too many messages queued\n")).c_str());
#elif defined(ESP32)
    log_e("Event message queue overflow: discard message");
#endif
    return false;
  }

#ifdef ESP32
  // length() is not thread-safe, thus acquiring the lock before this call..
  std::lock_guard<std::recursive_mutex> lock(_lockmq);
#endif

  _messageQueue.emplace_back(message, len);

  /*
    throttle queue run
    if Q is filled for >25% then network/CPU is congested, since there is no zero-copy mode for socket buff
    forcing Q run will only eat more heap ram and blow the buffer, let's just keep data in our own queue
    the queue will be processed at least on each onAck()/onPoll() call from AsyncTCP
  */
  if (_messageQueue.size() < SSE_MAX_QUEUED_MESSAGES >> 2 && _client->canSend()) {
    _runQueue();
  }

  return true;
}

bool AsyncEventSourceClient::_queueMessage(AsyncEvent_SharedData_t &&msg) {
  if (_messageQueue.size() >= SSE_MAX_QUEUED_MESSAGES) {
#ifdef ESP8266
    ets_printf(String(F("ERROR: Too many messages queued\n")).c_str());
#elif defined(ESP32)
    log_e("Event message queue overflow: discard message");
#endif
    return false;
  }

#ifdef ESP32
  // length() is not thread-safe, thus acquiring the lock before this call..
  std::lock_guard<std::recursive_mutex> lock(_lockmq);
#endif

  _messageQueue.emplace_back(std::move(msg));

  /*
    throttle queue run
    if Q is filled for >25% then network/CPU is congested, since there is no zero-copy mode for socket buff
    forcing Q run will only eat more heap ram and blow the buffer, let's just keep data in our own queue
    the queue will be processed at least on each onAck()/onPoll() call from AsyncTCP
  */
  if (_messageQueue.size() < SSE_MAX_QUEUED_MESSAGES >> 2 && _client->canSend()) {
    _runQueue();
  }
  return true;
}

void AsyncEventSourceClient::_onAck(size_t len __attribute__((unused)), uint32_t time __attribute__((unused))) {
#ifdef ESP32
  // Same here, acquiring the lock early
  std::lock_guard<std::recursive_mutex> lock(_lockmq);
#endif

  // adjust in-flight len
  if (len < _inflight) {
    _inflight -= len;
  } else {
    _inflight = 0;
  }

  // acknowledge as much messages's data as we got confirmed len from a AsyncTCP
  while (len && _messageQueue.size()) {
    len = _messageQueue.front().ack(len);
    if (_messageQueue.front().finished()) {
      // now we could release full ack'ed messages, we were keeping it unless send confirmed from AsyncTCP
      _messageQueue.pop_front();
    }
  }

  // try to send another batch of data
  if (_messageQueue.size()) {
    _runQueue();
  }
}

void AsyncEventSourceClient::_onPoll() {
  if (_messageQueue.size()) {
#ifdef ESP32
    // Same here, acquiring the lock early
    std::lock_guard<std::recursive_mutex> lock(_lockmq);
#endif
    _runQueue();
  }
}

void AsyncEventSourceClient::_onTimeout(uint32_t time __attribute__((unused))) {
  if (_client) {
    _client->close(true);
  }
}

void AsyncEventSourceClient::_onDisconnect() {
  if (!_client) {
    return;
  }
  _client = nullptr;
  _server->_handleDisconnect(this);
}

void AsyncEventSourceClient::close() {
  if (_client) {
    _client->close();
  }
}

bool AsyncEventSourceClient::send(const char *message, const char *event, uint32_t id, uint32_t reconnect) {
  if (!connected()) {
    return false;
  }
  return _queueMessage(std::make_shared<String>(generateEventMessage(message, event, id, reconnect)));
}

void AsyncEventSourceClient::_runQueue() {
  if (!_client) {
    return;
  }

  // there is no need to lock the mutex here, 'cause all the calls to this method must be already lock'ed
  size_t total_bytes_written = 0;
  for (auto i = _messageQueue.begin(); i != _messageQueue.end(); ++i) {
    if (!i->sent()) {
      const size_t bytes_written = i->write(_client);
      total_bytes_written += bytes_written;
      _inflight += bytes_written;
      if (bytes_written == 0 || _inflight > _max_inflight) {
        // Serial.print("_");
        break;
      }
    }
  }

  // flush socket
  if (total_bytes_written) {
    _client->send();
  }
}

void AsyncEventSourceClient::set_max_inflight_bytes(size_t value) {
  if (value >= SSE_MIN_INFLIGH && value <= SSE_MAX_INFLIGH) {
    _max_inflight = value;
  }
}

/*  AsyncEventSource  */

void AsyncEventSource::authorizeConnect(ArAuthorizeConnectHandler cb) {
  AsyncAuthorizationMiddleware *m = new AsyncAuthorizationMiddleware(401, cb);
  m->_freeOnRemoval = true;
  addMiddleware(m);
}

void AsyncEventSource::_addClient(AsyncEventSourceClient *client) {
  if (!client) {
    return;
  }
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_client_queue_lock);
#endif
  _clients.emplace_back(client);
  if (_connectcb) {
    _connectcb(client);
  }

  _adjust_inflight_window();
}

void AsyncEventSource::_handleDisconnect(AsyncEventSourceClient *client) {
  if (_disconnectcb) {
    _disconnectcb(client);
  }
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_client_queue_lock);
#endif
  for (auto i = _clients.begin(); i != _clients.end(); ++i) {
    if (i->get() == client) {
      _clients.erase(i);
      break;
    }
  }
  _adjust_inflight_window();
}

void AsyncEventSource::close() {
  // While the whole loop is not done, the linked list is locked and so the
  // iterator should remain valid even when AsyncEventSource::_handleDisconnect()
  // is called very early
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_client_queue_lock);
#endif
  for (const auto &c : _clients) {
    if (c->connected()) {
      /**
       * @brief: Fix self-deadlock by using recursive_mutex instead.
       * Due to c->close() shall call the callback function _onDisconnect()
       * The calling flow _onDisconnect() --> _handleDisconnect() --> deadlock
      */
      c->close();
    }
  }
}

// pmb fix
size_t AsyncEventSource::avgPacketsWaiting() const {
  size_t aql = 0;
  uint32_t nConnectedClients = 0;
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_client_queue_lock);
#endif
  if (!_clients.size()) {
    return 0;
  }

  for (const auto &c : _clients) {
    if (c->connected()) {
      aql += c->packetsWaiting();
      ++nConnectedClients;
    }
  }
  return ((aql) + (nConnectedClients / 2)) / (nConnectedClients);  // round up
}

AsyncEventSource::SendStatus AsyncEventSource::send(const char *message, const char *event, uint32_t id, uint32_t reconnect) {
  AsyncEvent_SharedData_t shared_msg = std::make_shared<String>(generateEventMessage(message, event, id, reconnect));
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_client_queue_lock);
#endif
  size_t hits = 0;
  size_t miss = 0;
  for (const auto &c : _clients) {
    if (c->write(shared_msg)) {
      ++hits;
    } else {
      ++miss;
    }
  }
  return hits == 0 ? DISCARDED : (miss == 0 ? ENQUEUED : PARTIALLY_ENQUEUED);
}

size_t AsyncEventSource::count() const {
#ifdef ESP32
  std::lock_guard<std::recursive_mutex> lock(_client_queue_lock);
#endif
  size_t n_clients{0};
  for (const auto &i : _clients) {
    if (i->connected()) {
      ++n_clients;
    }
  }

  return n_clients;
}

bool AsyncEventSource::canHandle(AsyncWebServerRequest *request) const {
  return request->isSSE() && request->url().equals(_url);
}

void AsyncEventSource::handleRequest(AsyncWebServerRequest *request) {
  request->send(new AsyncEventSourceResponse(this));
}

void AsyncEventSource::_adjust_inflight_window() {
  if (_clients.size()) {
    size_t inflight = SSE_MAX_INFLIGH / _clients.size();
    for (const auto &c : _clients) {
      c->set_max_inflight_bytes(inflight);
    }
    // Serial.printf("adjusted inflight to: %u\n", inflight);
  }
}

/*  Response  */

AsyncEventSourceResponse::AsyncEventSourceResponse(AsyncEventSource *server) {
  _server = server;
  _code = 200;
  _contentType = T_text_event_stream;
  _sendContentLength = false;
  addHeader(T_Cache_Control, T_no_cache);
  addHeader(T_Connection, T_keep_alive);
}

void AsyncEventSourceResponse::_respond(AsyncWebServerRequest *request) {
  String out;
  _assembleHead(out, request->version());
  request->client()->write(out.c_str(), _headLength);
  _state = RESPONSE_WAIT_ACK;
}

size_t AsyncEventSourceResponse::_ack(AsyncWebServerRequest *request, size_t len, uint32_t time __attribute__((unused))) {
  if (len) {
    new AsyncEventSourceClient(request, _server);
  }
  return 0;
}

