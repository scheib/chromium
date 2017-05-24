// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/android/content_settings/subresource_filter_infobar_delegate.h"

#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/android/android_theme_resources.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/subresource_filter/chrome_subresource_filter_client.h"
#include "chrome/grit/generated_resources.h"
#include "components/infobars/core/infobar.h"
#include "components/strings/grit/components_strings.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/l10n/l10n_util.h"

// static
void SubresourceFilterInfobarDelegate::Create(InfoBarService* infobar_service) {
  infobar_service->AddInfoBar(CreateSubresourceFilterInfoBar(
      base::WrapUnique(new SubresourceFilterInfobarDelegate())));
}

SubresourceFilterInfobarDelegate::SubresourceFilterInfobarDelegate()
    : ConfirmInfoBarDelegate(),
      using_experimental_infobar_(base::FeatureList::IsEnabled(
          subresource_filter::kSafeBrowsingSubresourceFilterExperimentalUI)) {}

SubresourceFilterInfobarDelegate::~SubresourceFilterInfobarDelegate() {}

base::string16 SubresourceFilterInfobarDelegate::GetExplanationText() const {
  return l10n_util::GetStringUTF16(
      IDS_FILTERED_DECEPTIVE_CONTENT_PROMPT_EXPLANATION);
}

// TODO(csharrison): For the time being the experimental infobar will use the
// PROMPT_RELOAD string for the toggle text. Update this when launch strings are
// finalized.
base::string16 SubresourceFilterInfobarDelegate::GetToggleText() const {
  DCHECK(using_experimental_infobar_);
  return l10n_util::GetStringUTF16(
      IDS_FILTERED_DECEPTIVE_CONTENT_PROMPT_RELOAD);
}

bool SubresourceFilterInfobarDelegate::ShouldShowExperimentalInfobar() const {
  return using_experimental_infobar_;
}

infobars::InfoBarDelegate::InfoBarIdentifier
SubresourceFilterInfobarDelegate::GetIdentifier() const {
  return SUBRESOURCE_FILTER_INFOBAR_DELEGATE_ANDROID;
}

int SubresourceFilterInfobarDelegate::GetIconId() const {
  return IDR_ANDROID_INFOBAR_SUBRESOURCE_FILTERING;
}

base::string16 SubresourceFilterInfobarDelegate::GetMessageText() const {
  return l10n_util::GetStringUTF16(IDS_FILTERED_DECEPTIVE_CONTENT_PROMPT_TITLE);
}

int SubresourceFilterInfobarDelegate::GetButtons() const {
  return BUTTON_OK | BUTTON_CANCEL;
}

base::string16 SubresourceFilterInfobarDelegate::GetButtonLabel(
    InfoBarButton button) const {
  if (button == BUTTON_OK)
    return l10n_util::GetStringUTF16(IDS_OK);

  return l10n_util::GetStringUTF16(
      using_experimental_infobar_
          ? IDS_APP_MENU_RELOAD
          : IDS_FILTERED_DECEPTIVE_CONTENT_PROMPT_RELOAD);
}

bool SubresourceFilterInfobarDelegate::Cancel() {
  content::WebContents* web_contents =
      InfoBarService::WebContentsFromInfoBar(infobar());
  ChromeSubresourceFilterClient::FromWebContents(web_contents)
      ->OnReloadRequested();
  return true;
}

bool SubresourceFilterInfobarDelegate::LinkClicked(
    WindowOpenDisposition disposition) {
  ChromeSubresourceFilterClient::LogAction(kActionDetailsShown);
  return false;
}
