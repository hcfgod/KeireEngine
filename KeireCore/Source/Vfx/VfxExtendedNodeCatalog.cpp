#include "KeireInternal/Vfx/VfxNodeCatalogInternal.h"

#include <iterator>
#include <utility>

namespace Keire::Internal
{
    namespace
    {
        [[nodiscard]] std::vector<VfxContextType> ValueContexts()
        {
            return {VfxContextType::Spawn, VfxContextType::Initialize, VfxContextType::Update, VfxContextType::Output,
                    VfxContextType::Event};
        }

        [[nodiscard]] VfxNodePinDescriptor Input(std::string name, std::string semantic, const VfxValueType type,
                                                 VfxParameterValue defaultValue)
        {
            return {std::move(name), std::move(semantic), type, true, std::move(defaultValue), {type}};
        }

        [[nodiscard]] VfxNodePinDescriptor Output(std::string name, std::string semantic, const VfxValueType type)
        {
            return {std::move(name), std::move(semantic), type, false, std::nullopt, {type}};
        }

        [[nodiscard]] VfxNodeDescriptor Operator(std::string id, std::string label, std::string category,
                                                 std::vector<VfxNodePinDescriptor> pins, const VfxValueOpcode opcode,
                                                 const VfxNodeBackendTier backend = VfxNodeBackendTier::CpuAndGpu,
                                                 std::vector<std::string> synonyms = {})
        {
            return {{std::move(id)},
                    std::move(label),
                    std::move(category),
                    std::move(synonyms),
                    VfxNodeClass::Operator,
                    VfxNodeTypeBehavior::Fixed,
                    VfxNodeSupportTier::KeireEquivalent,
                    {},
                    ValueContexts(),
                    std::move(pins),
                    {},
                    opcode,
                    1,
                    backend};
        }

        [[nodiscard]] VfxNodeDescriptor Inline(std::string id, std::string label,
                                               std::vector<VfxNodePinDescriptor> inputs,
                                               std::vector<VfxNodePinDescriptor> outputs,
                                               const VfxNodeBackendTier backend = VfxNodeBackendTier::CpuAndGpu)
        {
            inputs.insert(inputs.end(), std::make_move_iterator(outputs.begin()),
                          std::make_move_iterator(outputs.end()));
            return Operator(std::move(id), std::move(label), "Operator/Inline", std::move(inputs),
                            VfxValueOpcode::Passthrough, backend, {"inline", "literal", "structured value"});
        }

        [[nodiscard]] VfxNodeDescriptor Resource(std::string id, std::string label,
                                                 std::vector<VfxNodePinDescriptor> pins, const VfxValueOpcode opcode)
        {
            return Operator(std::move(id), std::move(label), "Operator/Sampling", std::move(pins), opcode,
                            VfxNodeBackendTier::CpuOnly, {"resource", "sample", "cpu provider"});
        }
    } // namespace

    std::vector<VfxNodeDescriptor> BuildVfxExtendedNodeCatalog()
    {
        std::vector<VfxNodeDescriptor> result;
        result.reserve(72);

        const auto addAttribute =
            [&result](std::string id, std::string label, const VfxValueType type, const VfxValueOpcode opcode)
        {
            result.push_back(Operator(std::move(id), std::move(label), "Operator/Attribute",
                                      {Output("Value", "out", type)}, opcode));
        };
        addAttribute("keire.operator.attribute-angular-velocity", "Get Attribute: angularVelocity",
                     VfxValueType::Vector3, VfxValueOpcode::AttributeAngularVelocity);
        addAttribute("keire.operator.attribute-direction", "Get Attribute: direction", VfxValueType::Vector3,
                     VfxValueOpcode::AttributeDirection);
        addAttribute("keire.operator.attribute-mass", "Get Attribute: mass", VfxValueType::Scalar,
                     VfxValueOpcode::AttributeMass);
        addAttribute("keire.operator.attribute-pivot", "Get Attribute: pivot", VfxValueType::Vector3,
                     VfxValueOpcode::AttributePivot);
        addAttribute("keire.operator.attribute-scale", "Get Attribute: scale", VfxValueType::Vector3,
                     VfxValueOpcode::AttributeScale);
        addAttribute("keire.operator.attribute-target-position", "Get Attribute: targetPosition", VfxValueType::Vector3,
                     VfxValueOpcode::AttributeTargetPosition);
        addAttribute("keire.operator.attribute-texture-index", "Get Attribute: texIndex", VfxValueType::UnsignedInteger,
                     VfxValueOpcode::AttributeTextureIndex);
        result.push_back(Operator(
            "keire.operator.custom-attribute", "Get Custom Attribute", "Operator/Attribute",
            {Input("Value", "value", VfxValueType::Vector4, Vector4{}), Output("Value", "out", VfxValueType::Vector4)},
            VfxValueOpcode::Passthrough, VfxNodeBackendTier::CpuAndGpu, {"custom value", "user attribute"}));

        result.push_back(Operator("keire.operator.local-to-world", "Local To World", "Operator/Built-In",
                                  {Output("Matrix", "out", VfxValueType::Matrix)}, VfxValueOpcode::LocalToWorld,
                                  VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.world-to-local", "World To Local", "Operator/Built-In",
                                  {Output("Matrix", "out", VfxValueType::Matrix)}, VfxValueOpcode::WorldToLocal,
                                  VfxNodeBackendTier::CpuOnly));

        result.push_back(Inline("keire.operator.inline-box", "Box",
                                {Input("Center", "center", VfxValueType::Vector3, Vector3{}),
                                 Input("Size", "size", VfxValueType::Vector3, Vector3{1.0F, 1.0F, 1.0F}),
                                 Input("Rotation", "rotation", VfxValueType::Vector3, Vector3{})},
                                {Output("Center", "centerOut", VfxValueType::Vector3),
                                 Output("Size", "sizeOut", VfxValueType::Vector3),
                                 Output("Rotation", "rotationOut", VfxValueType::Vector3)}));
        result.push_back(Inline("keire.operator.inline-curve", "Animation Curve",
                                {Input("Curve", "curve", VfxValueType::Curve, Curve1D::Constant(0.0F))},
                                {Output("Curve", "out", VfxValueType::Curve)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline(
            "keire.operator.inline-circle", "Circle",
            {Input("Center", "center", VfxValueType::Vector3, Vector3{}),
             Input("Normal", "normal", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
             Input("Radius", "radius", VfxValueType::Scalar, 0.5F), Input("Arc", "arc", VfxValueType::Scalar, 360.0F)},
            {Output("Center", "centerOut", VfxValueType::Vector3), Output("Normal", "normalOut", VfxValueType::Vector3),
             Output("Radius", "radiusOut", VfxValueType::Scalar), Output("Arc", "arcOut", VfxValueType::Scalar)}));
        result.push_back(Inline(
            "keire.operator.inline-cone", "Cone",
            {Input("Center", "center", VfxValueType::Vector3, Vector3{}),
             Input("Direction", "direction", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
             Input("Radius", "radius", VfxValueType::Scalar, 0.5F),
             Input("Height", "height", VfxValueType::Scalar, 1.0F), Input("Arc", "arc", VfxValueType::Scalar, 360.0F)},
            {Output("Center", "centerOut", VfxValueType::Vector3),
             Output("Direction", "directionOut", VfxValueType::Vector3),
             Output("Radius", "radiusOut", VfxValueType::Scalar), Output("Height", "heightOut", VfxValueType::Scalar),
             Output("Arc", "arcOut", VfxValueType::Scalar)}));
        result.push_back(Inline(
            "keire.operator.inline-sphere", "Sphere",
            {Input("Center", "center", VfxValueType::Vector3, Vector3{}),
             Input("Radius", "radius", VfxValueType::Scalar, 0.5F), Input("Arc", "arc", VfxValueType::Scalar, 360.0F)},
            {Output("Center", "centerOut", VfxValueType::Vector3), Output("Radius", "radiusOut", VfxValueType::Scalar),
             Output("Arc", "arcOut", VfxValueType::Scalar)}));
        result.push_back(Inline("keire.operator.inline-torus", "Torus",
                                {Input("Center", "center", VfxValueType::Vector3, Vector3{}),
                                 Input("Normal", "normal", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
                                 Input("Major Radius", "majorRadius", VfxValueType::Scalar, 1.0F),
                                 Input("Minor Radius", "minorRadius", VfxValueType::Scalar, 0.25F),
                                 Input("Arc", "arc", VfxValueType::Scalar, 360.0F)},
                                {Output("Center", "centerOut", VfxValueType::Vector3),
                                 Output("Normal", "normalOut", VfxValueType::Vector3),
                                 Output("Major Radius", "majorRadiusOut", VfxValueType::Scalar),
                                 Output("Minor Radius", "minorRadiusOut", VfxValueType::Scalar),
                                 Output("Arc", "arcOut", VfxValueType::Scalar)}));
        result.push_back(Inline("keire.operator.inline-cylinder", "Cylinder",
                                {Input("Center", "center", VfxValueType::Vector3, Vector3{}),
                                 Input("Direction", "direction", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
                                 Input("Radius", "radius", VfxValueType::Scalar, 0.5F),
                                 Input("Height", "height", VfxValueType::Scalar, 1.0F)},
                                {Output("Center", "centerOut", VfxValueType::Vector3),
                                 Output("Direction", "directionOut", VfxValueType::Vector3),
                                 Output("Radius", "radiusOut", VfxValueType::Scalar),
                                 Output("Height", "heightOut", VfxValueType::Scalar)}));
        result.push_back(Inline("keire.operator.inline-flipbook", "FlipBook",
                                {Input("Layout", "layout", VfxValueType::Vector4, Vector4{1.0F, 1.0F, 0.0F, 1.0F})},
                                {Output("Layout", "out", VfxValueType::Vector4)}));
        result.push_back(
            Inline("keire.operator.inline-gradient", "Gradient",
                   {Input("Gradient", "gradient", VfxValueType::Gradient, ColorGradient::Constant(Color{}))},
                   {Output("Gradient", "out", VfxValueType::Gradient)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline(
            "keire.operator.inline-line", "Line",
            {Input("Start", "start", VfxValueType::Vector3, Vector3{}),
             Input("End", "end", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F})},
            {Output("Start", "startOut", VfxValueType::Vector3), Output("End", "endOut", VfxValueType::Vector3)}));
        result.push_back(Inline("keire.operator.inline-matrix", "Matrix4x4",
                                {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{})},
                                {Output("Matrix", "out", VfxValueType::Matrix)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-mesh", "Mesh",
                                {Input("Mesh", "mesh", VfxValueType::Mesh, AssetId{})},
                                {Output("Mesh", "out", VfxValueType::Mesh)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-point-cache", "Point Cache",
                                {Input("Point Cache", "pointCache", VfxValueType::PointCache, AssetId{})},
                                {Output("Point Cache", "out", VfxValueType::PointCache)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-plane", "Plane",
                                {Input("Point", "point", VfxValueType::Vector3, Vector3{}),
                                 Input("Normal", "normal", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F})},
                                {Output("Point", "pointOut", VfxValueType::Vector3),
                                 Output("Normal", "normalOut", VfxValueType::Vector3)}));
        result.push_back(Inline("keire.operator.inline-texture-cube", "Cubemap",
                                {Input("Texture", "texture", VfxValueType::TextureCube, AssetId{})},
                                {Output("Texture", "out", VfxValueType::TextureCube)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-texture-cube-array", "Cubemap Array",
                                {Input("Texture", "texture", VfxValueType::Asset, AssetId{})},
                                {Output("Texture", "out", VfxValueType::Asset)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-texture2d", "Texture2D",
                                {Input("Texture", "texture", VfxValueType::Texture, AssetId{})},
                                {Output("Texture", "out", VfxValueType::Texture)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-texture2d-array", "Texture2D Array",
                                {Input("Texture", "texture", VfxValueType::Texture2DArray, AssetId{})},
                                {Output("Texture", "out", VfxValueType::Texture2DArray)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-texture3d", "Texture3D",
                                {Input("Texture", "texture", VfxValueType::Texture3D, AssetId{})},
                                {Output("Texture", "out", VfxValueType::Texture3D)}, VfxNodeBackendTier::CpuOnly));
        result.push_back(Inline("keire.operator.inline-transform", "Transform",
                                {Input("Position", "position", VfxValueType::Vector3, Vector3{}),
                                 Input("Rotation", "rotation", VfxValueType::Quaternion, Quaternion{}),
                                 Input("Scale", "scale", VfxValueType::Vector3, Vector3{1.0F, 1.0F, 1.0F})},
                                {Output("Position", "positionOut", VfxValueType::Vector3),
                                 Output("Rotation", "rotationOut", VfxValueType::Quaternion),
                                 Output("Scale", "scaleOut", VfxValueType::Vector3)},
                                VfxNodeBackendTier::CpuOnly));

        result.push_back(Operator("keire.operator.weighted-select", "Random Selector Weighted", "Operator/Logic",
                                  {Input("A", "a", VfxValueType::Vector3, Vector3{}),
                                   Input("B", "b", VfxValueType::Vector3, Vector3{1.0F, 1.0F, 1.0F}),
                                   Input("Weight A", "weightA", VfxValueType::Scalar, 1.0F),
                                   Input("Weight B", "weightB", VfxValueType::Scalar, 1.0F),
                                   Input("Selector", "selector", VfxValueType::Scalar, 0.5F),
                                   Output("Out", "out", VfxValueType::Vector3)},
                                  VfxValueOpcode::WeightedSelect));

        result.push_back(Operator("keire.operator.construct-matrix", "Construct Matrix", "Operator/Math/Geometry",
                                  {Input("Row 0", "row0", VfxValueType::Vector4, Vector4{1.0F, 0.0F, 0.0F, 0.0F}),
                                   Input("Row 1", "row1", VfxValueType::Vector4, Vector4{0.0F, 1.0F, 0.0F, 0.0F}),
                                   Input("Row 2", "row2", VfxValueType::Vector4, Vector4{0.0F, 0.0F, 1.0F, 0.0F}),
                                   Input("Row 3", "row3", VfxValueType::Vector4, Vector4{0.0F, 0.0F, 0.0F, 1.0F}),
                                   Output("Matrix", "out", VfxValueType::Matrix)},
                                  VfxValueOpcode::ConstructMatrix, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.look-at", "Look At", "Operator/Math/Geometry",
                                  {Input("Eye", "eye", VfxValueType::Vector3, Vector3{}),
                                   Input("Target", "target", VfxValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F}),
                                   Input("Up", "up", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
                                   Output("Matrix", "out", VfxValueType::Matrix)},
                                  VfxValueOpcode::LookAt, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.look-at-direction", "Look At Direction", "Operator/Math/Geometry",
                                  {Input("Origin", "origin", VfxValueType::Vector3, Vector3{}),
                                   Input("Target", "target", VfxValueType::Vector3, Vector3{0.0F, 0.0F, 1.0F}),
                                   Output("Direction", "out", VfxValueType::Vector3)},
                                  VfxValueOpcode::LookAtDirection));
        result.push_back(Operator(
            "keire.operator.sample-bezier", "Sample Bezier", "Operator/Math/Geometry",
            {Input("P0", "p0", VfxValueType::Vector3, Vector3{}), Input("P1", "p1", VfxValueType::Vector3, Vector3{}),
             Input("P2", "p2", VfxValueType::Vector3, Vector3{}), Input("P3", "p3", VfxValueType::Vector3, Vector3{}),
             Input("Time", "time", VfxValueType::Scalar, 0.5F), Output("Position", "out", VfxValueType::Vector3)},
            VfxValueOpcode::SampleBezier));
        result.push_back(Operator("keire.operator.swizzle", "Swizzle", "Operator/Math/Vector",
                                  {Input("Input", "input", VfxValueType::Vector4, Vector4{}),
                                   Input("X", "x", VfxValueType::UnsignedInteger, std::uint64_t{0}),
                                   Input("Y", "y", VfxValueType::UnsignedInteger, std::uint64_t{1}),
                                   Input("Z", "z", VfxValueType::UnsignedInteger, std::uint64_t{2}),
                                   Input("W", "w", VfxValueType::UnsignedInteger, std::uint64_t{3}),
                                   Output("Out", "out", VfxValueType::Vector4)},
                                  VfxValueOpcode::Swizzle));

        result.push_back(Operator(
            "keire.operator.area-circle", "Area (Circle)", "Operator/Math/Geometry",
            {Input("Radius", "radius", VfxValueType::Scalar, 0.5F), Output("Area", "out", VfxValueType::Scalar)},
            VfxValueOpcode::AreaCircle));
        result.push_back(
            Operator("keire.operator.change-space", "Change Space", "Operator/Math/Geometry",
                     {Input("Position", "position", VfxValueType::Vector3, Vector3{}),
                      Input("From", "from", VfxValueType::Matrix, Matrix4{}),
                      Input("To", "to", VfxValueType::Matrix, Matrix4{}), Output("Out", "out", VfxValueType::Vector3)},
                     VfxValueOpcode::ChangeSpace, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.distance-line", "Distance (Line)", "Operator/Math/Geometry",
                                  {Input("Point", "point", VfxValueType::Vector3, Vector3{}),
                                   Input("Start", "start", VfxValueType::Vector3, Vector3{}),
                                   Input("End", "end", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
                                   Output("Distance", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::DistanceLine));
        result.push_back(Operator("keire.operator.distance-plane", "Distance (Plane)", "Operator/Math/Geometry",
                                  {Input("Point", "point", VfxValueType::Vector3, Vector3{}),
                                   Input("Plane Point", "planePoint", VfxValueType::Vector3, Vector3{}),
                                   Input("Normal", "normal", VfxValueType::Vector3, Vector3{0.0F, 1.0F, 0.0F}),
                                   Output("Distance", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::DistancePlane));
        result.push_back(Operator("keire.operator.distance-sphere", "Distance (Sphere)", "Operator/Math/Geometry",
                                  {Input("Point", "point", VfxValueType::Vector3, Vector3{}),
                                   Input("Center", "center", VfxValueType::Vector3, Vector3{}),
                                   Input("Radius", "radius", VfxValueType::Scalar, 0.5F),
                                   Output("Distance", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::DistanceSphere));
        result.push_back(Operator(
            "keire.operator.invert-trs", "InvertTRS (Matrix)", "Operator/Math/Geometry",
            {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{}), Output("Out", "out", VfxValueType::Matrix)},
            VfxValueOpcode::InvertTrs, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.transform-direction", "Transform (Direction)",
                                  "Operator/Math/Geometry",
                                  {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{}),
                                   Input("Direction", "direction", VfxValueType::Vector3, Vector3{}),
                                   Output("Out", "out", VfxValueType::Vector3)},
                                  VfxValueOpcode::TransformDirection, VfxNodeBackendTier::CpuOnly));
        result.push_back(
            Operator("keire.operator.transform-matrix", "Transform (Matrix)", "Operator/Math/Geometry",
                     {Input("A", "a", VfxValueType::Matrix, Matrix4{}),
                      Input("B", "b", VfxValueType::Matrix, Matrix4{}), Output("Out", "out", VfxValueType::Matrix)},
                     VfxValueOpcode::TransformMatrix, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.transform-position", "Transform (Position)", "Operator/Math/Geometry",
                                  {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{}),
                                   Input("Position", "position", VfxValueType::Vector3, Vector3{}),
                                   Output("Out", "out", VfxValueType::Vector3)},
                                  VfxValueOpcode::TransformPosition, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.transform-vector", "Transform (Vector)", "Operator/Math/Geometry",
                                  {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{}),
                                   Input("Vector", "vector", VfxValueType::Vector3, Vector3{}),
                                   Output("Out", "out", VfxValueType::Vector3)},
                                  VfxValueOpcode::TransformVector, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator("keire.operator.transform-vector4", "Transform (Vector4)", "Operator/Math/Geometry",
                                  {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{}),
                                   Input("Vector", "vector", VfxValueType::Vector4, Vector4{}),
                                   Output("Out", "out", VfxValueType::Vector4)},
                                  VfxValueOpcode::TransformVector4, VfxNodeBackendTier::CpuOnly));
        result.push_back(Operator(
            "keire.operator.transpose-matrix", "Transpose (Matrix)", "Operator/Math/Geometry",
            {Input("Matrix", "matrix", VfxValueType::Matrix, Matrix4{}), Output("Out", "out", VfxValueType::Matrix)},
            VfxValueOpcode::TransposeMatrix, VfxNodeBackendTier::CpuOnly));

        result.push_back(Operator("keire.operator.volume-box", "Volume (Box)", "Operator/Math/Geometry",
                                  {Input("Size", "size", VfxValueType::Vector3, Vector3{1.0F, 1.0F, 1.0F}),
                                   Output("Volume", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::VolumeAxisAlignedBox));
        result.push_back(Operator("keire.operator.volume-cone", "Volume (Cone)", "Operator/Math/Geometry",
                                  {Input("Radius", "radius", VfxValueType::Scalar, 0.5F),
                                   Input("Height", "height", VfxValueType::Scalar, 1.0F),
                                   Output("Volume", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::VolumeCone));
        result.push_back(Operator("keire.operator.volume-cylinder", "Volume (Cylinder)", "Operator/Math/Geometry",
                                  {Input("Radius", "radius", VfxValueType::Scalar, 0.5F),
                                   Input("Height", "height", VfxValueType::Scalar, 1.0F),
                                   Output("Volume", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::VolumeCylinder));
        result.push_back(Operator(
            "keire.operator.volume-sphere", "Volume (Sphere)", "Operator/Math/Geometry",
            {Input("Radius", "radius", VfxValueType::Scalar, 0.5F), Output("Volume", "out", VfxValueType::Scalar)},
            VfxValueOpcode::VolumeSphere));
        result.push_back(Operator("keire.operator.volume-torus", "Volume (Torus)", "Operator/Math/Geometry",
                                  {Input("Minor Radius", "minorRadius", VfxValueType::Scalar, 0.25F),
                                   Input("Major Radius", "majorRadius", VfxValueType::Scalar, 1.0F),
                                   Output("Volume", "out", VfxValueType::Scalar)},
                                  VfxValueOpcode::VolumeTorus));

        result.push_back(
            Operator("keire.operator.sample-curve", "Sample Curve", "Operator/Sampling",
                     {Input("Curve", "curve", VfxValueType::Curve, Curve1D::Constant(0.0F)),
                      Input("Time", "time", VfxValueType::Scalar, 0.0F), Output("Value", "out", VfxValueType::Scalar)},
                     VfxValueOpcode::SampleCurve, VfxNodeBackendTier::CpuOnly));
        result.push_back(
            Operator("keire.operator.sample-gradient", "Sample Gradient", "Operator/Sampling",
                     {Input("Gradient", "gradient", VfxValueType::Gradient, ColorGradient::Constant(Color{})),
                      Input("Time", "time", VfxValueType::Scalar, 0.0F), Output("Color", "out", VfxValueType::Color)},
                     VfxValueOpcode::SampleGradient, VfxNodeBackendTier::CpuOnly));

        result.push_back(Resource("keire.operator.attribute-map", "Attribute Map",
                                  {Input("Texture", "resource", VfxValueType::Texture, AssetId{}),
                                   Input("UV", "coordinate", VfxValueType::Vector2, Vector2{}),
                                   Output("Value", "out", VfxValueType::Color)},
                                  VfxValueOpcode::ResourceAttributeMap));
        result.push_back(Resource("keire.operator.buffer-count", "Buffer Count",
                                  {Input("Buffer", "resource", VfxValueType::Buffer, AssetId{}),
                                   Output("Count", "out", VfxValueType::UnsignedInteger)},
                                  VfxValueOpcode::ResourceBufferCount));
        result.push_back(Resource("keire.operator.mesh-index-count", "Get Mesh Index Count",
                                  {Input("Mesh", "resource", VfxValueType::Mesh, AssetId{}),
                                   Output("Count", "out", VfxValueType::UnsignedInteger)},
                                  VfxValueOpcode::ResourceMeshIndexCount));
        result.push_back(Resource("keire.operator.mesh-triangle-count", "Get Mesh Triangle Count",
                                  {Input("Mesh", "resource", VfxValueType::Mesh, AssetId{}),
                                   Output("Count", "out", VfxValueType::UnsignedInteger)},
                                  VfxValueOpcode::ResourceMeshTriangleCount));
        result.push_back(Resource("keire.operator.mesh-vertex-count", "Get Mesh Vertex Count",
                                  {Input("Mesh", "resource", VfxValueType::Mesh, AssetId{}),
                                   Output("Count", "out", VfxValueType::UnsignedInteger)},
                                  VfxValueOpcode::ResourceMeshVertexCount));
        result.push_back(Resource("keire.operator.skinned-local-transform", "Get Skinned Mesh Local Root Transform",
                                  {Input("Skinned Mesh", "resource", VfxValueType::Mesh, AssetId{}),
                                   Output("Transform", "out", VfxValueType::Matrix)},
                                  VfxValueOpcode::ResourceMeshLocalTransform));
        result.push_back(Resource("keire.operator.skinned-world-transform", "Get Skinned Mesh World Root Transform",
                                  {Input("Skinned Mesh", "resource", VfxValueType::Mesh, AssetId{}),
                                   Output("Transform", "out", VfxValueType::Matrix)},
                                  VfxValueOpcode::ResourceMeshWorldTransform));
        result.push_back(Resource("keire.operator.texture-dimensions", "Get Texture Dimensions",
                                  {Input("Texture", "resource", VfxValueType::Texture, AssetId{}),
                                   Output("Dimensions", "out", VfxValueType::Vector3)},
                                  VfxValueOpcode::ResourceTextureDimensions));

        const auto addTexture = [&result](std::string id, std::string label, const VfxValueType resourceType,
                                          const VfxValueType coordinateType, const VfxValueOpcode opcode)
        {
            result.push_back(Resource(
                std::move(id), std::move(label),
                {Input("Texture", "resource", resourceType, AssetId{}),
                 Input("Coordinate", "coordinate", coordinateType, DefaultVfxValue(coordinateType)),
                 Input("Level", "level", VfxValueType::Scalar, 0.0F), Output("Color", "out", VfxValueType::Color)},
                opcode));
        };
        addTexture("keire.operator.load-texture2d", "Load Texture2D", VfxValueType::Texture, VfxValueType::Vector2,
                   VfxValueOpcode::ResourceLoadTexture2D);
        addTexture("keire.operator.load-texture2d-array", "Load Texture2DArray", VfxValueType::Texture2DArray,
                   VfxValueType::Vector3, VfxValueOpcode::ResourceLoadTexture2DArray);
        addTexture("keire.operator.load-texture3d", "Load Texture3D", VfxValueType::Texture3D, VfxValueType::Vector3,
                   VfxValueOpcode::ResourceLoadTexture3D);
        addTexture("keire.operator.sample-texture2d", "Sample Texture2D", VfxValueType::Texture, VfxValueType::Vector2,
                   VfxValueOpcode::ResourceSampleTexture2D);
        addTexture("keire.operator.sample-texture2d-array", "Sample Texture2DArray", VfxValueType::Texture2DArray,
                   VfxValueType::Vector3, VfxValueOpcode::ResourceSampleTexture2DArray);
        addTexture("keire.operator.sample-texture3d", "Sample Texture3D", VfxValueType::Texture3D,
                   VfxValueType::Vector3, VfxValueOpcode::ResourceSampleTexture3D);
        addTexture("keire.operator.sample-texture-cube", "Sample TextureCube", VfxValueType::TextureCube,
                   VfxValueType::Vector3, VfxValueOpcode::ResourceSampleTextureCube);
        addTexture("keire.operator.sample-texture-cube-array", "Sample TextureCubeArray", VfxValueType::Asset,
                   VfxValueType::Vector4, VfxValueOpcode::ResourceSampleTextureCubeArray);
        result.push_back(Resource("keire.operator.sample-buffer", "Sample Graphics Buffer",
                                  {Input("Buffer", "resource", VfxValueType::Buffer, AssetId{}),
                                   Input("Index", "index", VfxValueType::UnsignedInteger, std::uint64_t{0}),
                                   Output("Value", "out", VfxValueType::Vector4)},
                                  VfxValueOpcode::ResourceSampleBuffer));
        result.push_back(Resource(
            "keire.operator.sample-mesh", "Sample Mesh",
            {Input("Mesh", "resource", VfxValueType::Mesh, AssetId{}),
             Input("Coordinate", "coordinate", VfxValueType::Vector4, Vector4{}),
             Output("Position", "position", VfxValueType::Vector3), Output("Normal", "normal", VfxValueType::Vector3),
             Output("UV", "uv", VfxValueType::Vector2), Output("Color", "color", VfxValueType::Color)},
            VfxValueOpcode::ResourceSampleMesh));
        result.push_back(Resource("keire.operator.sample-mesh-index", "Sample Mesh Index",
                                  {Input("Mesh", "resource", VfxValueType::Mesh, AssetId{}),
                                   Input("Index", "index", VfxValueType::UnsignedInteger, std::uint64_t{0}),
                                   Output("Index", "out", VfxValueType::UnsignedInteger)},
                                  VfxValueOpcode::ResourceSampleMeshIndex));
        result.push_back(Resource(
            "keire.operator.sample-sdf", "Sample Signed Distance Field",
            {Input("Signed Distance Field", "resource", VfxValueType::SignedDistanceField, AssetId{}),
             Input("Coordinate", "coordinate", VfxValueType::Vector3, Vector3{}),
             Input("Level", "level", VfxValueType::Scalar, 0.0F), Output("Distance", "out", VfxValueType::Scalar)},
            VfxValueOpcode::ResourceSampleSignedDistanceField));

        result.push_back(Operator("keire.operator.spawn-state", "Spawn State", "Operator/Spawn",
                                  {Output("Playing", "playing", VfxValueType::Boolean),
                                   Output("Effect Time", "time", VfxValueType::Scalar),
                                   Output("Spawn Index", "spawnIndex", VfxValueType::UnsignedInteger)},
                                  VfxValueOpcode::SpawnState));
        return result;
    }
} // namespace Keire::Internal
