#include <stackchan/avatar/avatar/avatar.h>

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

using stackchan::avatar::Avatar;
using stackchan::avatar::Decorator;
using stackchan::avatar::Feature;
using stackchan::avatar::SpeechBubble;

struct Counts {
    int avatars = 0;
    int destroyed_avatars = 0;
    int resources = 0;
    int destroyed_resources = 0;
    int features = 0;
    int destroyed_features = 0;
    int speech_bubbles = 0;
    int destroyed_speech_bubbles = 0;
    int decorators = 0;
    int destroyed_decorators = 0;
    int feature_updates = 0;
    int speech_updates = 0;
    int decorator_updates = 0;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Resource {
    explicit Resource(Counts& counts) : counts(counts) { ++counts.resources; }
    ~Resource()
    {
        --counts.resources;
        ++counts.destroyed_resources;
    }
    Counts& counts;
};

class ProbeFeature final : public Feature {
public:
    explicit ProbeFeature(Counts& counts) : counts_(counts) { ++counts_.features; }
    ~ProbeFeature() override
    {
        --counts_.features;
        ++counts_.destroyed_features;
    }
    void _update() override { ++counts_.feature_updates; }

private:
    Counts& counts_;
};

class ProbeSpeechBubble final : public SpeechBubble {
public:
    explicit ProbeSpeechBubble(Counts& counts) : counts_(counts) { ++counts_.speech_bubbles; }
    ~ProbeSpeechBubble() override
    {
        --counts_.speech_bubbles;
        ++counts_.destroyed_speech_bubbles;
    }
    void _update() override { ++counts_.speech_updates; }

private:
    Counts& counts_;
};

class ProbeDecorator final : public Decorator {
public:
    explicit ProbeDecorator(Counts& counts) : counts_(counts) { ++counts_.decorators; }
    ~ProbeDecorator() override
    {
        --counts_.decorators;
        ++counts_.destroyed_decorators;
    }
    void _update() override { ++counts_.decorator_updates; }

private:
    Counts& counts_;
};

class ProbeAvatar final : public Avatar {
public:
    explicit ProbeAvatar(Counts& counts)
        : counts_(counts), panel_(counts), owned_resource_(std::make_unique<Resource>(counts))
    {
        ++counts_.avatars;
        _key_elements.leftEye = std::make_unique<ProbeFeature>(counts);
        _key_elements.rightEye = std::make_unique<ProbeFeature>(counts);
        _key_elements.mouth = std::make_unique<ProbeFeature>(counts);
        _key_elements.speechBubble = std::make_unique<ProbeSpeechBubble>(counts);
        addDecorator(std::make_unique<ProbeDecorator>(counts));
        addDecorator(std::make_unique<ProbeDecorator>(counts));
    }

    // Omit override so the nonvirtual-destructor regression also compiles and
    // is caught by runtime resource counts, including Release/NDEBUG builds.
    ~ProbeAvatar()
    {
        --counts_.avatars;
        ++counts_.destroyed_avatars;
    }

private:
    Counts& counts_;
    Resource panel_;
    std::unique_ptr<Resource> owned_resource_;
};

void check_one_live_avatar(const Counts& counts)
{
    check(counts.avatars == 1 && counts.resources == 2,
          "one live avatar must own its derived RAII resources");
    check(counts.features == 3 && counts.speech_bubbles == 1 && counts.decorators == 2,
          "one live avatar must own its features, speech bubble and decorators");
}

void check_all_released(const Counts& counts, int expected_avatars)
{
    check(counts.avatars == 0 && counts.destroyed_avatars == expected_avatars,
          "deleting through Avatar must run the derived destructor exactly once");
    check(counts.resources == 0 && counts.destroyed_resources == expected_avatars * 2,
          "derived avatar resources leaked or were destroyed more than once");
    check(counts.features == 0 && counts.destroyed_features == expected_avatars * 3,
          "avatar feature resources leaked or were destroyed more than once");
    check(counts.speech_bubbles == 0 && counts.destroyed_speech_bubbles == expected_avatars,
          "avatar speech bubbles leaked or were destroyed more than once");
    check(counts.decorators == 0 && counts.destroyed_decorators == expected_avatars * 2,
          "avatar decorators leaked or were destroyed more than once");
}

void repeated_base_pointer_resets_release_the_entire_avatar()
{
    Counts counts;
    std::unique_ptr<Avatar> avatar;
    for (int cycle = 1; cycle <= 100; ++cycle) {
        avatar = std::make_unique<ProbeAvatar>(counts);
        check_one_live_avatar(counts);
        avatar->update();
        check(counts.feature_updates == cycle * 3 && counts.speech_updates == cycle &&
              counts.decorator_updates == cycle * 2,
              "actual Avatar update must reach its owned elements and decorators");
        avatar.reset();
        check_all_released(counts, cycle);
        avatar.reset();
        check_all_released(counts, cycle);
    }
}

void replacing_an_attached_avatar_releases_the_previous_skin()
{
    Counts counts;
    std::unique_ptr<Avatar> avatar = std::make_unique<ProbeAvatar>(counts);
    for (int cycle = 1; cycle <= 100; ++cycle) {
        avatar = std::make_unique<ProbeAvatar>(counts);
        check_one_live_avatar(counts);
        check(counts.destroyed_avatars == cycle && counts.destroyed_resources == cycle * 2 &&
              counts.destroyed_features == cycle * 3 && counts.destroyed_speech_bubbles == cycle &&
              counts.destroyed_decorators == cycle * 2,
              "replacing a base-owned avatar must fully release the previous skin");
    }
    avatar.reset();
    check_all_released(counts, 101);
}

} // namespace

int main()
{
    const struct Test {
        const char* name;
        void (*run)();
    } tests[] = {
        {"repeated_base_pointer_resets_release_the_entire_avatar", repeated_base_pointer_resets_release_the_entire_avatar},
        {"replacing_an_attached_avatar_releases_the_previous_skin", replacing_an_attached_avatar_releases_the_previous_skin},
    };
    unsigned passed = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    const auto total = sizeof(tests) / sizeof(tests[0]);
    std::cout << passed << '/' << total << " avatar lifetime tests passed\n";
    return passed == total ? 0 : 1;
}
