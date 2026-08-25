#include "test_harness.h"

#include "fabgl/core/guid.h"
#include "fabgl/core/result.h"
#include "fabgl/diagnostics/logger.h"
#include "fabgl/math/types.h"
#include "fabgl/reflection/reflection.h"

#include <cstdint>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

using namespace fabgl;

FGL_TEST(result_carries_values_and_structured_errors) {
    auto success = Result<int>::success(42);
    FGL_CHECK(success);
    FGL_CHECK(success.value() == 42);

    auto failure = Result<int>::failure(
        Error(ErrorCode::InvalidArgument, "bad value").addContext("field", "speed"));
    FGL_CHECK(!failure);
    FGL_CHECK(failure.error().code() == ErrorCode::InvalidArgument);
    FGL_CHECK(failure.error().message() == "bad value");
    FGL_CHECK(failure.error().context().size() == 1);
    FGL_CHECK(failure.error().context()[0].key == "field");

    auto voidSuccess = Result<void>::success();
    FGL_CHECK(voidSuccess);
    auto voidFailure = Result<void>::failure(Error(ErrorCode::IoError, "disk unavailable"));
    FGL_CHECK(!voidFailure);
    FGL_CHECK(voidFailure.error().code() == ErrorCode::IoError);
}

FGL_TEST(strong_guids_are_canonical_distinct_and_stable) {
    static_assert(!std::is_same_v<EntityGuid, AssetGuid>);
    const auto generated = EntityGuid::generate();
    FGL_CHECK(!generated.isNil());
    const auto text = generated.toString();
    FGL_CHECK(text.size() == 36);
    auto parsed = EntityGuid::parse(text);
    FGL_CHECK(parsed);
    FGL_CHECK(parsed.value() == generated);

    const auto stableA = AssetGuid::fromStableName("assets/player.png");
    const auto stableB = AssetGuid::fromStableName("assets/player.png");
    const auto stableC = AssetGuid::fromStableName("assets/enemy.png");
    FGL_CHECK(stableA == stableB);
    FGL_CHECK(stableA != stableC);
    FGL_CHECK(!stableA.isNil());

    auto uppercase = EntityGuid::parse("550E8400-E29B-41D4-A716-446655440000");
    FGL_CHECK(uppercase);
    FGL_CHECK(uppercase.value().toString() == "550e8400-e29b-41d4-a716-446655440000");
    FGL_CHECK(!EntityGuid::parse("not-a-guid"));
    FGL_CHECK(!EntityGuid::parse("550e8400-e29b-41d4-a716-44665544000z"));

    std::set<std::string> generatedValues;
    for (std::size_t index = 0U; index < 4096U; ++index) {
        const auto value = EntityGuid::generate();
        FGL_CHECK((value.bytes()[6] & 0xF0U) == 0x40U);
        FGL_CHECK((value.bytes()[8] & 0xC0U) == 0x80U);
        FGL_CHECK(generatedValues.insert(value.toString()).second);
    }
}

FGL_TEST(math_types_cover_geometry_color_fixed_point_and_trs) {
    FGL_CHECK((Vec2{1.0F, 2.0F} + Vec2{3.0F, 4.0F}) == Vec2{4.0F, 6.0F});
    FGL_CHECK(Rect{1.0F, 2.0F, 5.0F, 6.0F}.contains(Vec2{6.0F, 8.0F}));
    FGL_CHECK(!Rect{1.0F, 2.0F, 5.0F, 6.0F}.contains(Vec2{6.1F, 8.0F}));
    FGL_CHECK(Color{0x12, 0x34, 0x56, 0x78}.rgba32() == 0x12345678U);

    const auto oneAndHalf = Fixed::fromFloat(1.5F);
    const auto two = Fixed(2);
    FGL_CHECK_NEAR((oneAndHalf * two).toFloat(), 3.0F, 0.0001F);
    FGL_CHECK_NEAR((two / oneAndHalf).toFloat(), 1.3333F, 0.001F);

    const auto matrix = Mat4::trs({10.0F, 20.0F, 0.0F}, {}, {2.0F, 3.0F, 1.0F});
    const auto transformed = matrix.transformPoint({1.0F, 1.0F, 0.0F});
    FGL_CHECK(nearlyEqual(transformed, {12.0F, 23.0F, 0.0F}));
}

FGL_TEST(structured_logger_filters_and_emits_json_lines) {
    Logger logger;
    auto memory = std::make_shared<MemoryLogSink>();
    std::ostringstream json;
    auto jsonSink = std::make_shared<JsonLineLogSink>(json);
    logger.addSink(memory);
    logger.addSink(jsonSink);
    logger.setMinimumLevel(LogLevel::Info);

    logger.log(LogLevel::Debug, "engine", "hidden");
    logger.log(LogLevel::Warning, "assets", "budget exceeded",
               {{"used_bytes", std::uint64_t{2048}},
                {"recoverable", true},
                {"path", std::string("a\"b")}});

    const auto records = memory->records();
    FGL_CHECK(records.size() == 1);
    FGL_CHECK(records[0].sequence == 1);
    FGL_CHECK(records[0].level == LogLevel::Warning);
    FGL_CHECK(records[0].fields.size() == 3);
    FGL_CHECK(json.str().find("\"category\":\"assets\"") != std::string::npos);
    FGL_CHECK(json.str().find("\"used_bytes\":2048") != std::string::npos);
    FGL_CHECK(json.str().find("a\\\"b") != std::string::npos);
}

namespace {
struct ReflectedObject final {
    Vec3 position{};
};

TypeMetadata reflectedObjectMetadata() {
    TypeMetadata type;
    type.typeId = ComponentTypeGuid::fromStableName("tests.ReflectedObject");
    type.name = "tests.ReflectedObject";
    type.displayName = "Reflected Object";
    PropertyMetadata property;
    property.name = "position";
    property.displayName = "Position";
    property.type = PropertyType::Vec3;
    property.flags = PropertyFlags::Serialize | PropertyFlags::RuntimeEditable;
    property.defaultValue = Vec3{};
    property.reader = [](const void* instance) {
        return Result<PropertyValue>::success(
            PropertyValue(static_cast<const ReflectedObject*>(instance)->position));
    };
    property.writer = [](void* instance, const PropertyValue& value) {
        const auto* vector = std::get_if<Vec3>(&value);
        if (vector == nullptr) {
            return Result<void>::failure(Error(ErrorCode::TypeMismatch, "expected Vec3"));
        }
        static_cast<ReflectedObject*>(instance)->position = *vector;
        return Result<void>::success();
    };
    type.properties.push_back(std::move(property));
    return type;
}
} // namespace

FGL_TEST(reflection_registry_exposes_metadata_and_safe_accessors) {
    ReflectionRegistry registry;
    auto metadata = reflectedObjectMetadata();
    const auto typeId = metadata.typeId;
    FGL_CHECK(registry.registerType(std::move(metadata)));
    FGL_CHECK(registry.size() == 1);
    const auto* type = registry.find(typeId);
    FGL_CHECK(type != nullptr);
    FGL_CHECK(registry.find("tests.ReflectedObject") == type);
    const auto* property = type->findProperty("position");
    FGL_CHECK(property != nullptr);

    ReflectedObject object;
    FGL_CHECK(property->write(&object, PropertyValue(Vec3{4.0F, 5.0F, 6.0F})));
    FGL_CHECK(object.position == Vec3{4.0F, 5.0F, 6.0F});
    auto read = property->read(&object);
    FGL_CHECK(read);
    FGL_CHECK(std::get<Vec3>(read.value()) == object.position);
    FGL_CHECK(!property->write(&object, PropertyValue(std::string("wrong"))));

    auto duplicate = reflectedObjectMetadata();
    FGL_CHECK(!registry.registerType(std::move(duplicate)));
}

FGL_TEST(reflection_validates_structured_property_values_and_editor_metadata) {
    PropertyMetadata list;
    list.name = "targets";
    list.type = PropertyType::List;
    list.listElementType = PropertyType::ComponentReference;
    const auto entity = EntityGuid::fromStableName("tests.reflection.entity");
    const auto component = ComponentTypeGuid::fromStableName("tests.reflection.component");
    PropertyList references{PropertyType::ComponentReference,
                            {ComponentReference{entity, component}}};
    FGL_CHECK(validatePropertyValue(list, PropertyValue(references)));
    references.values.push_back(ComponentReference{entity, ComponentTypeGuid{}});
    FGL_CHECK(!validatePropertyValue(list, PropertyValue(references)));

    PropertyMetadata curve;
    curve.name = "curve";
    curve.type = PropertyType::Curve;
    FGL_CHECK(validatePropertyValue(
        curve, PropertyValue(Curve{{CurvePoint{0.0, 0.0}, CurvePoint{1.0, 1.0}}})));
    FGL_CHECK(!validatePropertyValue(
        curve, PropertyValue(Curve{{CurvePoint{1.0, 0.0}, CurvePoint{1.0, 1.0}}})));

    PropertyMetadata animation;
    animation.name = "animation";
    animation.type = PropertyType::AnimationCurve;
    FGL_CHECK(validatePropertyValue(
        animation,
        PropertyValue(PropertyAnimationCurve{{AnimationCurveKey{0.0, 0.0, 0.0, 0.0},
                                               AnimationCurveKey{2.0, 1.0, -0.5, 0.5}}})));

    TypeMetadata invalidSlider;
    invalidSlider.typeId = ComponentTypeGuid::fromStableName("tests.InvalidSlider");
    invalidSlider.name = "tests.InvalidSlider";
    PropertyMetadata slider;
    slider.name = "amount";
    slider.type = PropertyType::Float;
    slider.defaultValue = 0.5;
    slider.editorHint = PropertyEditorHint::Slider;
    invalidSlider.properties.push_back(std::move(slider));
    ReflectionRegistry registry;
    FGL_CHECK(!registry.registerType(std::move(invalidSlider)));
}
