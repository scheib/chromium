// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/web/external_app_launcher.h"

#include "base/ios/ios_util.h"
#include "base/logging.h"
#include "base/mac/foundation_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/sys_string_conversions.h"
#include "components/strings/grit/components_strings.h"
#include "ios/chrome/browser/experimental_flags.h"
#import "ios/chrome/browser/open_url_util.h"
#import "ios/chrome/browser/web/mailto_url_rewriter.h"
#include "ios/chrome/grit/ios_strings.h"
#import "net/base/mac/url_conversions.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"
#include "url/url_constants.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// Returns a set of NSStrings that are URL schemes for iTunes Stores.
NSSet<NSString*>* ITMSSchemes() {
  static NSSet<NSString*>* schemes;
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    schemes = [NSSet<NSString*>
        setWithObjects:@"itms", @"itmss", @"itms-apps", @"itms-appss",
                       // There's no evidence that itms-bookss is actually
                       // supported, but over-inclusion costs less than
                       // under-inclusion.
                       @"itms-books", @"itms-bookss", nil];
  });
  return schemes;
}

bool UrlHasAppStoreScheme(const GURL& gURL) {
  std::string scheme = gURL.scheme();
  return [ITMSSchemes() containsObject:base::SysUTF8ToNSString(scheme)];
}

// Logs an entry for |Tab.ExternalApplicationOpened|.  If the user decided to
// open in an external app, pass true.  Otherwise, if the user cancelled the
// opening, pass false.
void RecordExternalApplicationOpened(bool opened) {
  UMA_HISTOGRAM_BOOLEAN("Tab.ExternalApplicationOpened", opened);
}

// Returns whether gURL has the scheme of a URL that initiates a call.
bool UrlHasPhoneCallScheme(const GURL& gURL) {
  return gURL.SchemeIs("tel") || gURL.SchemeIs("facetime") ||
         gURL.SchemeIs("facetime-audio");
}

// Returns a string to be used as the label for the prompt's action button.
NSString* PromptActionString(NSString* scheme) {
  if ([scheme isEqualToString:@"facetime"])
    return l10n_util::GetNSString(IDS_IOS_FACETIME_BUTTON);
  else if ([scheme isEqualToString:@"tel"] ||
           [scheme isEqualToString:@"facetime-audio"])
    return l10n_util::GetNSString(IDS_IOS_PHONE_CALL_BUTTON);
  NOTREACHED();
  return @"";
}

}  // namespace

@interface ExternalAppLauncher ()
// Returns the Phone/FaceTime call argument from |URL|.
+ (NSString*)formatCallArgument:(NSURL*)URL;
// Presents an alert controller with |prompt| and |openLabel| as button label
// on the root view controller before launching an external app identified by
// |URL|.
- (void)openExternalAppWithURL:(NSURL*)URL
                        prompt:(NSString*)prompt
                     openLabel:(NSString*)openLabel;
@end

@implementation ExternalAppLauncher

+ (NSString*)formatCallArgument:(NSURL*)URL {
  NSCharacterSet* charSet =
      [NSCharacterSet characterSetWithCharactersInString:@"/"];
  NSString* scheme = [NSString stringWithFormat:@"%@:", [URL scheme]];
  NSString* URLString = [URL absoluteString];
  if ([URLString length] <= [scheme length])
    return URLString;
  NSString* prompt = [[[[URL absoluteString] substringFromIndex:[scheme length]]
      stringByTrimmingCharactersInSet:charSet] stringByRemovingPercentEncoding];
  // Returns original URL string if there's nothing interesting to display
  // other than the scheme itself.
  if (![prompt length])
    return URLString;
  return prompt;
}

- (void)openExternalAppWithURL:(NSURL*)URL
                        prompt:(NSString*)prompt
                     openLabel:(NSString*)openLabel {
  UIAlertController* alertController =
      [UIAlertController alertControllerWithTitle:nil
                                          message:prompt
                                   preferredStyle:UIAlertControllerStyleAlert];
  UIAlertAction* openAction =
      [UIAlertAction actionWithTitle:openLabel
                               style:UIAlertActionStyleDefault
                             handler:^(UIAlertAction* action) {
                               RecordExternalApplicationOpened(true);
                               OpenUrlWithCompletionHandler(URL, nil);
                             }];
  UIAlertAction* cancelAction =
      [UIAlertAction actionWithTitle:l10n_util::GetNSString(IDS_CANCEL)
                               style:UIAlertActionStyleCancel
                             handler:^(UIAlertAction* action) {
                               RecordExternalApplicationOpened(false);
                             }];
  [alertController addAction:cancelAction];
  [alertController addAction:openAction];

  [[[[UIApplication sharedApplication] keyWindow] rootViewController]
      presentViewController:alertController
                   animated:YES
                 completion:nil];
}

- (BOOL)openURL:(const GURL&)gURL linkClicked:(BOOL)linkClicked {
  if (!gURL.is_valid() || !gURL.has_scheme())
    return NO;

  // Don't open external application if chrome is not active.
  if ([[UIApplication sharedApplication] applicationState] !=
      UIApplicationStateActive)
    return NO;

  NSURL* URL = net::NSURLWithGURL(gURL);
  if (base::ios::IsRunningOnOrLater(10, 3, 0)) {
    if (UrlHasAppStoreScheme(gURL)) {
      NSString* prompt = l10n_util::GetNSString(IDS_IOS_OPEN_IN_ANOTHER_APP);
      NSString* openLabel =
          l10n_util::GetNSString(IDS_IOS_APP_LAUNCHER_OPEN_APP_BUTTON_LABEL);
      [self openExternalAppWithURL:URL prompt:prompt openLabel:openLabel];
      return YES;
    }
  } else {
    // Prior to iOS 10.3, iOS does not prompt user when facetime: and
    // facetime-audio: URL schemes are opened, so Chrome needs to present an
    // alert before placing a phone call.
    if (UrlHasPhoneCallScheme(gURL)) {
      [self openExternalAppWithURL:URL
                            prompt:[[self class] formatCallArgument:URL]
                         openLabel:PromptActionString([URL scheme])];
      return YES;
    }
    // Prior to iOS 10.3, Chrome prompts user with an alert before opening
    // App Store when user did not tap on any links and an iTunes app URL is
    // opened. This maintains parity with Safari in pre-10.3 environment.
    if (!linkClicked && UrlHasAppStoreScheme(gURL)) {
      NSString* prompt = l10n_util::GetNSString(IDS_IOS_OPEN_IN_ANOTHER_APP);
      NSString* openLabel =
          l10n_util::GetNSString(IDS_IOS_APP_LAUNCHER_OPEN_APP_BUTTON_LABEL);
      [self openExternalAppWithURL:URL prompt:prompt openLabel:openLabel];
      return YES;
    }
  }

  // Replaces |URL| with a rewritten URL if it is of mailto: scheme.
  if (!experimental_flags::IsNativeAppLauncherEnabled() &&
      gURL.SchemeIs(url::kMailToScheme)) {
    MailtoURLRewriter* rewriter =
        [[MailtoURLRewriter alloc] initWithStandardHandlers];
    NSString* launchURL = [rewriter rewriteMailtoURL:gURL];
    if (launchURL)
      URL = [NSURL URLWithString:launchURL];
    UMA_HISTOGRAM_BOOLEAN("IOS.MailtoURLRewritten", launchURL != nil);
  }

  // If the following call returns YES, an external application is about to be
  // launched and Chrome will go into the background now.
  // TODO(crbug.com/622735): This call still needs to be updated.
  // It's heavily nested so some refactoring is needed.
  return [[UIApplication sharedApplication] openURL:URL];
}

@end
