#pragma once
// Player view orientation with axis-aligned air rotations.
//
// The orientation is a "body" frame (right, up, front), changed only by
// rotations, plus a head yaw/pitch from the mouse relative to that body
// (pitch clamped, like a normal FPS camera around the body's up axis).
//
// turn(): the view is first snapped to the world axes (front -> nearest axis,
// then right -> nearest axis perpendicular to it), then rotated 90 degrees.
// That happens instantly (it is the physical orientation), while the camera
// shown on screen eases from the old view to the new one over `duration`
// seconds; no new turn can start during that time.
//
// The easing is stored as a world-space offset rotation that decays to
// identity: shown = ease(offset -> identity) * physical. Mouse look changes
// the physical view during a transition and simply adds on top.

struct V3f { float x = 0, y = 0, z = 0; };

struct Quat { float w = 1, x = 0, y = 0, z = 0; };

class PlayerView {
public:
    enum Turn { PITCH_DOWN, PITCH_UP, YAW_LEFT, YAW_RIGHT, ROLL_LEFT, ROLL_RIGHT };

    float duration = 0.25f;           // visual transition time (s)

    // Upright body, looking along +x, then yaw/pitch (radians).
    void reset(float yaw, float pitch);
    void look(float dyaw, float dpitch);           // mouse
    bool turn(Turn t);                              // false if a transition is running
    // Landed on normal ground while rotated: rotate the whole view by the
    // smallest rotation that brings the body's up to world up (the look
    // direction turns with it: fell face down -> looking ahead again). The
    // mouse yaw/pitch relative to the body is kept. Eased like a turn.
    void landUpright();
    // Rotates the whole view (body frame, and the transition start) by a
    // rotation matrix (row-major 3x3): used when going through a portal, so
    // the view stays seamless.
    void applyRotation(const float m[9]);
    void update(float dt);                          // advances the transition
    bool transitioning() const { return t < duration; }

    // Physical view (what movement and turns use).
    void physical(V3f& front, V3f& right, V3f& up) const;
    // What the camera shows (eased during a transition).
    void visual(V3f& front, V3f& up) const;

    V3f bodyUp() const { return bU; }

    // Unit horizontal (x, z) directions for walking, from the physical view.
    // Works when looking straight up/down (uses the screen-up direction) and
    // when rolled sideways or upside down.
    void walkDirections(float& fx, float& fz, float& rx, float& rz) const;

private:
    V3f bR{0, 0, 1}, bU{0, 1, 0}, bF{1, 0, 0};    // body frame (axis-aligned after a turn)
    float yaw = 0, pitch = 0;                      // head, relative to the body
    Quat off;                                      // shown = ease(off -> identity) * physical
    float t = 1e9f;                                // time since the last turn
};

Quat quatFromBasis(const V3f& front, const V3f& up);
void basisFromQuat(const Quat& q, V3f& front, V3f& up);
Quat slerp(Quat a, Quat b, float t);
