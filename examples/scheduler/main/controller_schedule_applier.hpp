#pragma once

#include "ecy_stat_controller.hpp"
#include "schedule.hpp"

namespace frost {

// A schedule succeeds only after application-level verification, including
// the read-before-apply path. An ATT acknowledgement is insufficient.
class ControllerScheduleApplier final : public ScheduleApplier {
 public:
  explicit ControllerScheduleApplier(EcyStatController& controller)
      : controller_(controller) {}

  bool ensure_applied(const DesiredState& state) override {
    last_result_ = controller_.ensure_applied(state);
    return last_result_.status == ApplyStatus::Verified;
  }

  const ApplyResult& last_result() const { return last_result_; }

 private:
  ApplyResult last_result_{ApplyStatus::Failed, {}, {}};
  EcyStatController& controller_;
};

}  // namespace frost
