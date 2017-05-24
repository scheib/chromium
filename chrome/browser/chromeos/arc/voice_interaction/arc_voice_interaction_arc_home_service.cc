// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/arc/voice_interaction/arc_voice_interaction_arc_home_service.h"

#include <utility>
#include <vector>

#include "ash/shell.h"
#include "base/bind.h"
#include "base/logging.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/chromeos/first_run/first_run.h"
#include "chrome/browser/ui/ash/launcher/chrome_launcher_controller.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/arc/arc_bridge_service.h"
#include "components/arc/instance_holder.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/web_contents.h"
#include "ui/accessibility/platform/ax_snapshot_node_android_platform.h"
#include "ui/aura/window.h"
#include "ui/snapshot/snapshot.h"
#include "ui/wm/public/activation_client.h"

namespace arc {

namespace {

mojom::VoiceInteractionStructurePtr CreateVoiceInteractionStructure(
    const ui::AXSnapshotNodeAndroid& view_structure) {
  auto structure = mojom::VoiceInteractionStructure::New();
  structure->text = view_structure.text;
  structure->text_size = view_structure.text_size;

  structure->bold = view_structure.bold;
  structure->italic = view_structure.italic;
  structure->underline = view_structure.underline;
  structure->line_through = view_structure.line_through;
  structure->color = view_structure.color;
  structure->bgcolor = view_structure.bgcolor;

  structure->class_name = view_structure.class_name;
  structure->rect = view_structure.rect;

  if (view_structure.has_selection) {
    auto selection = mojom::TextSelection::New();
    selection->start_selection = view_structure.start_selection;
    selection->end_selection = view_structure.end_selection;
    structure->selection = std::move(selection);
  }

  for (auto& child : view_structure.children)
    structure->children.push_back(CreateVoiceInteractionStructure(*child));

  return structure;
}

void RequestVoiceInteractionStructureCallback(
    const base::Callback<void(mojom::VoiceInteractionStructurePtr)>& callback,
    const ui::AXTreeUpdate& update) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  callback.Run(CreateVoiceInteractionStructure(
      *ui::AXSnapshotNodeAndroid::Create(update, false)));
}

}  // namespace

ArcVoiceInteractionArcHomeService::ArcVoiceInteractionArcHomeService(
    ArcBridgeService* bridge_service)
    : ArcService(bridge_service), binding_(this) {
  arc_bridge_service()->voice_interaction_arc_home()->AddObserver(this);
}

ArcVoiceInteractionArcHomeService::~ArcVoiceInteractionArcHomeService() {
  arc_bridge_service()->voice_interaction_arc_home()->RemoveObserver(this);
}

void ArcVoiceInteractionArcHomeService::OnInstanceReady() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  mojom::VoiceInteractionArcHomeInstance* home_instance =
      ARC_GET_INSTANCE_FOR_METHOD(
          arc_bridge_service()->voice_interaction_arc_home(), Init);
  DCHECK(home_instance);
  home_instance->Init(binding_.CreateInterfacePtrAndBind());
}

void ArcVoiceInteractionArcHomeService::GetVoiceInteractionStructure(
    const GetVoiceInteractionStructureCallback& callback) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  if (!browser || !browser->window()->IsActive()) {
    // TODO(muyuanli): retrieve context for apps.
    LOG(ERROR) << "Retrieving context from apps is not implemented.";
    callback.Run(mojom::VoiceInteractionStructure::New());
    return;
  }

  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  // Do not process incognito tab.
  if (web_contents->GetBrowserContext()->IsOffTheRecord()) {
    callback.Run(mojom::VoiceInteractionStructure::New());
    return;
  }

  web_contents->RequestAXTreeSnapshot(
      base::Bind(&RequestVoiceInteractionStructureCallback, callback));
}

void ArcVoiceInteractionArcHomeService::OnVoiceInteractionOobeSetupComplete() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  chromeos::first_run::MaybeLaunchDialogImmediately();
}

// static
mojom::VoiceInteractionStructurePtr
ArcVoiceInteractionArcHomeService::CreateVoiceInteractionStructureForTesting(
    const ui::AXSnapshotNodeAndroid& view_structure) {
  return CreateVoiceInteractionStructure(view_structure);
}

}  // namespace arc
