/*
 * Copyright (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2009 Google Inc. All rights reserved.
 * Copyright (C) 2011 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HTTPParsers_h
#define HTTPParsers_h

#include "platform/PlatformExport.h"
#include "platform/json/JSONValues.h"
#include "platform/network/ParsedContentType.h"
#include "platform/wtf/Allocator.h"
#include "platform/wtf/Forward.h"
#include "platform/wtf/HashSet.h"
#include "platform/wtf/Vector.h"
#include "platform/wtf/text/StringHash.h"

#include <stdint.h>
#include <memory>

namespace blink {

class HTTPHeaderMap;
class Suborigin;
class ResourceResponse;

enum ContentTypeOptionsDisposition {
  kContentTypeOptionsNone,
  kContentTypeOptionsNosniff
};

// Be sure to update the behavior of
// XSSAuditor::combineXSSProtectionHeaderAndCSP whenever you change this enum's
// content or ordering.
enum ReflectedXSSDisposition {
  kReflectedXSSUnset = 0,
  kAllowReflectedXSS,
  kReflectedXSSInvalid,
  kFilterReflectedXSS,
  kBlockReflectedXSS
};

using CommaDelimitedHeaderSet = HashSet<String, CaseFoldingHash>;

struct CacheControlHeader {
  DISALLOW_NEW();
  bool parsed : 1;
  bool contains_no_cache : 1;
  bool contains_no_store : 1;
  bool contains_must_revalidate : 1;
  double max_age;

  CacheControlHeader()
      : parsed(false),
        contains_no_cache(false),
        contains_no_store(false),
        contains_must_revalidate(false),
        max_age(0.0) {}
};

struct ServerTimingHeader {
  String metric;
  double duration;
  String description;

  ServerTimingHeader(String metric, double duration, String description)
      : metric(metric), duration(duration), description(description) {}
};

using ServerTimingHeaderVector = Vector<std::unique_ptr<ServerTimingHeader>>;

PLATFORM_EXPORT bool IsContentDispositionAttachment(const String&);
PLATFORM_EXPORT bool IsValidHTTPHeaderValue(const String&);
PLATFORM_EXPORT bool IsValidHTTPFieldContentRFC7230(const String&);
// Checks whether the given string conforms to the |token| ABNF production
// defined in the RFC 7230 or not.
//
// The ABNF is for validating octets, but this method takes a String instance
// for convenience which consists of Unicode code points. When this method sees
// non-ASCII characters, it just returns false.
PLATFORM_EXPORT bool IsValidHTTPToken(const String&);
// |matcher| specifies a function to check a whitespace character. if |nullptr|
// is specified, ' ' and '\t' are treated as whitespace characters.
PLATFORM_EXPORT bool ParseHTTPRefresh(const String& refresh,
                                      WTF::CharacterMatchFunctionPtr matcher,
                                      double& delay,
                                      String& url);
PLATFORM_EXPORT double ParseDate(const String&);

// Given a Media Type (like "foo/bar; baz=gazonk" - usually from the
// 'Content-Type' HTTP header), extract and return the "type/subtype" portion
// ("foo/bar").
//
// Note:
// - This function does not in any way check that the "type/subtype" pair
//   is well-formed.
// - OWSes at the head and the tail of the region before the first semicolon
//   are trimmed.
PLATFORM_EXPORT AtomicString ExtractMIMETypeFromMediaType(const AtomicString&);
PLATFORM_EXPORT String ExtractCharsetFromMediaType(const String&);
PLATFORM_EXPORT void FindCharsetInMediaType(const String& media_type,
                                            unsigned& charset_pos,
                                            unsigned& charset_len,
                                            unsigned start = 0);
PLATFORM_EXPORT ReflectedXSSDisposition
ParseXSSProtectionHeader(const String& header,
                         String& failure_reason,
                         unsigned& failure_position,
                         String& report_url);
PLATFORM_EXPORT CacheControlHeader
ParseCacheControlDirectives(const AtomicString& cache_control_header,
                            const AtomicString& pragma_header);
PLATFORM_EXPORT void ParseCommaDelimitedHeader(const String& header_value,
                                               CommaDelimitedHeaderSet&);
// Returns true on success, otherwise false. The Suborigin argument must be a
// non-null return argument. |messages| is a list of messages based on any
// parse warnings or errors. Even if parseSuboriginHeader returns true, there
// may be Strings in |messages|.
PLATFORM_EXPORT bool ParseSuboriginHeader(const String& header,
                                          Suborigin*,
                                          WTF::Vector<String>& messages);

PLATFORM_EXPORT ContentTypeOptionsDisposition
ParseContentTypeOptionsHeader(const String& header);

// Returns true and stores the position of the end of the headers to |*end|
// if the headers part ends in |bytes[0..size]|. Returns false otherwise.
PLATFORM_EXPORT bool ParseMultipartFormHeadersFromBody(
    const char* bytes,
    size_t,
    HTTPHeaderMap* header_fields,
    size_t* end);

// Returns true and stores the position of the end of the headers to |*end|
// if the headers part ends in |bytes[0..size]|. Returns false otherwise.
PLATFORM_EXPORT bool ParseMultipartHeadersFromBody(const char* bytes,
                                                   size_t,
                                                   ResourceResponse*,
                                                   size_t* end);

// Parses a header value containing JSON data, according to
// https://tools.ietf.org/html/draft-ietf-httpbis-jfv-01
// Returns an empty unique_ptr if the header cannot be parsed as JSON. JSON
// strings which represent object nested deeper than |maxParseDepth| will also
// cause an empty return value.
PLATFORM_EXPORT std::unique_ptr<JSONArray> ParseJSONHeader(const String& header,
                                                           int max_parse_depth);

// Extracts the values in a Content-Range header and returns true if all three
// values are present and valid for a 206 response; otherwise returns false.
// The following values will be outputted:
// |*first_byte_position| = inclusive position of the first byte of the range
// |*last_byte_position| = inclusive position of the last byte of the range
// |*instance_length| = size in bytes of the object requested
// If this method returns false, then all of the outputs will be -1.
PLATFORM_EXPORT bool ParseContentRangeHeaderFor206(const String& content_range,
                                                   int64_t* first_byte_position,
                                                   int64_t* last_byte_position,
                                                   int64_t* instance_length);

PLATFORM_EXPORT std::unique_ptr<ServerTimingHeaderVector>
ParseServerTimingHeader(const String&);

PLATFORM_EXPORT String CheckDoubleQuotedString(const String&);

using Mode = blink::ParsedContentType::Mode;
PLATFORM_EXPORT bool Consume(char, const String&, unsigned&);
PLATFORM_EXPORT bool ConsumeToken(Mode, const String&, unsigned&, StringView&);
PLATFORM_EXPORT bool ConsumeQuotedString(const String&, unsigned&, String&);
PLATFORM_EXPORT bool ConsumeTokenOrQuotedString(Mode,
                                                const String&,
                                                unsigned&,
                                                String&);
PLATFORM_EXPORT bool IsEnd(const String&, unsigned);

}  // namespace blink

#endif
