/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/prediction/prediction.h"

#include "modules/prediction/container/container_manager.h"
#include "modules/prediction/evaluator/evaluator_manager.h"
#include "modules/prediction/predictor/predictor_manager.h"
#include "modules/prediction/common/prediction_gflags.h"
#include "modules/prediction/proto/prediction_obstacle.pb.h"
#include "modules/prediction/container/obstacles/obstacles_container.h"
#include "modules/prediction/container/pose/pose_container.h"
#include "modules/common/adapters/adapter_manager.h"
#include "modules/common/adapters/adapter_gflags.h"
#include "modules/common/util/file.h"

namespace apollo {
namespace prediction {

using ::apollo::perception::PerceptionObstacles;
using ::apollo::localization::LocalizationEstimate;
using ::apollo::common::adapter::AdapterManager;
using ::apollo::common::adapter::AdapterConfig;
using ::apollo::common::Status;
using ::apollo::common::ErrorCode;

std::string Prediction::Name() const {
  return FLAGS_prediction_module_name;
}

Status Prediction::Init() {
  // Load prediction conf
  prediction_conf_.Clear();
  if (!::apollo::common::util::GetProtoFromFile(FLAGS_prediction_conf_file,
                                                &prediction_conf_)) {
    return OnError("Unable to load prediction conf file: " +
                   FLAGS_prediction_conf_file);
  } else {
    ADEBUG << "Prediction config file is loaded into: "
           << prediction_conf_.ShortDebugString();
  }

  adapter_conf_.Clear();
  if (!::apollo::common::util::GetProtoFromFile(FLAGS_adapter_config_path,
                                                &adapter_conf_)) {
    return OnError("Unable to load adapter conf file: " +
                   FLAGS_adapter_config_path);
  } else {
    ADEBUG << "Adapter config file is loaded into: "
           << adapter_conf_.ShortDebugString();
  }

  // Initialization of all managers
  AdapterManager::instance()->Init();
  ContainerManager::instance()->Init(adapter_conf_);
  EvaluatorManager::instance()->Init(prediction_conf_);
  PredictorManager::instance()->Init(prediction_conf_);

  CHECK(AdapterManager::GetLocalization()) << "Localization is not ready.";
  CHECK(AdapterManager::GetPerceptionObstacles()) << "Perception is not ready.";

  // Set perception obstacle callback function
  AdapterManager::SetPerceptionObstaclesCallback(&Prediction::OnPerception,
                                                 this);
  return Status::OK();
}

Status Prediction::Start() {
  return Status::OK();
}

void Prediction::Stop() {}

void Prediction::OnPerception(const PerceptionObstacles &perception_obstacles) {
  auto localization_adapter = AdapterManager::GetLocalization();
  ObstaclesContainer* obstacles_container = dynamic_cast<ObstaclesContainer*>(
      ContainerManager::instance()->GetContainer(
      AdapterConfig::PERCEPTION_OBSTACLES));
  CHECK_NOTNULL(obstacles_container);
  if (localization_adapter->Empty()) {
    ADEBUG << "No localization message.";
  } else {
    const LocalizationEstimate& localization =
        localization_adapter->GetLatestObserved();
    ADEBUG << "Received localization message ["
           << localization.ShortDebugString()
           << "].";
    PoseContainer* pose_container = dynamic_cast<PoseContainer*>(
        ContainerManager::instance()->GetContainer(
        AdapterConfig::LOCALIZATION));
    pose_container->Insert(localization);
    obstacles_container->InsertPerceptionObstacle(
        *(pose_container->ToPerceptionObstacle()),
        pose_container->GetTimestamp());
  }
  if (obstacles_container == nullptr) {
    AERROR << "Null obstacles container";
    return;
  }
  obstacles_container->Insert(perception_obstacles);
  EvaluatorManager::instance()->Run(perception_obstacles);
  PredictorManager::instance()->Run(perception_obstacles);

  PredictionObstacles prediction_obstacles;
  prediction_obstacles.CopyFrom(
      PredictorManager::instance()->prediction_obstacles());
  AdapterManager::FillPredictionHeader(Name(), &prediction_obstacles);
  AdapterManager::PublishPrediction(prediction_obstacles);
  ADEBUG << "Published a prediction message ["
         << prediction_obstacles.ShortDebugString() << "].";
  // for (const auto& pob : prediction_obstacles.prediction_obstacle()) {
  //   for (const auto& traj : pob.trajectory()) {
  //     for (const auto& traj_point : traj.trajectory_point()) {
  //       AINFO << "[" << std::fixed << std::setprecision(6)
  //             << traj_point.path_point().x() << ", "
  //             << std::fixed << std::setprecision(6)
  //             << traj_point.path_point().y() << "]";
  //     }
  //   }
  // }
}

Status Prediction::OnError(const std::string& error_msg) {
  return Status(ErrorCode::PREDICTION_ERROR, error_msg);
}

}  // namespace prediction
}  // namespace apollo
