// Copyright (c) 2024 Open Navigation LLC
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

#include "opennav_docking/dock_database.hpp"

namespace opennav_docking
{

DockDatabase::DockDatabase()
: dock_loader_("opennav_docking_core", "opennav_docking_core::ChargingDock")
{}

DockDatabase::~DockDatabase()
{
  dock_instances_.clear();
  dock_plugins_.clear();
}

bool DockDatabase::initialize(const rclcpp_lifecycle::LifecycleNode::WeakPtr & parent)
{
  node_ = parent;
  auto node = node_.lock();

  if (getDockPlugins(node) && getDockInstances(node)) {
    RCLCPP_INFO(
      node->get_logger(),
      "Docking Server has %u dock types and %u dock instances available.",
      this->plugin_size(), this->instance_size());
    return true;
  }

  return false;
}

void DockDatabase::activate()
{
  DockPluginMap::iterator it;
  for (it = dock_plugins_.begin(); it != dock_plugins_.end(); ++it) {
    it->second->activate();
  }
}

void DockDatabase::deactivate()
{
  DockPluginMap::iterator it;
  for (it = dock_plugins_.begin(); it != dock_plugins_.end(); ++it) {
    it->second->deactivate();
  }
}

Dock * DockDatabase::findDock(const std::string & dock_id)
{
  Dock * dock_instance = findDockInstance(dock_id);
  ChargingDock::Ptr dock_plugin{nullptr};

  if (dock_instance) {
    dock_plugin = findDockPlugin(dock_instance->type);
    if (dock_plugin) {
      // Populate the plugin shared pointer
      dock_instance->plugin = dock_plugin;
      return dock_instance;
    }
    throw DockingException("Dock requested has no valid plugin!"); // TODO specialize
  }
  throw DockingException("Dock ID requested is not in database!"); // TODO specialize
}

Dock * DockDatabase::findDockInstance(const std::string & dock_id)
{
  auto it = dock_instances_.find(dock_id);
  if (it != dock_instances_.end()) {
    return &(it->second);
  }
  return nullptr;
}

ChargingDock::Ptr DockDatabase::findDockPlugin(const std::string & type)
{
  // If only one dock plugin and type not set, use the default dock
  if (type.empty() && dock_plugins_.size() == 1) {
    return dock_plugins_.begin()->second;
  }

  auto it = dock_plugins_.find(type);
  if (it != dock_plugins_.end()) {
    return it->second;
  }
  return nullptr;
}

bool DockDatabase::getDockPlugins(const rclcpp_lifecycle::LifecycleNode::SharedPtr & node)
{
  std::vector<std::string> docks_plugins;
  if (!node->has_parameter("dock_plugins")) {
    node->declare_parameter("dock_plugins", rclcpp::ParameterType::PARAMETER_STRING_ARRAY);
  }
  if (!node->get_parameter("dock_plugins", docks_plugins)) {
    RCLCPP_ERROR(node->get_logger(), "Charging dock plugins not given!");
    return false;
  }

  if (docks_plugins.size() < 1u) {
    RCLCPP_ERROR(node->get_logger(), "Charging dock plugins empty! Must provide 1.");
    return false;
  }

  for (size_t i = 0; i != docks_plugins.size(); i++) {
    try {
      std::string plugin_type = nav2_util::get_plugin_type_param(
        node, docks_plugins[i]);
      opennav_docking_core::ChargingDock::Ptr dock =
        dock_loader_.createUniqueInstance(plugin_type);
      RCLCPP_INFO(
        node->get_logger(), "Created charging dock plugin %s of type %s",
        docks_plugins[i].c_str(), plugin_type.c_str());
      dock->configure(node, docks_plugins[i], nullptr); // TODO TF buffer
      dock_plugins_.insert({docks_plugins[i], dock});
    } catch (const std::exception & ex) {
      RCLCPP_FATAL(
        node->get_logger(), "Failed to create Charging Dock plugin. Exception: %s",
        ex.what());
      return false;
    }
  }

  return true;
}

bool DockDatabase::getDockInstances(const rclcpp_lifecycle::LifecycleNode::SharedPtr & node)
{
  using rclcpp::ParameterType::PARAMETER_STRING;
  using rclcpp::ParameterType::PARAMETER_STRING_ARRAY;

  // Attempt to obtain docks from separate file
  std::string dock_filepath;
  node->declare_parameter("dock_database", PARAMETER_STRING);
  if (node->get_parameter("dock_database", dock_filepath)) {
    RCLCPP_INFO(
      node->get_logger(), "Loading dock from database file  %s.", dock_filepath.c_str());
    try {
      return utils::parseDockFile(dock_filepath, node, dock_instances_);
    } catch (YAML::ParserException & e) {
      RCLCPP_ERROR(
        node->get_logger(),
        "Dock database (%s) is malformed: %s.", dock_filepath.c_str(), e.what());
      return false;
    }
    return true;
  }

  // Attempt to obtain docks from parameter file
  std::vector<std::string> docks_param;
  node->declare_parameter("docks", PARAMETER_STRING_ARRAY);
  if (node->get_parameter("docks", docks_param)) {
    RCLCPP_INFO(node->get_logger(), "Loading docks from parameter file.");
    return utils::parseDockParams(docks_param, node, dock_instances_);
  }

  RCLCPP_ERROR(
    node->get_logger(),
    "Dock database filepath nor dock parameters set. Unable to perform docking actions.");
  return false;
}

unsigned int DockDatabase::plugin_size() const
{
  return dock_plugins_.size();
}

unsigned int DockDatabase::instance_size() const
{
  return dock_instances_.size();
}

}  // namespace opennav_docking