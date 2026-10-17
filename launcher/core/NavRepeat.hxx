#pragma once

namespace vibe::launcher {

// turns a held nav axis into discrete menu steps: one on press, then repeats after a delay
class NavRepeat {
public:
    static constexpr double kDelay = 0.4;  // seconds held before repeating
    static constexpr double kRate = 0.12;  // seconds between repeats
    static constexpr float kThreshold = 0.5f;

    // returns -1, 0 or +1 steps for this frame
    int update(float axis, double dt) {
        const int dir = axis > kThreshold ? 1 : (axis < -kThreshold ? -1 : 0);
        if(dir == 0 || dir != _dir) {
            _dir = dir;
            _held = 0.0;
            _next = kDelay;
            return dir;
        }
        _held += dt;
        if(_held >= _next) {
            _next += kRate;
            return dir;
        }
        return 0;
    }

private:
    int _dir = 0;
    double _held = 0.0;
    double _next = kDelay;
};

} // namespace vibe::launcher
