/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "host/libs/vm_manager/crosvm_manager.h"

#include <string>
#include <vector>

#include <glog/logging.h>

#include "common/libs/utils/subprocess.h"
#include "host/libs/config/cuttlefish_config.h"
#include "host/libs/vm_manager/qemu_manager.h"

namespace vm_manager {

namespace {

std::string GetControlSocketPath(const vsoc::CuttlefishConfig* config) {
  return config->PerInstancePath("crosvm_control.sock");
}

cvd::SharedFD ConnectToLogMonitor(const std::string& log_monitor_name) {
  return cvd::SharedFD::SocketLocalClient(log_monitor_name.c_str(), false,
                                          SOCK_STREAM);
}

}  // namespace

const std::string CrosvmManager::name() { return "crosvm"; }

CrosvmManager::CrosvmManager(const vsoc::CuttlefishConfig* config)
    : VmManager(config) {}

cvd::Command CrosvmManager::StartCommand() {
  if (!config_->ramdisk_image_path().empty()) {
    // TODO re-enable ramdisk when crosvm supports it
    LOG(FATAL) << "initramfs not supported";
    return cvd::Command("/bin/false");
  }

  // TODO Add aarch64 support
  // TODO Add the tap interfaces (--tap-fd)
  // TODO Redirect logcat output

  // Run crosvm directly instead of through a cf_crosvm.sh script. The kernel
  // logs are on crosvm's standard output, so we need to redirect it to the log
  // monitor socket, a helper script will print more than just the logs to
  // standard output.
  cvd::Command command(config_->crosvm_binary());
  command.AddParameter("run");

  command.AddParameter("--mem=", config_->memory_mb());
  command.AddParameter("--cpus=", config_->cpus());
  command.AddParameter("--params=", config_->kernel_cmdline_as_string());
  command.AddParameter("--disk=", config_->system_image_path());
  command.AddParameter("--rwdisk=", config_->data_image_path());
  command.AddParameter("--rwdisk=", config_->cache_image_path());
  command.AddParameter("--disk=", config_->vendor_image_path());
  command.AddParameter("--socket=", GetControlSocketPath(config_));
  command.AddParameter("--android-fstab=", config_->gsi_fstab_path());

  // TODO remove this (use crosvm's seccomp files)
  command.AddParameter("--disable-sandbox");

  if (config_->vsock_guest_cid() >= 2) {
    command.AddParameter("--cid=", config_->vsock_guest_cid());
  }

  auto kernel_log_connection =
      ConnectToLogMonitor(config_->kernel_log_socket_name());
  if (!kernel_log_connection->IsOpen()) {
    LOG(WARNING) << "Unable to connect to log monitor: "
                 << kernel_log_connection->StrError();
  } else {
    command.RedirectStdIO(cvd::Subprocess::StdIOChannel::kStdOut,
                          kernel_log_connection);
  }

  // This needs to be the last parameter
  command.AddParameter(config_->kernel_image_path());

  return command;
}

bool CrosvmManager::Stop() {
  cvd::Command command(config_->crosvm_binary());
  command.AddParameter("stop");
  command.AddParameter(GetControlSocketPath(config_));

  auto process = command.Start();

  return process.Wait() == 0;
}

}  // namespace vm_manager
