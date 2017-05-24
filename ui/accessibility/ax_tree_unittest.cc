// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/ax_tree.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_serializable_tree.h"
#include "ui/accessibility/ax_tree_serializer.h"

using base::DoubleToString;
using base::IntToString;
using base::StringPrintf;

namespace ui {

namespace {

std::string IntVectorToString(const std::vector<int>& items) {
  std::string str;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i > 0)
      str += ",";
    str += IntToString(items[i]);
  }
  return str;
}

class FakeAXTreeDelegate : public AXTreeDelegate {
 public:
  FakeAXTreeDelegate()
      : tree_data_changed_(false),
        root_changed_(false) {}

  void OnNodeDataWillChange(AXTree* tree,
                            const AXNodeData& old_node_data,
                            const AXNodeData& new_node_data) override {}
  void OnTreeDataChanged(AXTree* tree) override {
    tree_data_changed_ = true;
  }

  void OnNodeWillBeDeleted(AXTree* tree, AXNode* node) override {
    deleted_ids_.push_back(node->id());
  }

  void OnSubtreeWillBeDeleted(AXTree* tree, AXNode* node) override {
    subtree_deleted_ids_.push_back(node->id());
  }

  void OnNodeWillBeReparented(AXTree* tree, AXNode* node) override {}

  void OnSubtreeWillBeReparented(AXTree* tree, AXNode* node) override {}

  void OnNodeCreated(AXTree* tree, AXNode* node) override {
    created_ids_.push_back(node->id());
  }

  void OnNodeReparented(AXTree* tree, AXNode* node) override {}

  void OnNodeChanged(AXTree* tree, AXNode* node) override {
    changed_ids_.push_back(node->id());
  }

  void OnAtomicUpdateFinished(AXTree* tree,
                              bool root_changed,
                              const std::vector<Change>& changes) override {
    root_changed_ = root_changed;

    for (size_t i = 0; i < changes.size(); ++i) {
      int id = changes[i].node->id();
      switch (changes[i].type) {
        case NODE_CREATED:
          node_creation_finished_ids_.push_back(id);
          break;
        case SUBTREE_CREATED:
          subtree_creation_finished_ids_.push_back(id);
          break;
        case NODE_REPARENTED:
          node_reparented_finished_ids_.push_back(id);
          break;
        case SUBTREE_REPARENTED:
          subtree_reparented_finished_ids_.push_back(id);
          break;
        case NODE_CHANGED:
          change_finished_ids_.push_back(id);
          break;
      }
    }
  }

  void OnRoleChanged(AXTree* tree,
                     AXNode* node,
                     AXRole old_role,
                     AXRole new_role) override {
    attribute_change_log_.push_back(StringPrintf("Role changed from %s to %s",
                                                 ToString(old_role).c_str(),
                                                 ToString(new_role).c_str()));
  }

  void OnStateChanged(AXTree* tree,
                      AXNode* node,
                      AXState state,
                      bool new_value) override {
    attribute_change_log_.push_back(StringPrintf("%s changed to %s",
                                                 ToString(state).c_str(),
                                                 new_value ? "true" : "false"));
  }

  void OnStringAttributeChanged(AXTree* tree,
                                AXNode* node,
                                AXStringAttribute attr,
                                const std::string& old_value,
                                const std::string& new_value) override {
    attribute_change_log_.push_back(
        StringPrintf("%s changed from %s to %s", ToString(attr).c_str(),
                     old_value.c_str(), new_value.c_str()));
  }

  void OnIntAttributeChanged(AXTree* tree,
                             AXNode* node,
                             AXIntAttribute attr,
                             int32_t old_value,
                             int32_t new_value) override {
    attribute_change_log_.push_back(StringPrintf("%s changed from %d to %d",
                                                 ToString(attr).c_str(),
                                                 old_value, new_value));
  }

  void OnFloatAttributeChanged(AXTree* tree,
                               AXNode* node,
                               AXFloatAttribute attr,
                               float old_value,
                               float new_value) override {
    attribute_change_log_.push_back(StringPrintf(
        "%s changed from %s to %s", ToString(attr).c_str(),
        DoubleToString(old_value).c_str(), DoubleToString(new_value).c_str()));
  }

  void OnBoolAttributeChanged(AXTree* tree,
                              AXNode* node,
                              AXBoolAttribute attr,
                              bool new_value) override {
    attribute_change_log_.push_back(StringPrintf("%s changed to %s",
                                                 ToString(attr).c_str(),
                                                 new_value ? "true" : "false"));
  }

  void OnIntListAttributeChanged(
      AXTree* tree,
      AXNode* node,
      AXIntListAttribute attr,
      const std::vector<int32_t>& old_value,
      const std::vector<int32_t>& new_value) override {
    attribute_change_log_.push_back(
        StringPrintf("%s changed from %s to %s", ToString(attr).c_str(),
                     IntVectorToString(old_value).c_str(),
                     IntVectorToString(new_value).c_str()));
  }

  bool tree_data_changed() const { return tree_data_changed_; }
  bool root_changed() const { return root_changed_; }
  const std::vector<int32_t>& deleted_ids() { return deleted_ids_; }
  const std::vector<int32_t>& subtree_deleted_ids() {
    return subtree_deleted_ids_;
  }
  const std::vector<int32_t>& created_ids() { return created_ids_; }
  const std::vector<int32_t>& node_creation_finished_ids() {
    return node_creation_finished_ids_;
  }
  const std::vector<int32_t>& subtree_creation_finished_ids() {
    return subtree_creation_finished_ids_;
  }
  const std::vector<int32_t>& node_reparented_finished_ids() {
    return node_reparented_finished_ids_;
  }
  const std::vector<int32_t>& subtree_reparented_finished_ids() {
    return subtree_reparented_finished_ids_;
  }
  const std::vector<int32_t>& change_finished_ids() {
    return change_finished_ids_;
  }
  const std::vector<std::string>& attribute_change_log() {
    return attribute_change_log_;
  }

 private:
  bool tree_data_changed_;
  bool root_changed_;
  std::vector<int32_t> deleted_ids_;
  std::vector<int32_t> subtree_deleted_ids_;
  std::vector<int32_t> created_ids_;
  std::vector<int32_t> changed_ids_;
  std::vector<int32_t> node_creation_finished_ids_;
  std::vector<int32_t> subtree_creation_finished_ids_;
  std::vector<int32_t> node_reparented_finished_ids_;
  std::vector<int32_t> subtree_reparented_finished_ids_;
  std::vector<int32_t> change_finished_ids_;
  std::vector<std::string> attribute_change_log_;
};

}  // namespace

TEST(AXTreeTest, SerializeSimpleAXTree) {
  AXNodeData root;
  root.id = 1;
  root.role = AX_ROLE_DIALOG;
  root.AddState(AX_STATE_FOCUSABLE);
  root.location = gfx::RectF(0, 0, 800, 600);
  root.child_ids.push_back(2);
  root.child_ids.push_back(3);

  AXNodeData button;
  button.id = 2;
  button.role = AX_ROLE_BUTTON;
  button.location = gfx::RectF(20, 20, 200, 30);

  AXNodeData checkbox;
  checkbox.id = 3;
  checkbox.role = AX_ROLE_CHECK_BOX;
  checkbox.location = gfx::RectF(20, 50, 200, 30);

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.push_back(root);
  initial_state.nodes.push_back(button);
  initial_state.nodes.push_back(checkbox);
  initial_state.has_tree_data = true;
  initial_state.tree_data.title = "Title";
  AXSerializableTree src_tree(initial_state);

  std::unique_ptr<AXTreeSource<const AXNode*, AXNodeData, AXTreeData>>
      tree_source(src_tree.CreateTreeSource());
  AXTreeSerializer<const AXNode*, AXNodeData, AXTreeData> serializer(
      tree_source.get());
  AXTreeUpdate update;
  serializer.SerializeChanges(src_tree.root(), &update);

  AXTree dst_tree;
  ASSERT_TRUE(dst_tree.Unserialize(update));

  const AXNode* root_node = dst_tree.root();
  ASSERT_TRUE(root_node != NULL);
  EXPECT_EQ(root.id, root_node->id());
  EXPECT_EQ(root.role, root_node->data().role);

  ASSERT_EQ(2, root_node->child_count());

  const AXNode* button_node = root_node->ChildAtIndex(0);
  EXPECT_EQ(button.id, button_node->id());
  EXPECT_EQ(button.role, button_node->data().role);

  const AXNode* checkbox_node = root_node->ChildAtIndex(1);
  EXPECT_EQ(checkbox.id, checkbox_node->id());
  EXPECT_EQ(checkbox.role, checkbox_node->data().role);

  EXPECT_EQ(
      "AXTree title=Title\n"
      "id=1 dialog FOCUSABLE (0, 0)-(800, 600) actions= child_ids=2,3\n"
      "  id=2 button (20, 20)-(200, 30) actions=\n"
      "  id=3 checkBox (20, 50)-(200, 30) actions=\n",
      dst_tree.ToString());
}

TEST(AXTreeTest, SerializeAXTreeUpdate) {
  AXNodeData list;
  list.id = 3;
  list.role = AX_ROLE_LIST;
  list.child_ids.push_back(4);
  list.child_ids.push_back(5);
  list.child_ids.push_back(6);

  AXNodeData list_item_2;
  list_item_2.id = 5;
  list_item_2.role = AX_ROLE_LIST_ITEM;

  AXNodeData list_item_3;
  list_item_3.id = 6;
  list_item_3.role = AX_ROLE_LIST_ITEM;

  AXNodeData button;
  button.id = 7;
  button.role = AX_ROLE_BUTTON;

  AXTreeUpdate update;
  update.root_id = 3;
  update.nodes.push_back(list);
  update.nodes.push_back(list_item_2);
  update.nodes.push_back(list_item_3);
  update.nodes.push_back(button);

  EXPECT_EQ(
      "AXTreeUpdate: root id 3\n"
      "id=3 list (0, 0)-(0, 0) actions= child_ids=4,5,6\n"
      "  id=5 listItem (0, 0)-(0, 0) actions=\n"
      "  id=6 listItem (0, 0)-(0, 0) actions=\n"
      "id=7 button (0, 0)-(0, 0) actions=\n",
      update.ToString());
}

TEST(AXTreeTest, DeleteUnknownSubtreeFails) {
  AXNodeData root;
  root.id = 1;

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.push_back(root);
  AXTree tree(initial_state);

  // This should fail because we're asking it to delete
  // a subtree rooted at id=2, which doesn't exist.
  AXTreeUpdate update;
  update.node_id_to_clear = 2;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Bad node_id_to_clear: 2", tree.error());
}

TEST(AXTreeTest, LeaveOrphanedDeletedSubtreeFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[0].child_ids.push_back(3);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[2].id = 3;
  AXTree tree(initial_state);

  // This should fail because we delete a subtree rooted at id=2
  // but never update it.
  AXTreeUpdate update;
  update.node_id_to_clear = 2;
  update.nodes.resize(1);
  update.nodes[0].id = 3;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Nodes left pending by the update: 2", tree.error());
}

TEST(AXTreeTest, LeaveOrphanedNewChildFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  AXTree tree(initial_state);

  // This should fail because we add a new child to the root node
  // but never update it.
  AXTreeUpdate update;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(2);
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Nodes left pending by the update: 2", tree.error());
}

TEST(AXTreeTest, DuplicateChildIdFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  AXTree tree(initial_state);

  // This should fail because a child id appears twice.
  AXTreeUpdate update;
  update.nodes.resize(2);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(2);
  update.nodes[0].child_ids.push_back(2);
  update.nodes[1].id = 2;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Node 1 has duplicate child id 2", tree.error());
}

TEST(AXTreeTest, InvalidReparentingFails) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].child_ids.push_back(3);
  initial_state.nodes[2].id = 3;

  AXTree tree(initial_state);

  // This should fail because node 3 is reparented from node 2 to node 1
  // without deleting node 1's subtree first.
  AXTreeUpdate update;
  update.nodes.resize(3);
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(3);
  update.nodes[0].child_ids.push_back(2);
  update.nodes[1].id = 2;
  update.nodes[2].id = 3;
  EXPECT_FALSE(tree.Unserialize(update));
  ASSERT_EQ("Node 3 reparented from 2 to 1", tree.error());
}

TEST(AXTreeTest, TreeDelegateIsCalled) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(2);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;

  AXTree tree(initial_state);
  AXTreeUpdate update;
  update.node_id_to_clear = 1;
  update.root_id = 3;
  update.nodes.resize(2);
  update.nodes[0].id = 3;
  update.nodes[0].child_ids.push_back(4);
  update.nodes[1].id = 4;

  FakeAXTreeDelegate fake_delegate;
  tree.SetDelegate(&fake_delegate);

  EXPECT_TRUE(tree.Unserialize(update));

  ASSERT_EQ(2U, fake_delegate.deleted_ids().size());
  EXPECT_EQ(1, fake_delegate.deleted_ids()[0]);
  EXPECT_EQ(2, fake_delegate.deleted_ids()[1]);

  ASSERT_EQ(1U, fake_delegate.subtree_deleted_ids().size());
  EXPECT_EQ(1, fake_delegate.subtree_deleted_ids()[0]);

  ASSERT_EQ(2U, fake_delegate.created_ids().size());
  EXPECT_EQ(3, fake_delegate.created_ids()[0]);
  EXPECT_EQ(4, fake_delegate.created_ids()[1]);

  ASSERT_EQ(1U, fake_delegate.subtree_creation_finished_ids().size());
  EXPECT_EQ(3, fake_delegate.subtree_creation_finished_ids()[0]);

  ASSERT_EQ(1U, fake_delegate.node_creation_finished_ids().size());
  EXPECT_EQ(4, fake_delegate.node_creation_finished_ids()[0]);

  ASSERT_EQ(true, fake_delegate.root_changed());

  tree.SetDelegate(NULL);
}

TEST(AXTreeTest, TreeDelegateIsCalledForTreeDataChanges) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.has_tree_data = true;
  initial_state.tree_data.title = "Initial";
  AXTree tree(initial_state);

  FakeAXTreeDelegate fake_delegate;
  tree.SetDelegate(&fake_delegate);

  // An empty update shouldn't change tree data.
  AXTreeUpdate empty_update;
  EXPECT_TRUE(tree.Unserialize(empty_update));
  EXPECT_FALSE(fake_delegate.tree_data_changed());
  EXPECT_EQ("Initial", tree.data().title);

  // An update with tree data shouldn't change tree data if
  // |has_tree_data| isn't set.
  AXTreeUpdate ignored_tree_data_update;
  ignored_tree_data_update.tree_data.title = "Ignore Me";
  EXPECT_TRUE(tree.Unserialize(ignored_tree_data_update));
  EXPECT_FALSE(fake_delegate.tree_data_changed());
  EXPECT_EQ("Initial", tree.data().title);

  // An update with |has_tree_data| set should update the tree data.
  AXTreeUpdate tree_data_update;
  tree_data_update.has_tree_data = true;
  tree_data_update.tree_data.title = "New Title";
  EXPECT_TRUE(tree.Unserialize(tree_data_update));
  EXPECT_TRUE(fake_delegate.tree_data_changed());
  EXPECT_EQ("New Title", tree.data().title);

  tree.SetDelegate(NULL);
}

TEST(AXTreeTest, ReparentingDoesNotTriggerNodeCreated) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(3);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;
  initial_state.nodes[1].child_ids.push_back(3);
  initial_state.nodes[2].id = 3;

  FakeAXTreeDelegate fake_delegate;
  AXTree tree(initial_state);
  tree.SetDelegate(&fake_delegate);

  AXTreeUpdate update;
  update.nodes.resize(2);
  update.node_id_to_clear = 2;
  update.root_id = 1;
  update.nodes[0].id = 1;
  update.nodes[0].child_ids.push_back(3);
  update.nodes[1].id = 3;
  EXPECT_TRUE(tree.Unserialize(update)) << tree.error();
  std::vector<int> created = fake_delegate.node_creation_finished_ids();
  std::vector<int> subtree_reparented =
      fake_delegate.subtree_reparented_finished_ids();
  std::vector<int> node_reparented =
      fake_delegate.node_reparented_finished_ids();
  ASSERT_EQ(std::find(created.begin(), created.end(), 3), created.end());
  ASSERT_NE(std::find(subtree_reparented.begin(), subtree_reparented.end(), 3),
            subtree_reparented.end());
  ASSERT_EQ(std::find(node_reparented.begin(), node_reparented.end(), 3),
            node_reparented.end());
}

TEST(AXTreeTest, TreeDelegateIsNotCalledForReparenting) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(2);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].child_ids.push_back(2);
  initial_state.nodes[1].id = 2;

  AXTree tree(initial_state);
  AXTreeUpdate update;
  update.node_id_to_clear = 1;
  update.root_id = 2;
  update.nodes.resize(2);
  update.nodes[0].id = 2;
  update.nodes[0].child_ids.push_back(4);
  update.nodes[1].id = 4;

  FakeAXTreeDelegate fake_delegate;
  tree.SetDelegate(&fake_delegate);

  EXPECT_TRUE(tree.Unserialize(update));

  ASSERT_EQ(1U, fake_delegate.deleted_ids().size());
  EXPECT_EQ(1, fake_delegate.deleted_ids()[0]);

  ASSERT_EQ(1U, fake_delegate.subtree_deleted_ids().size());
  EXPECT_EQ(1, fake_delegate.subtree_deleted_ids()[0]);

  ASSERT_EQ(1U, fake_delegate.created_ids().size());
  EXPECT_EQ(4, fake_delegate.created_ids()[0]);

  ASSERT_EQ(1U, fake_delegate.subtree_creation_finished_ids().size());
  EXPECT_EQ(4, fake_delegate.subtree_creation_finished_ids()[0]);

  ASSERT_EQ(1U, fake_delegate.subtree_reparented_finished_ids().size());
  EXPECT_EQ(2, fake_delegate.subtree_reparented_finished_ids()[0]);

  EXPECT_EQ(0U, fake_delegate.node_creation_finished_ids().size());
  EXPECT_EQ(0U, fake_delegate.node_reparented_finished_ids().size());

  ASSERT_EQ(true, fake_delegate.root_changed());

  tree.SetDelegate(NULL);
}

// UAF caught by ax_tree_fuzzer
TEST(AXTreeTest, BogusAXTree) {
  AXTreeUpdate initial_state;
  AXNodeData node;
  node.id = 0;
  initial_state.nodes.push_back(node);
  initial_state.nodes.push_back(node);
  ui::AXTree tree;
  tree.Unserialize(initial_state);
}

// UAF caught by ax_tree_fuzzer
TEST(AXTreeTest, BogusAXTree2) {
  AXTreeUpdate initial_state;
  AXNodeData node;
  node.id = 0;
  initial_state.nodes.push_back(node);
  AXNodeData node2;
  node2.id = 0;
  node2.child_ids.push_back(0);
  node2.child_ids.push_back(0);
  initial_state.nodes.push_back(node2);
  ui::AXTree tree;
  tree.Unserialize(initial_state);
}

// UAF caught by ax_tree_fuzzer
TEST(AXTreeTest, BogusAXTree3) {
  AXTreeUpdate initial_state;
  AXNodeData node;
  node.id = 0;
  node.child_ids.push_back(1);
  initial_state.nodes.push_back(node);

  AXNodeData node2;
  node2.id = 1;
  node2.child_ids.push_back(1);
  node2.child_ids.push_back(1);
  initial_state.nodes.push_back(node2);

  ui::AXTree tree;
  tree.Unserialize(initial_state);
}

TEST(AXTreeTest, RoleAndStateChangeCallbacks) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].role = AX_ROLE_BUTTON;
  initial_state.nodes[0].AddIntAttribute(ui::AX_ATTR_CHECKED_STATE,
                                         ui::AX_CHECKED_STATE_TRUE);
  initial_state.nodes[0].AddState(AX_STATE_FOCUSABLE);
  AXTree tree(initial_state);

  FakeAXTreeDelegate fake_delegate;
  tree.SetDelegate(&fake_delegate);

  // Change the role and state.
  AXTreeUpdate update;
  update.root_id = 1;
  update.nodes.resize(1);
  update.nodes[0].id = 1;
  update.nodes[0].role = AX_ROLE_CHECK_BOX;
  update.nodes[0].AddIntAttribute(ui::AX_ATTR_CHECKED_STATE,
                                  ui::AX_CHECKED_STATE_FALSE);
  update.nodes[0].AddState(AX_STATE_FOCUSABLE);
  update.nodes[0].AddState(AX_STATE_VISITED);
  EXPECT_TRUE(tree.Unserialize(update));

  const std::vector<std::string>& change_log =
      fake_delegate.attribute_change_log();
  ASSERT_EQ(3U, change_log.size());
  EXPECT_EQ("Role changed from button to checkBox", change_log[0]);
  EXPECT_EQ("visited changed to true", change_log[1]);
  EXPECT_EQ("checkedState changed from 2 to 1", change_log[2]);

  tree.SetDelegate(NULL);
}

TEST(AXTreeTest, AttributeChangeCallbacks) {
  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddStringAttribute(AX_ATTR_NAME, "N1");
  initial_state.nodes[0].AddStringAttribute(AX_ATTR_DESCRIPTION, "D1");
  initial_state.nodes[0].AddBoolAttribute(AX_ATTR_LIVE_ATOMIC, true);
  initial_state.nodes[0].AddBoolAttribute(AX_ATTR_LIVE_BUSY, false);
  initial_state.nodes[0].AddFloatAttribute(AX_ATTR_MIN_VALUE_FOR_RANGE, 1.0);
  initial_state.nodes[0].AddFloatAttribute(AX_ATTR_MAX_VALUE_FOR_RANGE, 10.0);
  initial_state.nodes[0].AddIntAttribute(AX_ATTR_SCROLL_X, 5);
  initial_state.nodes[0].AddIntAttribute(AX_ATTR_SCROLL_X_MIN, 1);
  AXTree tree(initial_state);

  FakeAXTreeDelegate fake_delegate;
  tree.SetDelegate(&fake_delegate);

  // Change existing attributes.
  AXTreeUpdate update0;
  update0.root_id = 1;
  update0.nodes.resize(1);
  update0.nodes[0].id = 1;
  update0.nodes[0].AddStringAttribute(AX_ATTR_NAME, "N2");
  update0.nodes[0].AddStringAttribute(AX_ATTR_DESCRIPTION, "D2");
  update0.nodes[0].AddBoolAttribute(AX_ATTR_LIVE_ATOMIC, false);
  update0.nodes[0].AddBoolAttribute(AX_ATTR_LIVE_BUSY, true);
  update0.nodes[0].AddFloatAttribute(AX_ATTR_MIN_VALUE_FOR_RANGE, 2.0);
  update0.nodes[0].AddFloatAttribute(AX_ATTR_MAX_VALUE_FOR_RANGE, 9.0);
  update0.nodes[0].AddIntAttribute(AX_ATTR_SCROLL_X, 6);
  update0.nodes[0].AddIntAttribute(AX_ATTR_SCROLL_X_MIN, 2);
  EXPECT_TRUE(tree.Unserialize(update0));

  const std::vector<std::string>& change_log =
      fake_delegate.attribute_change_log();
  ASSERT_EQ(8U, change_log.size());
  EXPECT_EQ("name changed from N1 to N2", change_log[0]);
  EXPECT_EQ("description changed from D1 to D2", change_log[1]);
  EXPECT_EQ("liveAtomic changed to false", change_log[2]);
  EXPECT_EQ("liveBusy changed to true", change_log[3]);
  EXPECT_EQ("minValueForRange changed from 1 to 2", change_log[4]);
  EXPECT_EQ("maxValueForRange changed from 10 to 9", change_log[5]);
  EXPECT_EQ("scrollX changed from 5 to 6", change_log[6]);
  EXPECT_EQ("scrollXMin changed from 1 to 2", change_log[7]);

  FakeAXTreeDelegate fake_delegate2;
  tree.SetDelegate(&fake_delegate2);

  // Add and remove attributes.
  AXTreeUpdate update1;
  update1.root_id = 1;
  update1.nodes.resize(1);
  update1.nodes[0].id = 1;
  update1.nodes[0].AddStringAttribute(AX_ATTR_DESCRIPTION, "D3");
  update1.nodes[0].AddStringAttribute(AX_ATTR_VALUE, "V3");
  update1.nodes[0].AddBoolAttribute(AX_ATTR_MODAL, true);
  update1.nodes[0].AddFloatAttribute(AX_ATTR_VALUE_FOR_RANGE, 5.0);
  update1.nodes[0].AddFloatAttribute(AX_ATTR_MAX_VALUE_FOR_RANGE, 9.0);
  update1.nodes[0].AddIntAttribute(AX_ATTR_SCROLL_X, 7);
  update1.nodes[0].AddIntAttribute(AX_ATTR_SCROLL_X_MAX, 10);
  EXPECT_TRUE(tree.Unserialize(update1));

  const std::vector<std::string>& change_log2 =
      fake_delegate2.attribute_change_log();
  ASSERT_EQ(10U, change_log2.size());
  EXPECT_EQ("name changed from N2 to ", change_log2[0]);
  EXPECT_EQ("description changed from D2 to D3", change_log2[1]);
  EXPECT_EQ("value changed from  to V3", change_log2[2]);
  EXPECT_EQ("liveBusy changed to false", change_log2[3]);
  EXPECT_EQ("modal changed to true", change_log2[4]);
  EXPECT_EQ("minValueForRange changed from 2 to 0", change_log2[5]);
  EXPECT_EQ("valueForRange changed from 0 to 5", change_log2[6]);
  EXPECT_EQ("scrollXMin changed from 2 to 0", change_log2[7]);
  EXPECT_EQ("scrollX changed from 6 to 7", change_log2[8]);
  EXPECT_EQ("scrollXMax changed from 0 to 10", change_log2[9]);

  tree.SetDelegate(NULL);
}

TEST(AXTreeTest, IntListChangeCallbacks) {
  std::vector<int32_t> one;
  one.push_back(1);

  std::vector<int32_t> two;
  two.push_back(2);
  two.push_back(2);

  std::vector<int32_t> three;
  three.push_back(3);

  AXTreeUpdate initial_state;
  initial_state.root_id = 1;
  initial_state.nodes.resize(1);
  initial_state.nodes[0].id = 1;
  initial_state.nodes[0].AddIntListAttribute(AX_ATTR_CONTROLS_IDS, one);
  initial_state.nodes[0].AddIntListAttribute(AX_ATTR_DETAILS_IDS, two);
  AXTree tree(initial_state);

  FakeAXTreeDelegate fake_delegate;
  tree.SetDelegate(&fake_delegate);

  // Change existing attributes.
  AXTreeUpdate update0;
  update0.root_id = 1;
  update0.nodes.resize(1);
  update0.nodes[0].id = 1;
  update0.nodes[0].AddIntListAttribute(AX_ATTR_CONTROLS_IDS, two);
  update0.nodes[0].AddIntListAttribute(AX_ATTR_DETAILS_IDS, three);
  EXPECT_TRUE(tree.Unserialize(update0));

  const std::vector<std::string>& change_log =
      fake_delegate.attribute_change_log();
  ASSERT_EQ(2U, change_log.size());
  EXPECT_EQ("controlsIds changed from 1 to 2,2", change_log[0]);
  EXPECT_EQ("detailsIds changed from 2,2 to 3", change_log[1]);

  FakeAXTreeDelegate fake_delegate2;
  tree.SetDelegate(&fake_delegate2);

  // Add and remove attributes.
  AXTreeUpdate update1;
  update1.root_id = 1;
  update1.nodes.resize(1);
  update1.nodes[0].id = 1;
  update1.nodes[0].AddIntListAttribute(AX_ATTR_DETAILS_IDS, two);
  update1.nodes[0].AddIntListAttribute(AX_ATTR_FLOWTO_IDS, three);
  EXPECT_TRUE(tree.Unserialize(update1));

  const std::vector<std::string>& change_log2 =
      fake_delegate2.attribute_change_log();
  ASSERT_EQ(3U, change_log2.size());
  EXPECT_EQ("controlsIds changed from 2,2 to ", change_log2[0]);
  EXPECT_EQ("detailsIds changed from 3 to 2,2", change_log2[1]);
  EXPECT_EQ("flowtoIds changed from  to 3", change_log2[2]);

  tree.SetDelegate(NULL);
}

}  // namespace ui
