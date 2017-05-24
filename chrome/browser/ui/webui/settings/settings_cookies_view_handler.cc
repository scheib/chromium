// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/settings/settings_cookies_view_handler.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browsing_data/browsing_data_appcache_helper.h"
#include "chrome/browser/browsing_data/browsing_data_cache_storage_helper.h"
#include "chrome/browser/browsing_data/browsing_data_channel_id_helper.h"
#include "chrome/browser/browsing_data/browsing_data_cookie_helper.h"
#include "chrome/browser/browsing_data/browsing_data_database_helper.h"
#include "chrome/browser/browsing_data/browsing_data_file_system_helper.h"
#include "chrome/browser/browsing_data/browsing_data_flash_lso_helper.h"
#include "chrome/browser/browsing_data/browsing_data_indexed_db_helper.h"
#include "chrome/browser/browsing_data/browsing_data_local_storage_helper.h"
#include "chrome/browser/browsing_data/browsing_data_media_license_helper.h"
#include "chrome/browser/browsing_data/browsing_data_quota_helper.h"
#include "chrome/browser/browsing_data/browsing_data_service_worker_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/cookies_tree_model_util.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_ui.h"

namespace storage {
class FileSystemContext;
}

namespace settings {

const char kId[] = "id";
const char kChildren[] = "children";
const char kStart[] = "start";
const char kCount[] = "count";

CookiesViewHandler::CookiesViewHandler()
  : batch_update_(false),
    model_util_(new CookiesTreeModelUtil) {
}

CookiesViewHandler::~CookiesViewHandler() {
}

void CookiesViewHandler::OnJavascriptAllowed() {
}

void CookiesViewHandler::OnJavascriptDisallowed() {
}

void CookiesViewHandler::RegisterMessages() {
  EnsureCookiesTreeModelCreated();

  web_ui()->RegisterMessageCallback(
      "getCookieDetails",
      base::Bind(&CookiesViewHandler::HandleGetCookieDetails,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "updateCookieSearchResults",
      base::Bind(&CookiesViewHandler::HandleUpdateSearchResults,
                 base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeAllCookies",
      base::Bind(&CookiesViewHandler::HandleRemoveAll, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeCookie",
      base::Bind(&CookiesViewHandler::HandleRemove, base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "loadCookie", base::Bind(&CookiesViewHandler::HandleLoadChildren,
                               base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "reloadCookies", base::Bind(&CookiesViewHandler::HandleReloadCookies,
                                  base::Unretained(this)));
}

void CookiesViewHandler::TreeNodesAdded(ui::TreeModel* model,
                                        ui::TreeModelNode* parent,
                                        int start,
                                        int count) {
  // Skip if there is a batch update in progress.
  if (batch_update_)
    return;

  CookiesTreeModel* tree_model = static_cast<CookiesTreeModel*>(model);
  CookieTreeNode* parent_node = tree_model->AsNode(parent);

  std::unique_ptr<base::ListValue> children(new base::ListValue);
  // Passing false for |include_quota_nodes| since they don't reflect reality
  // until bug http://crbug.com/642955 is fixed and local/session storage is
  // counted against the total.
  model_util_->GetChildNodeList(
      parent_node, start, count, /*include_quota_nodes=*/false, children.get());

  base::DictionaryValue args;
  if (parent == tree_model->GetRoot())
    args.Set(kId, base::MakeUnique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(parent_node));
  args.SetInteger(kStart, start);
  args.Set(kChildren, std::move(children));
  FireWebUIListener("onTreeItemAdded", args);
}

void CookiesViewHandler::TreeNodesRemoved(ui::TreeModel* model,
                                          ui::TreeModelNode* parent,
                                          int start,
                                          int count) {
  // Skip if there is a batch update in progress.
  if (batch_update_)
    return;

  CookiesTreeModel* tree_model = static_cast<CookiesTreeModel*>(model);

  base::DictionaryValue args;
  if (parent == tree_model->GetRoot())
    args.Set(kId, base::MakeUnique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(tree_model->AsNode(parent)));
  args.SetInteger(kStart, start);
  args.SetInteger(kCount, count);
  FireWebUIListener("onTreeItemRemoved", args);
}

void CookiesViewHandler::TreeModelBeginBatch(CookiesTreeModel* model) {
  DCHECK(!batch_update_);  // There should be no nested batch begin.
  batch_update_ = true;
}

void CookiesViewHandler::TreeModelEndBatch(CookiesTreeModel* model) {
  DCHECK(batch_update_);
  batch_update_ = false;

  if (IsJavascriptAllowed())
    SendChildren(model->GetRoot());
}

void CookiesViewHandler::EnsureCookiesTreeModelCreated() {
  if (!cookies_tree_model_.get()) {
    Profile* profile = Profile::FromWebUI(web_ui());
    content::StoragePartition* storage_partition =
        content::BrowserContext::GetDefaultStoragePartition(profile);
    content::IndexedDBContext* indexed_db_context =
        storage_partition->GetIndexedDBContext();
    content::ServiceWorkerContext* service_worker_context =
        storage_partition->GetServiceWorkerContext();
    content::CacheStorageContext* cache_storage_context =
        storage_partition->GetCacheStorageContext();
    storage::FileSystemContext* file_system_context =
        storage_partition->GetFileSystemContext();
    LocalDataContainer* container = new LocalDataContainer(
        new BrowsingDataCookieHelper(profile->GetRequestContext()),
        new BrowsingDataDatabaseHelper(profile),
        new BrowsingDataLocalStorageHelper(profile), NULL,
        new BrowsingDataAppCacheHelper(profile),
        new BrowsingDataIndexedDBHelper(indexed_db_context),
        BrowsingDataFileSystemHelper::Create(file_system_context),
        BrowsingDataQuotaHelper::Create(profile),
        BrowsingDataChannelIDHelper::Create(profile->GetRequestContext()),
        new BrowsingDataServiceWorkerHelper(service_worker_context),
        new BrowsingDataCacheStorageHelper(cache_storage_context),
        BrowsingDataFlashLSOHelper::Create(profile),
        BrowsingDataMediaLicenseHelper::Create(file_system_context));
    cookies_tree_model_.reset(
        new CookiesTreeModel(container,
                             profile->GetExtensionSpecialStoragePolicy()));
    cookies_tree_model_->AddCookiesTreeObserver(this);
  }
}

void CookiesViewHandler::HandleUpdateSearchResults(
    const base::ListValue* args) {
  base::string16 query;
  CHECK(args->GetString(0, &query));

  cookies_tree_model_->UpdateSearchResults(query);
}

void CookiesViewHandler::HandleGetCookieDetails(const base::ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  CHECK(args->GetString(0, &callback_id_));
  std::string site;
  CHECK(args->GetString(1, &site));

  AllowJavascript();
  const CookieTreeNode* node = model_util_->GetTreeNodeFromTitle(
      cookies_tree_model_->GetRoot(), base::UTF8ToUTF16(site));

  if (!node) {
    RejectJavascriptCallback(base::Value(callback_id_), base::Value());
    callback_id_.clear();
    return;
  }

  SendCookieDetails(node);
}

void CookiesViewHandler::HandleReloadCookies(const base::ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  CHECK(args->GetString(0, &callback_id_));

  AllowJavascript();
  cookies_tree_model_.reset();
  EnsureCookiesTreeModelCreated();
}

void CookiesViewHandler::HandleRemoveAll(const base::ListValue* args) {
  CHECK_EQ(1U, args->GetSize());
  CHECK(args->GetString(0, &callback_id_));

  cookies_tree_model_->DeleteAllStoredObjects();
}

void CookiesViewHandler::HandleRemove(const base::ListValue* args) {
  std::string node_path;
  CHECK(args->GetString(0, &node_path));

  const CookieTreeNode* node = model_util_->GetTreeNodeFromPath(
      cookies_tree_model_->GetRoot(), node_path);
  if (node)
    cookies_tree_model_->DeleteCookieNode(const_cast<CookieTreeNode*>(node));
}

void CookiesViewHandler::HandleLoadChildren(const base::ListValue* args) {
  CHECK_EQ(2U, args->GetSize());
  CHECK(args->GetString(0, &callback_id_));

  std::string node_path;
  CHECK(args->GetString(1, &node_path));

  const CookieTreeNode* node = model_util_->GetTreeNodeFromPath(
      cookies_tree_model_->GetRoot(), node_path);
  if (node)
    SendChildren(node);
}

void CookiesViewHandler::SendChildren(const CookieTreeNode* parent) {
  std::unique_ptr<base::ListValue> children(new base::ListValue);
  // Passing false for |include_quota_nodes| since they don't reflect reality
  // until bug http://crbug.com/642955 is fixed and local/session storage is
  // counted against the total.
  model_util_->GetChildNodeList(parent, /*start=*/0, parent->child_count(),
      /*include_quota_nodes=*/false, children.get());

  base::DictionaryValue args;
  if (parent == cookies_tree_model_->GetRoot())
    args.Set(kId, base::MakeUnique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(parent));
  args.Set(kChildren, std::move(children));

  ResolveJavascriptCallback(base::Value(callback_id_), args);
  callback_id_.clear();
}

void CookiesViewHandler::SendCookieDetails(const CookieTreeNode* parent) {
  std::unique_ptr<base::ListValue> children(new base::ListValue);
  // Passing false for |include_quota_nodes| since they don't reflect reality
  // until bug http://crbug.com/642955 is fixed and local/session storage is
  // counted against the total.
  model_util_->GetChildNodeDetails(parent, /*start=*/0, parent->child_count(),
                                   /*include_quota_nodes=*/false,
                                   children.get());

  base::DictionaryValue args;
  if (parent == cookies_tree_model_->GetRoot())
    args.Set(kId, base::MakeUnique<base::Value>());
  else
    args.SetString(kId, model_util_->GetTreeNodeId(parent));
  args.Set(kChildren, std::move(children));

  ResolveJavascriptCallback(base::Value(callback_id_), args);
  callback_id_.clear();
}

}  // namespace settings
