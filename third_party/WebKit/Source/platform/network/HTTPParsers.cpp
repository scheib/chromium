/*
 * Copyright (C) 2006 Alexey Proskuryakov (ap@webkit.org)
 * Copyright (C) 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile Inc. http://www.torchmobile.com/
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

#include "platform/network/HTTPParsers.h"

#include "net/http/http_content_disposition.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_util.h"
#include "platform/HTTPNames.h"
#include "platform/json/JSONParser.h"
#include "platform/loader/fetch/ResourceResponse.h"
#include "platform/weborigin/Suborigin.h"
#include "platform/wtf/DateMath.h"
#include "platform/wtf/MathExtras.h"
#include "platform/wtf/text/CString.h"
#include "platform/wtf/text/CharacterNames.h"
#include "platform/wtf/text/ParsingUtilities.h"
#include "platform/wtf/text/StringBuilder.h"
#include "platform/wtf/text/StringUTF8Adaptor.h"
#include "platform/wtf/text/WTFString.h"
#include "public/platform/WebString.h"

using namespace WTF;

namespace blink {

namespace {

const Vector<AtomicString>& ReplaceHeaders() {
  // The list of response headers that we do not copy from the original
  // response when generating a ResourceResponse for a MIME payload.
  // Note: this is called only on the main thread.
  DEFINE_STATIC_LOCAL(Vector<AtomicString>, headers,
                      ({"content-type", "content-length", "content-disposition",
                        "content-range", "range", "set-cookie"}));
  return headers;
}

bool IsWhitespace(UChar chr) {
  return (chr == ' ') || (chr == '\t');
}

// true if there is more to parse, after incrementing pos past whitespace.
// Note: Might return pos == str.length()
// if |matcher| is nullptr, isWhitespace() is used.
inline bool SkipWhiteSpace(const String& str,
                           unsigned& pos,
                           CharacterMatchFunctionPtr matcher = nullptr) {
  unsigned len = str.length();

  if (matcher) {
    while (pos < len && matcher(str[pos]))
      ++pos;
  } else {
    while (pos < len && IsWhitespace(str[pos]))
      ++pos;
  }

  return pos < len;
}

// Returns true if the function can match the whole token (case insensitive)
// incrementing pos on match, otherwise leaving pos unchanged.
// Note: Might return pos == str.length()
inline bool SkipToken(const String& str, unsigned& pos, const char* token) {
  unsigned len = str.length();
  unsigned current = pos;

  while (current < len && *token) {
    if (ToASCIILower(str[current]) != *token++)
      return false;
    ++current;
  }

  if (*token)
    return false;

  pos = current;
  return true;
}

// True if the expected equals sign is seen and there is more to follow.
inline bool SkipEquals(const String& str, unsigned& pos) {
  return SkipWhiteSpace(str, pos) && str[pos++] == '=' &&
         SkipWhiteSpace(str, pos);
}

// True if a value present, incrementing pos to next space or semicolon, if any.
// Note: might return pos == str.length().
inline bool SkipValue(const String& str, unsigned& pos) {
  unsigned start = pos;
  unsigned len = str.length();
  while (pos < len) {
    if (str[pos] == ' ' || str[pos] == '\t' || str[pos] == ';')
      break;
    ++pos;
  }
  return pos != start;
}

template <typename CharType>
inline bool IsASCIILowerAlphaOrDigit(CharType c) {
  return IsASCIILower(c) || IsASCIIDigit(c);
}

template <typename CharType>
inline bool IsASCIILowerAlphaOrDigitOrHyphen(CharType c) {
  return IsASCIILowerAlphaOrDigit(c) || c == '-';
}

Suborigin::SuboriginPolicyOptions GetSuboriginPolicyOptionFromString(
    const String& policy_option_name) {
  if (policy_option_name == "'unsafe-postmessage-send'")
    return Suborigin::SuboriginPolicyOptions::kUnsafePostMessageSend;

  if (policy_option_name == "'unsafe-postmessage-receive'")
    return Suborigin::SuboriginPolicyOptions::kUnsafePostMessageReceive;

  if (policy_option_name == "'unsafe-cookies'")
    return Suborigin::SuboriginPolicyOptions::kUnsafeCookies;

  if (policy_option_name == "'unsafe-credentials'")
    return Suborigin::SuboriginPolicyOptions::kUnsafeCredentials;

  return Suborigin::SuboriginPolicyOptions::kNone;
}

// suborigin-name = LOWERALPHA *( LOWERALPHA / DIGIT )
//
// Does not trim whitespace before or after the suborigin-name.
const UChar* ParseSuboriginName(const UChar* begin,
                                const UChar* end,
                                String& name,
                                WTF::Vector<String>& messages) {
  // Parse the name of the suborigin (no spaces, single string)
  if (begin == end) {
    messages.push_back(String("No Suborigin name specified."));
    return nullptr;
  }

  const UChar* position = begin;

  if (!skipExactly<UChar, IsASCIILower>(position, end)) {
    messages.push_back("Invalid character \'" + String(position, 1) +
                       "\' in suborigin. First character must be a lower case "
                       "alphabetic character.");
    return nullptr;
  }

  skipWhile<UChar, IsASCIILowerAlphaOrDigit>(position, end);
  if (position != end && !IsASCIISpace(*position)) {
    messages.push_back("Invalid character \'" + String(position, 1) +
                       "\' in suborigin.");
    return nullptr;
  }

  size_t length = position - begin;
  name = String(begin, length).DeprecatedLower();
  return position;
}

const UChar* ParseSuboriginPolicyOption(const UChar* begin,
                                        const UChar* end,
                                        String& option,
                                        WTF::Vector<String>& messages) {
  const UChar* position = begin;

  if (*position != '\'') {
    messages.push_back("Invalid character \'" + String(position, 1) +
                       "\' in suborigin policy. Suborigin policy options must "
                       "start and end with a single quote.");
    return nullptr;
  }
  position = position + 1;

  skipWhile<UChar, IsASCIILowerAlphaOrDigitOrHyphen>(position, end);
  if (position == end || IsASCIISpace(*position)) {
    messages.push_back(String("Expected \' to end policy option."));
    return nullptr;
  }

  if (*position != '\'') {
    messages.push_back("Invalid character \'" + String(position, 1) +
                       "\' in suborigin policy.");
    return nullptr;
  }

  DCHECK_GT(position, begin);
  size_t length = (position + 1) - begin;

  option = String(begin, length);
  return position + 1;
}

}  // namespace

bool IsValidHTTPHeaderValue(const String& name) {
  // FIXME: This should really match name against
  // field-value in section 4.2 of RFC 2616.

  return name.ContainsOnlyLatin1() && !name.Contains('\r') &&
         !name.Contains('\n') && !name.Contains('\0');
}

// See RFC 7230, Section 3.2.
// Checks whether |value| matches field-content in RFC 7230.
// link: http://tools.ietf.org/html/rfc7230#section-3.2
bool IsValidHTTPFieldContentRFC7230(const String& value) {
  if (value.IsEmpty())
    return false;

  UChar first_character = value[0];
  if (first_character == ' ' || first_character == '\t')
    return false;

  UChar last_character = value[value.length() - 1];
  if (last_character == ' ' || last_character == '\t')
    return false;

  for (unsigned i = 0; i < value.length(); ++i) {
    UChar c = value[i];
    // TODO(mkwst): Extract this character class to a central location,
    // https://crbug.com/527324.
    if (c == 0x7F || c > 0xFF || (c < 0x20 && c != '\t'))
      return false;
  }

  return true;
}

// See RFC 7230, Section 3.2.6.
bool IsValidHTTPToken(const String& characters) {
  if (characters.IsEmpty())
    return false;
  for (unsigned i = 0; i < characters.length(); ++i) {
    UChar c = characters[i];
    if (c > 0x7F || !net::HttpUtil::IsTokenChar(c))
      return false;
  }
  return true;
}

bool IsContentDispositionAttachment(const String& content_disposition) {
  CString cstring(content_disposition.Utf8());
  std::string string(cstring.data(), cstring.length());
  return net::HttpContentDisposition(string, std::string()).is_attachment();
}

bool ParseHTTPRefresh(const String& refresh,
                      CharacterMatchFunctionPtr matcher,
                      double& delay,
                      String& url) {
  unsigned len = refresh.length();
  unsigned pos = 0;
  matcher = matcher ? matcher : IsWhitespace;

  if (!SkipWhiteSpace(refresh, pos, matcher))
    return false;

  while (pos != len && refresh[pos] != ',' && refresh[pos] != ';' &&
         !matcher(refresh[pos]))
    ++pos;

  if (pos == len) {  // no URL
    url = String();
    bool ok;
    delay = refresh.StripWhiteSpace().ToDouble(&ok);
    return ok;
  } else {
    bool ok;
    delay = refresh.Left(pos).StripWhiteSpace().ToDouble(&ok);
    if (!ok)
      return false;

    SkipWhiteSpace(refresh, pos, matcher);
    if (pos < len && (refresh[pos] == ',' || refresh[pos] == ';'))
      ++pos;
    SkipWhiteSpace(refresh, pos, matcher);
    unsigned url_start_pos = pos;
    if (refresh.FindIgnoringASCIICase("url", url_start_pos) == url_start_pos) {
      url_start_pos += 3;
      SkipWhiteSpace(refresh, url_start_pos, matcher);
      if (refresh[url_start_pos] == '=') {
        ++url_start_pos;
        SkipWhiteSpace(refresh, url_start_pos, matcher);
      } else {
        url_start_pos = pos;  // e.g. "Refresh: 0; url.html"
      }
    }

    unsigned url_end_pos = len;

    if (refresh[url_start_pos] == '"' || refresh[url_start_pos] == '\'') {
      UChar quotation_mark = refresh[url_start_pos];
      url_start_pos++;
      while (url_end_pos > url_start_pos) {
        url_end_pos--;
        if (refresh[url_end_pos] == quotation_mark)
          break;
      }

      // https://bugs.webkit.org/show_bug.cgi?id=27868
      // Sometimes there is no closing quote for the end of the URL even though
      // there was an opening quote.  If we looped over the entire alleged URL
      // string back to the opening quote, just go ahead and use everything
      // after the opening quote instead.
      if (url_end_pos == url_start_pos)
        url_end_pos = len;
    }

    url = refresh.Substring(url_start_pos, url_end_pos - url_start_pos)
              .StripWhiteSpace();
    return true;
  }
}

double ParseDate(const String& value) {
  return ParseDateFromNullTerminatedCharacters(value.Utf8().data());
}

AtomicString ExtractMIMETypeFromMediaType(const AtomicString& media_type) {
  unsigned length = media_type.length();

  unsigned pos = 0;

  while (pos < length) {
    UChar c = media_type[pos];
    if (c != '\t' && c != ' ')
      break;
    ++pos;
  }

  if (pos == length)
    return media_type;

  unsigned type_start = pos;

  unsigned type_end = pos;
  while (pos < length) {
    UChar c = media_type[pos];

    // While RFC 2616 does not allow it, other browsers allow multiple values in
    // the HTTP media type header field, Content-Type. In such cases, the media
    // type string passed here may contain the multiple values separated by
    // commas. For now, this code ignores text after the first comma, which
    // prevents it from simply failing to parse such types altogether.  Later
    // for better compatibility we could consider using the first or last valid
    // MIME type instead.
    // See https://bugs.webkit.org/show_bug.cgi?id=25352 for more discussion.
    if (c == ',' || c == ';')
      break;

    if (c != '\t' && c != ' ')
      type_end = pos + 1;

    ++pos;
  }

  return AtomicString(
      media_type.GetString().Substring(type_start, type_end - type_start));
}

String ExtractCharsetFromMediaType(const String& media_type) {
  unsigned pos, len;
  FindCharsetInMediaType(media_type, pos, len);
  return media_type.Substring(pos, len);
}

void FindCharsetInMediaType(const String& media_type,
                            unsigned& charset_pos,
                            unsigned& charset_len,
                            unsigned start) {
  charset_pos = start;
  charset_len = 0;

  size_t pos = start;
  unsigned length = media_type.length();

  while (pos < length) {
    pos = media_type.FindIgnoringASCIICase("charset", pos);
    if (pos == kNotFound || !pos) {
      charset_len = 0;
      return;
    }

    // is what we found a beginning of a word?
    if (media_type[pos - 1] > ' ' && media_type[pos - 1] != ';') {
      pos += 7;
      continue;
    }

    pos += 7;

    // skip whitespace
    while (pos != length && media_type[pos] <= ' ')
      ++pos;

    if (media_type[pos++] !=
        '=')  // this "charset" substring wasn't a parameter
              // name, but there may be others
      continue;

    while (pos != length && (media_type[pos] <= ' ' || media_type[pos] == '"' ||
                             media_type[pos] == '\''))
      ++pos;

    // we don't handle spaces within quoted parameter values, because charset
    // names cannot have any
    unsigned endpos = pos;
    while (pos != length && media_type[endpos] > ' ' &&
           media_type[endpos] != '"' && media_type[endpos] != '\'' &&
           media_type[endpos] != ';')
      ++endpos;

    charset_pos = pos;
    charset_len = endpos - pos;
    return;
  }
}

ReflectedXSSDisposition ParseXSSProtectionHeader(const String& header,
                                                 String& failure_reason,
                                                 unsigned& failure_position,
                                                 String& report_url) {
  DEFINE_STATIC_LOCAL(String, failure_reason_invalid_toggle,
                      ("expected 0 or 1"));
  DEFINE_STATIC_LOCAL(String, failure_reason_invalid_separator,
                      ("expected semicolon"));
  DEFINE_STATIC_LOCAL(String, failure_reason_invalid_equals,
                      ("expected equals sign"));
  DEFINE_STATIC_LOCAL(String, failure_reason_invalid_mode,
                      ("invalid mode directive"));
  DEFINE_STATIC_LOCAL(String, failure_reason_invalid_report,
                      ("invalid report directive"));
  DEFINE_STATIC_LOCAL(String, failure_reason_duplicate_mode,
                      ("duplicate mode directive"));
  DEFINE_STATIC_LOCAL(String, failure_reason_duplicate_report,
                      ("duplicate report directive"));
  DEFINE_STATIC_LOCAL(String, failure_reason_invalid_directive,
                      ("unrecognized directive"));

  unsigned pos = 0;

  if (!SkipWhiteSpace(header, pos))
    return kReflectedXSSUnset;

  if (header[pos] == '0')
    return kAllowReflectedXSS;

  if (header[pos++] != '1') {
    failure_reason = failure_reason_invalid_toggle;
    return kReflectedXSSInvalid;
  }

  ReflectedXSSDisposition result = kFilterReflectedXSS;
  bool mode_directive_seen = false;
  bool report_directive_seen = false;

  while (1) {
    // At end of previous directive: consume whitespace, semicolon, and
    // whitespace.
    if (!SkipWhiteSpace(header, pos))
      return result;

    if (header[pos++] != ';') {
      failure_reason = failure_reason_invalid_separator;
      failure_position = pos;
      return kReflectedXSSInvalid;
    }

    if (!SkipWhiteSpace(header, pos))
      return result;

    // At start of next directive.
    if (SkipToken(header, pos, "mode")) {
      if (mode_directive_seen) {
        failure_reason = failure_reason_duplicate_mode;
        failure_position = pos;
        return kReflectedXSSInvalid;
      }
      mode_directive_seen = true;
      if (!SkipEquals(header, pos)) {
        failure_reason = failure_reason_invalid_equals;
        failure_position = pos;
        return kReflectedXSSInvalid;
      }
      if (!SkipToken(header, pos, "block")) {
        failure_reason = failure_reason_invalid_mode;
        failure_position = pos;
        return kReflectedXSSInvalid;
      }
      result = kBlockReflectedXSS;
    } else if (SkipToken(header, pos, "report")) {
      if (report_directive_seen) {
        failure_reason = failure_reason_duplicate_report;
        failure_position = pos;
        return kReflectedXSSInvalid;
      }
      report_directive_seen = true;
      if (!SkipEquals(header, pos)) {
        failure_reason = failure_reason_invalid_equals;
        failure_position = pos;
        return kReflectedXSSInvalid;
      }
      size_t start_pos = pos;
      if (!SkipValue(header, pos)) {
        failure_reason = failure_reason_invalid_report;
        failure_position = pos;
        return kReflectedXSSInvalid;
      }
      report_url = header.Substring(start_pos, pos - start_pos);
      failure_position =
          start_pos;  // If later semantic check deems unacceptable.
    } else {
      failure_reason = failure_reason_invalid_directive;
      failure_position = pos;
      return kReflectedXSSInvalid;
    }
  }
}

ContentTypeOptionsDisposition ParseContentTypeOptionsHeader(
    const String& header) {
  if (header.StripWhiteSpace().DeprecatedLower() == "nosniff")
    return kContentTypeOptionsNosniff;
  return kContentTypeOptionsNone;
}

static bool IsCacheHeaderSeparator(UChar c) {
  // See RFC 2616, Section 2.2
  switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '"':
    case '/':
    case '[':
    case ']':
    case '?':
    case '=':
    case '{':
    case '}':
    case ' ':
    case '\t':
      return true;
    default:
      return false;
  }
}

static bool IsControlCharacter(UChar c) {
  return c < ' ' || c == 127;
}

static inline String TrimToNextSeparator(const String& str) {
  return str.Substring(0, str.Find(IsCacheHeaderSeparator));
}

static void ParseCacheHeader(const String& header,
                             Vector<std::pair<String, String>>& result) {
  const String safe_header = header.RemoveCharacters(IsControlCharacter);
  unsigned max = safe_header.length();
  for (unsigned pos = 0; pos < max; /* pos incremented in loop */) {
    size_t next_comma_position = safe_header.find(',', pos);
    size_t next_equal_sign_position = safe_header.find('=', pos);
    if (next_equal_sign_position != kNotFound &&
        (next_equal_sign_position < next_comma_position ||
         next_comma_position == kNotFound)) {
      // Get directive name, parse right hand side of equal sign, then add to
      // map
      String directive = TrimToNextSeparator(
          safe_header.Substring(pos, next_equal_sign_position - pos)
              .StripWhiteSpace());
      pos += next_equal_sign_position - pos + 1;

      String value = safe_header.Substring(pos, max - pos).StripWhiteSpace();
      if (value[0] == '"') {
        // The value is a quoted string
        size_t next_double_quote_position = value.find('"', 1);
        if (next_double_quote_position != kNotFound) {
          // Store the value as a quoted string without quotes
          result.push_back(std::pair<String, String>(
              directive, value.Substring(1, next_double_quote_position - 1)
                             .StripWhiteSpace()));
          pos += (safe_header.find('"', pos) - pos) +
                 next_double_quote_position + 1;
          // Move past next comma, if there is one
          size_t next_comma_position2 = safe_header.find(',', pos);
          if (next_comma_position2 != kNotFound)
            pos += next_comma_position2 - pos + 1;
          else
            return;  // Parse error if there is anything left with no comma
        } else {
          // Parse error; just use the rest as the value
          result.push_back(std::pair<String, String>(
              directive,
              TrimToNextSeparator(
                  value.Substring(1, value.length() - 1).StripWhiteSpace())));
          return;
        }
      } else {
        // The value is a token until the next comma
        size_t next_comma_position2 = value.find(',');
        if (next_comma_position2 != kNotFound) {
          // The value is delimited by the next comma
          result.push_back(std::pair<String, String>(
              directive,
              TrimToNextSeparator(
                  value.Substring(0, next_comma_position2).StripWhiteSpace())));
          pos += (safe_header.find(',', pos) - pos) + 1;
        } else {
          // The rest is the value; no change to value needed
          result.push_back(
              std::pair<String, String>(directive, TrimToNextSeparator(value)));
          return;
        }
      }
    } else if (next_comma_position != kNotFound &&
               (next_comma_position < next_equal_sign_position ||
                next_equal_sign_position == kNotFound)) {
      // Add directive to map with empty string as value
      result.push_back(std::pair<String, String>(
          TrimToNextSeparator(
              safe_header.Substring(pos, next_comma_position - pos)
                  .StripWhiteSpace()),
          ""));
      pos += next_comma_position - pos + 1;
    } else {
      // Add last directive to map with empty string as value
      result.push_back(std::pair<String, String>(
          TrimToNextSeparator(
              safe_header.Substring(pos, max - pos).StripWhiteSpace()),
          ""));
      return;
    }
  }
}

CacheControlHeader ParseCacheControlDirectives(
    const AtomicString& cache_control_value,
    const AtomicString& pragma_value) {
  CacheControlHeader cache_control_header;
  cache_control_header.parsed = true;
  cache_control_header.max_age = std::numeric_limits<double>::quiet_NaN();

  static const char kNoCacheDirective[] = "no-cache";
  static const char kNoStoreDirective[] = "no-store";
  static const char kMustRevalidateDirective[] = "must-revalidate";
  static const char kMaxAgeDirective[] = "max-age";

  if (!cache_control_value.IsEmpty()) {
    Vector<std::pair<String, String>> directives;
    ParseCacheHeader(cache_control_value, directives);

    size_t directives_size = directives.size();
    for (size_t i = 0; i < directives_size; ++i) {
      // RFC2616 14.9.1: A no-cache directive with a value is only meaningful
      // for proxy caches.  It should be ignored by a browser level cache.
      if (DeprecatedEqualIgnoringCase(directives[i].first, kNoCacheDirective) &&
          directives[i].second.IsEmpty()) {
        cache_control_header.contains_no_cache = true;
      } else if (DeprecatedEqualIgnoringCase(directives[i].first,
                                             kNoStoreDirective)) {
        cache_control_header.contains_no_store = true;
      } else if (DeprecatedEqualIgnoringCase(directives[i].first,
                                             kMustRevalidateDirective)) {
        cache_control_header.contains_must_revalidate = true;
      } else if (DeprecatedEqualIgnoringCase(directives[i].first,
                                             kMaxAgeDirective)) {
        if (!std::isnan(cache_control_header.max_age)) {
          // First max-age directive wins if there are multiple ones.
          continue;
        }
        bool ok;
        double max_age = directives[i].second.ToDouble(&ok);
        if (ok)
          cache_control_header.max_age = max_age;
      }
    }
  }

  if (!cache_control_header.contains_no_cache) {
    // Handle Pragma: no-cache
    // This is deprecated and equivalent to Cache-control: no-cache
    // Don't bother tokenizing the value, it is not important
    cache_control_header.contains_no_cache =
        pragma_value.DeprecatedLower().Contains(kNoCacheDirective);
  }
  return cache_control_header;
}

void ParseCommaDelimitedHeader(const String& header_value,
                               CommaDelimitedHeaderSet& header_set) {
  Vector<String> results;
  header_value.Split(",", results);
  for (auto& value : results)
    header_set.insert(value.StripWhiteSpace(IsWhitespace));
}

bool ParseSuboriginHeader(const String& header,
                          Suborigin* suborigin,
                          WTF::Vector<String>& messages) {
  Vector<String> headers;
  header.Split(',', true, headers);

  if (headers.size() > 1)
    messages.push_back(
        "Multiple Suborigin headers found. Ignoring all but the first.");

  Vector<UChar> characters;
  headers[0].AppendTo(characters);

  const UChar* position = characters.data();
  const UChar* end = position + characters.size();

  skipWhile<UChar, IsASCIISpace>(position, end);

  String name;
  position = ParseSuboriginName(position, end, name, messages);
  // For now it is appropriate to simply return false if the name is empty and
  // act as if the header doesn't exist. If suborigin policy options are created
  // that can apply to the empty suborigin, than this will have to change.
  if (!position || name.IsEmpty())
    return false;

  suborigin->SetName(name);

  while (position < end) {
    skipWhile<UChar, IsASCIISpace>(position, end);
    if (position == end)
      return true;

    String option_name;
    position = ParseSuboriginPolicyOption(position, end, option_name, messages);

    if (!position) {
      suborigin->Clear();
      return false;
    }

    Suborigin::SuboriginPolicyOptions option =
        GetSuboriginPolicyOptionFromString(option_name);
    if (option == Suborigin::SuboriginPolicyOptions::kNone)
      messages.push_back("Ignoring unknown suborigin policy option " +
                         option_name + ".");
    else
      suborigin->AddPolicyOption(option);
  }

  return true;
}

bool ParseMultipartHeadersFromBody(const char* bytes,
                                   size_t size,
                                   ResourceResponse* response,
                                   size_t* end) {
  DCHECK(IsMainThread());

  int headers_end_pos =
      net::HttpUtil::LocateEndOfAdditionalHeaders(bytes, size, 0);

  if (headers_end_pos < 0)
    return false;

  *end = headers_end_pos;

  // Eat headers and prepend a status line as is required by
  // HttpResponseHeaders.
  std::string headers("HTTP/1.1 200 OK\r\n");
  headers.append(bytes, headers_end_pos);

  scoped_refptr<net::HttpResponseHeaders> response_headers =
      new net::HttpResponseHeaders(
          net::HttpUtil::AssembleRawHeaders(headers.data(), headers.length()));

  std::string mime_type, charset;
  response_headers->GetMimeTypeAndCharset(&mime_type, &charset);
  response->SetMimeType(WebString::FromUTF8(mime_type));
  response->SetTextEncodingName(WebString::FromUTF8(charset));

  // Copy headers listed in replaceHeaders to the response.
  for (const AtomicString& header : ReplaceHeaders()) {
    std::string value;
    StringUTF8Adaptor adaptor(header);
    base::StringPiece header_string_piece(adaptor.AsStringPiece());
    size_t iterator = 0;

    response->ClearHTTPHeaderField(header);
    while (response_headers->EnumerateHeader(&iterator, header_string_piece,
                                             &value)) {
      response->AddHTTPHeaderField(header, WebString::FromLatin1(value));
    }
  }
  return true;
}

bool ParseMultipartFormHeadersFromBody(const char* bytes,
                                       size_t size,
                                       HTTPHeaderMap* header_fields,
                                       size_t* end) {
  DCHECK_EQ(0u, header_fields->size());

  int headersEndPos =
      net::HttpUtil::LocateEndOfAdditionalHeaders(bytes, size, 0);

  if (headersEndPos < 0)
    return false;

  *end = headersEndPos;

  // Eat headers and prepend a status line as is required by
  // HttpResponseHeaders.
  std::string headers("HTTP/1.1 200 OK\r\n");
  headers.append(bytes, headersEndPos);

  scoped_refptr<net::HttpResponseHeaders> responseHeaders =
      new net::HttpResponseHeaders(
          net::HttpUtil::AssembleRawHeaders(headers.data(), headers.length()));

  // Copy selected header fields.
  const AtomicString* const headerNamePointers[] = {
      &HTTPNames::Content_Disposition, &HTTPNames::Content_Type};
  for (const AtomicString* headerNamePointer : headerNamePointers) {
    StringUTF8Adaptor adaptor(*headerNamePointer);
    size_t iterator = 0;
    base::StringPiece headerNameStringPiece = adaptor.AsStringPiece();
    std::string value;
    while (responseHeaders->EnumerateHeader(&iterator, headerNameStringPiece,
                                            &value)) {
      header_fields->Add(*headerNamePointer, WebString::FromUTF8(value));
    }
  }

  return true;
}

// See https://tools.ietf.org/html/draft-ietf-httpbis-jfv-01, Section 4.
std::unique_ptr<JSONArray> ParseJSONHeader(const String& header,
                                           int max_parse_depth) {
  StringBuilder sb;
  sb.Append("[");
  sb.Append(header);
  sb.Append("]");
  std::unique_ptr<JSONValue> header_value =
      ParseJSON(sb.ToString(), max_parse_depth);
  return JSONArray::From(std::move(header_value));
}

bool ParseContentRangeHeaderFor206(const String& content_range,
                                   int64_t* first_byte_position,
                                   int64_t* last_byte_position,
                                   int64_t* instance_length) {
  return net::HttpUtil::ParseContentRangeHeaderFor206(
      StringUTF8Adaptor(content_range).AsStringPiece(), first_byte_position,
      last_byte_position, instance_length);
}

template <typename CharType>
inline bool IsNotServerTimingHeaderDelimiter(CharType c) {
  return c != '=' && c != ';' && c != ',';
}

const LChar* ParseServerTimingToken(const LChar* begin,
                                    const LChar* end,
                                    String& result) {
  const LChar* position = begin;
  skipWhile<LChar, IsNotServerTimingHeaderDelimiter>(position, end);
  result = String(begin, position - begin).StripWhiteSpace();
  return position;
}

String CheckDoubleQuotedString(const String& value) {
  if (value.length() < 2 || value[0] != '"' ||
      value[value.length() - 1] != '"') {
    return value;
  }

  StringBuilder out;
  unsigned pos = 1;                   // Begin after the opening DQUOTE.
  unsigned len = value.length() - 1;  // End before the closing DQUOTE.

  // Skip past backslashes, but include everything else.
  while (pos < len) {
    if (value[pos] == '\\')
      pos++;
    if (pos < len)
      out.Append(value[pos++]);
  }

  return out.ToString();
}

std::unique_ptr<ServerTimingHeaderVector> ParseServerTimingHeader(
    const String& headerValue) {
  std::unique_ptr<ServerTimingHeaderVector> headers =
      WTF::MakeUnique<ServerTimingHeaderVector>();

  if (!headerValue.IsNull()) {
    DCHECK(headerValue.Is8Bit());

    const LChar* position = headerValue.Characters8();
    const LChar* end = position + headerValue.length();
    while (position < end) {
      String metric, value, description = "";
      position = ParseServerTimingToken(position, end, metric);
      if (position != end && *position == '=') {
        position = ParseServerTimingToken(position + 1, end, value);
      }
      if (position != end && *position == ';') {
        position = ParseServerTimingToken(position + 1, end, description);
      }
      position++;

      headers->push_back(WTF::MakeUnique<ServerTimingHeader>(
          metric, value.ToDouble(), CheckDoubleQuotedString(description)));
    }
  }
  return headers;
}

bool IsTokenCharacter(Mode mode, UChar c) {
  if (c >= 128)
    return false;
  if (c < 0x20)
    return false;

  switch (c) {
    case ' ':
    case ';':
    case '"':
      return false;
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ':':
    case '\\':
    case '/':
    case '[':
    case ']':
    case '?':
    case '=':
      return mode == Mode::kRelaxed;
    default:
      return true;
  }
}

bool Consume(char c, const String& input, unsigned& index) {
  DCHECK_NE(c, ' ');
  while (index < input.length() && input[index] == ' ')
    ++index;

  if (index < input.length() && input[index] == c) {
    ++index;
    return true;
  }
  return false;
}

bool ConsumeToken(Mode mode,
                  const String& input,
                  unsigned& index,
                  StringView& output) {
  DCHECK(output.IsNull());

  while (index < input.length() && input[index] == ' ')
    ++index;

  auto start = index;
  while (index < input.length() && IsTokenCharacter(mode, input[index]))
    ++index;

  if (start == index)
    return false;

  output = StringView(input, start, index - start);
  return true;
}

bool ConsumeQuotedString(const String& input, unsigned& index, String& output) {
  StringBuilder builder;
  DCHECK_EQ('"', input[index]);
  ++index;
  while (index < input.length()) {
    if (input[index] == '\\') {
      ++index;
      if (index == input.length())
        return false;
      builder.Append(input[index]);
      ++index;
      continue;
    }
    if (input[index] == '"') {
      output = builder.ToString();
      ++index;
      return true;
    }
    builder.Append(input[index]);
    ++index;
  }
  return false;
}

bool ConsumeTokenOrQuotedString(Mode mode,
                                const String& input,
                                unsigned& index,
                                String& output) {
  while (index < input.length() && input[index] == ' ')
    ++index;
  if (input.length() == index)
    return false;
  if (input[index] == '"') {
    return ConsumeQuotedString(input, index, output);
  }
  StringView view;
  auto result = ConsumeToken(mode, input, index, view);
  output = view.ToString();
  return result;
}

bool IsEnd(const String& input, unsigned index) {
  while (index < input.length()) {
    if (input[index] != ' ')
      return false;
    ++index;
  }
  return true;
}

}  // namespace blink
