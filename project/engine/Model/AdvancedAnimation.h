#pragma once

#include "MathUtil.h"
#include "DataTypes.h"
#include <string>
#include <vector>
#include <map>

namespace AdvAnim {

    struct Node {
        std::string name;
        std::vector<Node> children;
    };

    struct AnimatedModel {
        ModelData modelData;
        Node rootNode;
    };

    template <typename tValue>
    struct Keyframe {
        float time;
        tValue value;
    };
    using KeyframeVector3 = Keyframe<Vector3>;
    using KeyframeQuaternion = Keyframe<Quaternion>;

    template<typename tValue>
    struct AnimationCurve {
        std::vector<Keyframe<tValue>> keyframes;
    };

    struct NodeAnimation {
        AnimationCurve<Vector3> translate;
        AnimationCurve<Quaternion> rotate;
        AnimationCurve<Vector3> scale;
    };

    struct Animation {
        float duration; // アニメーション全体の尺（単位は秒）
        std::map<std::string, NodeAnimation> nodeAnimations;
    };

    AnimatedModel LoadModelFile(const std::string& directoryPath, const std::string& filename);
    Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename);

    Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);
    Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);

} // namespace AdvAnim
