#pragma once

#include <tools/event/signal.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace smooth_ui_toolkit {
template <typename T> T clamp(T value, T minimum, T maximum) { return std::clamp(value, minimum, maximum); }
struct Vector2i { int x = 0; int y = 0; };
}
namespace uitk = smooth_ui_toolkit;

inline void* lv_screen_active() { return nullptr; }
enum class HeadPetGesture { None, Press, Release, SwipeForward, SwipeBackward };
enum class ImuMotionEvent { None, Shake };

struct LegacyTestHal {
    std::uint32_t now = 0;
    std::uint32_t millis() const { return now; }
    uitk::Signal<HeadPetGesture> onHeadPetGesture;
    uitk::Signal<ImuMotionEvent> onImuMotionEvent;
};
inline LegacyTestHal& GetHAL() { static LegacyTestHal hal; return hal; }

namespace stackchan {
class Poolable { public: virtual ~Poolable() = default; };
class Random {
public:
    static Random& getInstance() { static Random random; return random; }
    int getInt(int minimum, int) { return minimum; }
};
namespace addon { class NeonLight {}; }
namespace motion {
class Motion {
public:
    bool locked = false, moving = false;
    int moves = 0, homes = 0, lock_writes = 0;
    uitk::Vector2i angles{20, 50};
    bool isModifyLocked() const { return locked; }
    void setModifyLock(bool value) { locked = value; ++lock_writes; }
    bool isMoving() const { return moving; }
    uitk::Vector2i getCurrentAngles() const { return angles; }
    void moveWithSpeed(int yaw, int pitch, int) { angles = {yaw, pitch}; moving = true; ++moves; }
    void goHome(int) { angles = {0, 0}; moving = true; ++moves; ++homes; }
};
}
namespace avatar {
enum class Emotion { Neutral, Happy };
class Decorator { public: virtual ~Decorator() = default; };
class HeartDecorator : public Decorator { public: HeartDecorator(void*, int, int) {} };
class ShyDecorator : public Decorator { public: ShyDecorator(void*, int) {} };
class DizzyDecorator : public Decorator { public: DizzyDecorator(void*, int, int) {} };
class Feature {
public:
    bool visible = true;
    int weight = 0, rotation = 0, writes = 0;
    void setVisible(bool value) { visible = value; ++writes; }
    void setWeight(int value) { weight = value; ++writes; }
    void setRotation(int value) { rotation = value; ++writes; }
};
class Avatar {
public:
    bool locked = false;
    Emotion emotion = Emotion::Neutral;
    int emotion_writes = 0, lock_writes = 0, removed = 0;
    Feature left, right, mouth_feature;
    std::unordered_map<int, std::unique_ptr<Decorator>> decorations;
    bool isModifyLocked() const { return locked; }
    void setModifyLock(bool value) { locked = value; ++lock_writes; }
    Emotion getEmotion() const { return emotion; }
    void setEmotion(Emotion value) { emotion = value; ++emotion_writes; }
    Feature& leftEye() { return left; }
    Feature& rightEye() { return right; }
    Feature& mouth() { return mouth_feature; }
    int addDecorator(std::unique_ptr<Decorator> decorator) {
        const int id = next_id++;
        decorations.emplace(id, std::move(decorator));
        return id;
    }
    void removeDecorator(int id) { removed += static_cast<int>(decorations.erase(id)); }
private:
    int next_id = 0;
};
}
}
