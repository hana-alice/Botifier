// CMake copies the unmodified production Modifiable/HeadPet/IMU headers beside
// isolated platform fakes, so their relative includes need no production hooks.
#include <stackchan/modifiers/head_pet.h>
#include <stackchan/modifiers/imu.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Robot : stackchan::Modifiable {
    stackchan::motion::Motion head;
    stackchan::avatar::Avatar face;
    stackchan::avatar::Avatar* current = &face;
    stackchan::addon::NeonLight light;
    int missing_avatar_reads = 0;
    stackchan::motion::Motion& motion() override { return head; }
    stackchan::avatar::Avatar& avatar() override {
        if (!current) { ++missing_avatar_reads; throw std::runtime_error("missing avatar accessed"); }
        return *current;
    }
    bool hasAvatar() override { return current != nullptr; }
    stackchan::addon::NeonLight& leftNeonLight() override { return light; }
    stackchan::addon::NeonLight& rightNeonLight() override { return light; }
};
void swipe() { GetHAL().onHeadPetGesture.emit(HeadPetGesture::SwipeForward); }
void release() { GetHAL().onHeadPetGesture.emit(HeadPetGesture::Release); }
void shake() { GetHAL().onImuMotionEvent.emit(ImuMotionEvent::Shake); }
void reset_time() { GetHAL().now = 0; }

void scope_defaults_and_restores() {
    Robot robot;
    require(!robot.interactionsSuspended(), "normal apps start unsuspended");
    robot.setInteractionsSuspended(true);
    require(robot.interactionsSuspended(), "app scope is visible through Modifiable");
    robot.setInteractionsSuspended(false);
    require(!robot.interactionsSuspended(), "app can restore prior false scope");
}
void normal_head_pet_keeps_its_restore() {
    reset_time(); Robot robot; stackchan::HeadPetModifier modifier;
    swipe(); modifier._update(robot);
    require(robot.head.moves == 1 && robot.face.emotion == stackchan::avatar::Emotion::Happy,
            "normal swipe still animates head and face");
    release(); modifier._update(robot);
    GetHAL().now = 2999; modifier._update(robot);
    require(robot.head.moves == 1, "normal restore waits full delay");
    GetHAL().now = 3000; modifier._update(robot);
    require(robot.head.moves == 2 && robot.face.emotion == stackchan::avatar::Emotion::Neutral,
            "normal AI head pet still restores");
}
void head_pet_suspension_cancels_delayed_restore() {
    reset_time(); Robot robot; stackchan::HeadPetModifier modifier;
    swipe(); modifier._update(robot); release(); modifier._update(robot);
    robot.setInteractionsSuspended(true); modifier._update(robot);
    require(robot.face.decorations.empty(), "old head-pet decorations retire before avatar replacement");
    robot.head.locked = true;
    const auto face_writes = robot.face.emotion_writes;
    const auto lock_writes = robot.head.lock_writes;
    GetHAL().now = 5000; swipe(); release(); modifier._update(robot);
    require(robot.head.moves == 1 && robot.head.locked && robot.head.lock_writes == lock_writes,
            "suspended head pet never moves or unlocks new owner");
    require(robot.face.emotion_writes == face_writes, "suspension never restores saved emotion");
    robot.setInteractionsSuspended(false); modifier._update(robot);
    GetHAL().now = 10000; modifier._update(robot);
    require(robot.head.moves == 1, "cancelled restore and blocked events never replay");
    robot.head.locked = false; robot.head.moving = false;
    swipe(); modifier._update(robot);
    require(robot.head.moves == 2, "fresh gesture works after resuming");
}
void head_pet_drops_queued_events_and_handles_no_avatar() {
    reset_time(); Robot robot; stackchan::HeadPetModifier modifier;
    swipe(); robot.setInteractionsSuspended(true); modifier._update(robot);
    require(robot.head.moves == 0 && robot.face.emotion_writes == 0, "queued swipe is discarded at scope entry");
    robot.setInteractionsSuspended(false); robot.current = nullptr;
    release(); swipe(); modifier._update(robot);
    require(robot.missing_avatar_reads == 0, "head pet never dereferences absent avatar");
    robot.current = &robot.face; modifier._update(robot);
    require(robot.head.moves == 0, "events observed without avatar never replay");
}
void head_pet_never_restores_another_avatar() {
    reset_time(); Robot robot; stackchan::HeadPetModifier modifier;
    swipe(); modifier._update(robot); release(); modifier._update(robot);
    stackchan::avatar::Avatar next;
    robot.current = &next; GetHAL().now = 5000; modifier._update(robot);
    require(robot.head.moves == 1 && next.emotion_writes == 0 && next.removed == 0,
            "avatar replacement discards old session without touching new avatar");
}
void normal_imu_keeps_its_reaction() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    shake(); modifier._update(robot);
    require(robot.head.homes == 1 && robot.head.locked && robot.face.locked && !robot.face.left.visible,
            "normal IMU owns free locks and shows reaction");
    GetHAL().now = 4000; modifier._update(robot);
    require(!robot.head.locked && !robot.face.locked && robot.face.left.visible && robot.face.decorations.empty(),
            "normal IMU restores only its own state");
}
void imu_does_not_unlock_a_foreign_motion_owner() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    robot.head.locked = true;
    shake(); modifier._update(robot);
    require(robot.head.homes == 0, "IMU does not write motion through occupied lock");
    GetHAL().now = 4000; modifier._update(robot);
    require(robot.head.locked && robot.head.lock_writes == 0, "IMU restore never clears a lock it did not acquire");
}
void imu_does_not_take_a_foreign_face() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    robot.face.locked = true;
    shake(); modifier._update(robot); GetHAL().now = 5000; modifier._update(robot);
    require(robot.face.locked && robot.face.lock_writes == 0 && robot.face.left.writes == 0 && robot.head.moves == 0,
            "already-owned face rejects legacy reaction without partial motion");
}
void imu_suspension_retires_own_state_once() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    shake(); modifier._update(robot);
    robot.setInteractionsSuspended(true); modifier._update(robot);
    require(!robot.head.locked && !robot.face.locked && robot.face.left.visible && robot.face.decorations.empty(),
            "scope entry retires old IMU locks and decorations");
    robot.head.locked = robot.face.locked = true;
    const auto motion_lock_writes = robot.head.lock_writes;
    const auto face_lock_writes = robot.face.lock_writes;
    const auto feature_writes = robot.face.left.writes;
    shake(); GetHAL().now = 5000; modifier._update(robot); modifier._update(robot);
    require(robot.head.locked && robot.face.locked && robot.head.lock_writes == motion_lock_writes &&
            robot.face.lock_writes == face_lock_writes && robot.face.left.writes == feature_writes && robot.head.moves == 1,
            "later suspended frames do not clear or write new owner's state");
    robot.setInteractionsSuspended(false); modifier._update(robot);
    require(robot.head.moves == 1 && robot.face.lock_writes == face_lock_writes, "blocked shake never replays");
}
void imu_suspension_preserves_foreign_motion_lock() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    robot.head.locked = true; shake(); modifier._update(robot);
    robot.setInteractionsSuspended(true); modifier._update(robot);
    require(robot.head.locked && robot.head.lock_writes == 0 && !robot.face.locked,
            "scope cleanup preserves foreign motion owner while clearing own face");
}
void imu_missing_avatar_cancels_safely() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    shake(); modifier._update(robot); robot.current = nullptr;
    modifier._update(robot); shake(); modifier._update(robot);
    require(robot.missing_avatar_reads == 0 && !robot.head.locked, "missing avatar drops events and releases only own motion");
    robot.current = &robot.face; modifier._update(robot);
    require(robot.head.moves == 1, "absent-avatar shake does not replay");
}
void imu_lost_lock_is_not_reacquired_or_cleared() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    shake(); modifier._update(robot);
    robot.head.locked = false; GetHAL().now = 1; modifier._update(robot);
    require(!robot.head.locked && robot.head.homes == 1, "lost cooperative ownership is not reacquired");
    robot.head.locked = true; GetHAL().now = 4000; modifier._update(robot);
    require(robot.head.locked, "later foreign true lock is not cleared after observed ownership loss");
}
void imu_never_cleans_a_replacement_avatar() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    shake(); modifier._update(robot);
    stackchan::avatar::Avatar next; next.locked = true;
    next.addDecorator(std::make_unique<stackchan::avatar::ShyDecorator>(nullptr, 0));
    robot.current = &next; modifier._update(robot);
    require(next.locked && next.lock_writes == 0 && next.left.writes == 0 && next.decorations.size() == 1,
            "replacement avatar keeps its lock, geometry and decorators");
}
void resumed_imu_accepts_only_fresh_shakes() {
    reset_time(); Robot robot; stackchan::ImuEventModifier modifier;
    robot.setInteractionsSuspended(true); shake(); modifier._update(robot); shake();
    robot.setInteractionsSuspended(false); modifier._update(robot);
    require(robot.head.moves == 0, "events queued at and during suspension are discarded");
    shake(); modifier._update(robot);
    require(robot.head.homes == 1, "fresh shake remains functional after resuming");
}
void destruction_disconnects_callbacks() {
    reset_time(); Robot robot;
    { stackchan::HeadPetModifier head; stackchan::ImuEventModifier imu; }
    swipe(); release(); shake();
    require(robot.head.moves == 0, "destroyed modifiers leave no live callback target");
}
}

int main() {
    try {
        scope_defaults_and_restores();
        normal_head_pet_keeps_its_restore();
        head_pet_suspension_cancels_delayed_restore();
        head_pet_drops_queued_events_and_handles_no_avatar();
        head_pet_never_restores_another_avatar();
        normal_imu_keeps_its_reaction();
        imu_does_not_unlock_a_foreign_motion_owner();
        imu_does_not_take_a_foreign_face();
        imu_suspension_retires_own_state_once();
        imu_suspension_preserves_foreign_motion_lock();
        imu_missing_avatar_cancels_safely();
        imu_lost_lock_is_not_reacquired_or_cleared();
        imu_never_cleans_a_replacement_avatar();
        resumed_imu_accepts_only_fresh_shakes();
        destruction_disconnects_callbacks();
        std::cout << "legacy_interaction_test: 15 groups passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "legacy_interaction_test: " << error.what() << '\n';
        return 1;
    }
}
