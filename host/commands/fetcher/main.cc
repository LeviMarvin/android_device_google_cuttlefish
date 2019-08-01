//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>

#include <sys/stat.h>
#include <unistd.h>

#include "gflags/gflags.h"
#include <glog/logging.h>

#include "common/libs/utils/subprocess.h"

#include "build_api.h"
#include "credential_source.h"

// TODO(schuffelen): Mixed builds.
DEFINE_string(build_id, "latest", "Build ID for all artifacts");
DEFINE_string(branch, "aosp-master", "Branch when build_id=\"latest\"");
DEFINE_string(target, "aosp_cf_x86_phone-userdebug", "Build target");
DEFINE_string(credential_source, "", "Build API credential source");
DEFINE_string(system_image_build_target, "", "Alternate target for the system image");
DEFINE_string(system_image_build_id, "", "Alternate build for the system image");

namespace {

const std::string& HOST_TOOLS = "cvd-host_package.tar.gz";

std::string target_image_zip(std::string target, const std::string& build_id) {
  if (target.find("-userdebug") != std::string::npos) {
    target.replace(target.find("-userdebug"), sizeof("-userdebug"), "");
  }
  if (target.find("-eng") != std::string::npos) {
    target.replace(target.find("-eng"), sizeof("-eng"), "");
  }
  return target + "-img-" + build_id + ".zip";
}

bool download_images(BuildApi* build_api, const std::string& target,
                     const std::string& build_id,
                     const std::vector<std::string>& images) {
  std::string img_zip_name = target_image_zip(target, build_id);
  auto artifacts = build_api->Artifacts(build_id, target, "latest");
  bool has_image_zip = false;
  for (const auto& artifact : artifacts) {
    has_image_zip |= artifact.Name() == img_zip_name;
  }
  if (!has_image_zip) {
    LOG(ERROR) << "Target " << target << " at id " << build_id
        << " did not have " << img_zip_name;
    return false;
  }
  build_api->ArtifactToFile(build_id, target, "latest",
                            img_zip_name, img_zip_name);
  // -o for "overwrite"
  std::vector<std::string> command = {"/usr/bin/unzip", "-o", img_zip_name};
  for (const auto& image_file : images) {
    command.push_back(image_file);
  }
  int result = cvd::execute(command);
  if (result != 0) {
    LOG(ERROR) << "Could not extract " << img_zip_name << "; ran command";
    for (const auto& argument : command) {
      LOG(ERROR) << argument;
    }
    return false;
  }
  if (unlink(img_zip_name.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << img_zip_name;
  }
  return true;
}
bool download_images(BuildApi* build_api, const std::string& target,
                     const std::string& build_id) {
  return download_images(build_api, target, build_id, {});
}

bool download_host_package(BuildApi* build_api, const std::string& target,
                           const std::string& build_id) {
  auto artifacts = build_api->Artifacts(build_id, target, "latest");
  bool has_host_package = false;
  for (const auto& artifact : artifacts) {
    has_host_package |= artifact.Name() == HOST_TOOLS;
  }
  if (!has_host_package) {
    LOG(ERROR) << "Target " << target << " at id " << build_id
        << " did not have " << HOST_TOOLS;
    return false;
  }
  build_api->ArtifactToFile(build_id, target, "latest",
                            HOST_TOOLS, HOST_TOOLS);
  if (cvd::execute({"/bin/tar", "xvf", HOST_TOOLS}) != 0) {
    LOG(FATAL) << "Could not extract " << HOST_TOOLS;
    return false;
  }
  if (unlink(HOST_TOOLS.c_str()) != 0) {
    LOG(ERROR) << "Could not delete " << HOST_TOOLS;
  }
  return true;
}

} // namespace

int main(int argc, char** argv) {
  ::android::base::InitLogging(argv, android::base::StderrLogger);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  curl_global_init(CURL_GLOBAL_DEFAULT);
  {
    std::unique_ptr<CredentialSource> credential_source;
    if (FLAGS_credential_source == "gce") {
      credential_source = GceMetadataCredentialSource::make();
    }
    BuildApi build_api(std::move(credential_source));
    std::string build_id = FLAGS_build_id;
    if (build_id == "latest") {
      build_id = build_api.LatestBuildId(FLAGS_branch, FLAGS_target);
    }

    if (!download_host_package(&build_api, FLAGS_target, build_id)) {
      LOG(FATAL) << "Could not download host package with target "
          << FLAGS_target << " and build id " << build_id;
    }
    if (!download_images(&build_api, FLAGS_target, build_id)) {
      LOG(FATAL) << "Could not download images with target "
          << FLAGS_target << " and build id " << build_id;
    }
    if (FLAGS_system_image_build_id != "") {
      std::string system_target = FLAGS_system_image_build_target == ""
          ? FLAGS_target
          : FLAGS_system_image_build_target;
      if (!download_images(&build_api, system_target, FLAGS_system_image_build_id, {"system.img"})) {
        LOG(FATAL) << "Could not download system image at target "
            << FLAGS_target << " and build id " << FLAGS_system_image_build_id;
      }
    }
  }
  curl_global_cleanup();
}
