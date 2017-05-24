// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/update_client/utils.h"

#include <stddef.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "components/crx_file/id_util.h"
#include "components/data_use_measurement/core/data_use_user_data.h"
#include "components/update_client/component.h"
#include "components/update_client/configurator.h"
#include "components/update_client/update_client.h"
#include "components/update_client/update_client_errors.h"
#include "crypto/secure_hash.h"
#include "crypto/sha2.h"
#include "net/base/load_flags.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_fetcher.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/url_request/url_request_status.h"
#include "url/gurl.h"

namespace update_client {

namespace {

// Produces an extension-like friendly id.
std::string HexStringToID(const std::string& hexstr) {
  std::string id;
  for (size_t i = 0; i < hexstr.size(); ++i) {
    int val = 0;
    if (base::HexStringToInt(
            base::StringPiece(hexstr.begin() + i, hexstr.begin() + i + 1),
            &val)) {
      id.append(1, val + 'a');
    } else {
      id.append(1, 'a');
    }
  }

  DCHECK(crx_file::id_util::IdIsValid(id));

  return id;
}

}  // namespace


std::unique_ptr<net::URLFetcher> SendProtocolRequest(
    const GURL& url,
    const std::string& protocol_request,
    net::URLFetcherDelegate* url_fetcher_delegate,
    net::URLRequestContextGetter* url_request_context_getter) {
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("component_updater_utils", R"(
        semantics {
          sender: "Component Updater"
          description:
            "The component updater in Chrome is responsible for updating code "
            "and data modules such as Flash, CrlSet, Origin Trials, etc. These "
            "modules are updated on cycles independent of the Chrome release "
            "tracks. It runs in the browser process and communicates with a "
            "set of servers using the Omaha protocol to find the latest "
            "versions of components, download them, and register them with the "
            "rest of Chrome."
          trigger: "Manual or automatic software updates."
          data:
            "Various OS and Chrome parameters such as version, bitness, "
            "release tracks, etc."
          destination: GOOGLE_OWNED_SERVICE
        }
        policy {
          cookies_allowed: false
          setting: "This feature cannot be disabled."
          chrome_policy {
            ComponentUpdatesEnabled {
              policy_options {mode: MANDATORY}
              ComponentUpdatesEnabled: false
            }
          }
        })");
  std::unique_ptr<net::URLFetcher> url_fetcher = net::URLFetcher::Create(
      0, url, net::URLFetcher::POST, url_fetcher_delegate, traffic_annotation);
  if (!url_fetcher.get())
    return url_fetcher;

  data_use_measurement::DataUseUserData::AttachToFetcher(
      url_fetcher.get(), data_use_measurement::DataUseUserData::UPDATE_CLIENT);
  url_fetcher->SetUploadData("application/xml", protocol_request);
  url_fetcher->SetRequestContext(url_request_context_getter);
  url_fetcher->SetLoadFlags(net::LOAD_DO_NOT_SEND_COOKIES |
                            net::LOAD_DO_NOT_SAVE_COOKIES |
                            net::LOAD_DISABLE_CACHE);
  url_fetcher->SetAutomaticallyRetryOn5xx(false);
  url_fetcher->Start();

  return url_fetcher;
}

bool FetchSuccess(const net::URLFetcher& fetcher) {
  return GetFetchError(fetcher) == 0;
}

int GetFetchError(const net::URLFetcher& fetcher) {
  const net::URLRequestStatus::Status status(fetcher.GetStatus().status());
  switch (status) {
    case net::URLRequestStatus::IO_PENDING:
    case net::URLRequestStatus::CANCELED:
      // Network status is a small positive number.
      return status;

    case net::URLRequestStatus::SUCCESS: {
      // Response codes are positive numbers, greater than 100.
      const int response_code(fetcher.GetResponseCode());
      if (response_code == 200)
        return 0;
      else
        return response_code ? response_code : -1;
    }

    case net::URLRequestStatus::FAILED: {
      // Network errors are small negative numbers.
      const int error = fetcher.GetStatus().error();
      return error ? error : -1;
    }

    default:
      return -1;
  }
}

bool HasDiffUpdate(const Component& component) {
  return !component.crx_diffurls().empty();
}

bool IsHttpServerError(int status_code) {
  return 500 <= status_code && status_code < 600;
}

bool DeleteFileAndEmptyParentDirectory(const base::FilePath& filepath) {
  if (!base::DeleteFile(filepath, false))
    return false;

  const base::FilePath dirname(filepath.DirName());
  if (!base::IsDirectoryEmpty(dirname))
    return true;

  return base::DeleteFile(dirname, false);
}

std::string GetCrxComponentID(const CrxComponent& component) {
  const size_t kCrxIdSize = 16;
  CHECK_GE(component.pk_hash.size(), kCrxIdSize);
  return HexStringToID(base::ToLowerASCII(
      base::HexEncode(&component.pk_hash[0], kCrxIdSize)));
}

bool VerifyFileHash256(const base::FilePath& filepath,
                       const std::string& expected_hash_str) {
  std::vector<uint8_t> expected_hash;
  if (!base::HexStringToBytes(expected_hash_str, &expected_hash) ||
      expected_hash.size() != crypto::kSHA256Length) {
    return false;
  }

  base::MemoryMappedFile mmfile;
  if (!mmfile.Initialize(filepath))
    return false;

  uint8_t actual_hash[crypto::kSHA256Length] = {0};
  std::unique_ptr<crypto::SecureHash> hasher(
      crypto::SecureHash::Create(crypto::SecureHash::SHA256));
  hasher->Update(mmfile.data(), mmfile.length());
  hasher->Finish(actual_hash, sizeof(actual_hash));

  return memcmp(actual_hash, &expected_hash[0], sizeof(actual_hash)) == 0;
}

bool IsValidBrand(const std::string& brand) {
  const size_t kMaxBrandSize = 4;
  if (!brand.empty() && brand.size() != kMaxBrandSize)
    return false;

  return std::find_if_not(brand.begin(), brand.end(), [](char ch) {
           return base::IsAsciiAlpha(ch);
         }) == brand.end();
}

// Helper function.
// Returns true if |part| matches the expression
// ^[<special_chars>a-zA-Z0-9]{min_length,max_length}$
bool IsValidInstallerAttributePart(const std::string& part,
                                   const std::string& special_chars,
                                   size_t min_length,
                                   size_t max_length) {
  if (part.size() < min_length || part.size() > max_length)
    return false;

  return std::find_if_not(part.begin(), part.end(), [&special_chars](char ch) {
           if (base::IsAsciiAlpha(ch) || base::IsAsciiDigit(ch))
             return true;

           for (auto c : special_chars) {
             if (c == ch)
               return true;
           }

           return false;
         }) == part.end();
}

// Returns true if the |name| parameter matches ^[-_a-zA-Z0-9]{1,256}$ .
bool IsValidInstallerAttributeName(const std::string& name) {
  return IsValidInstallerAttributePart(name, "-_", 1, 256);
}

// Returns true if the |value| parameter matches ^[-.,;+_=a-zA-Z0-9]{0,256}$ .
bool IsValidInstallerAttributeValue(const std::string& value) {
  return IsValidInstallerAttributePart(value, "-.,;+_=", 0, 256);
}

bool IsValidInstallerAttribute(const InstallerAttribute& attr) {
  return IsValidInstallerAttributeName(attr.first) &&
         IsValidInstallerAttributeValue(attr.second);
}

void RemoveUnsecureUrls(std::vector<GURL>* urls) {
  DCHECK(urls);
  urls->erase(std::remove_if(
                  urls->begin(), urls->end(),
                  [](const GURL& url) { return !url.SchemeIsCryptographic(); }),
              urls->end());
}

CrxInstaller::Result InstallFunctionWrapper(base::Callback<bool()> callback) {
  return CrxInstaller::Result(callback.Run() ? InstallError::NONE
                                             : InstallError::GENERIC_ERROR);
}

}  // namespace update_client
