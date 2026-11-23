#version 440

// GPU Dual-Kawase backdrop blur fragment shader for Qt 6 RHI (Vulkan / OpenGL / Metal).
// Compiled via qsb (Qt6::ShaderTools) to accelerate FinnOS 35px glass and 15px
// notification backdrop blur directly on the compositor GPU scene graph.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 texelSize;
    float blurRadius;
    vec4 tintColor;
};

layout(binding = 1) uniform sampler2D source;

void main() {
    vec2 halfpixel = texelSize * max(0.5, blurRadius * 0.25);
    vec4 sum = texture(source, qt_TexCoord0 + vec2(-halfpixel.x * 2.0, 0.0));
    sum += texture(source, qt_TexCoord0 + vec2(-halfpixel.x, halfpixel.y)) * 2.0;
    sum += texture(source, qt_TexCoord0 + vec2(0.0, halfpixel.y * 2.0));
    sum += texture(source, qt_TexCoord0 + vec2(halfpixel.x, halfpixel.y)) * 2.0;
    sum += texture(source, qt_TexCoord0 + vec2(halfpixel.x * 2.0, 0.0));
    sum += texture(source, qt_TexCoord0 + vec2(halfpixel.x, -halfpixel.y)) * 2.0;
    sum += texture(source, qt_TexCoord0 + vec2(0.0, -halfpixel.y * 2.0));
    sum += texture(source, qt_TexCoord0 + vec2(-halfpixel.x, -halfpixel.y)) * 2.0;
    vec4 blurred = sum / 12.0;
    vec3 mixed = mix(blurred.rgb, tintColor.rgb, tintColor.a);
    fragColor = vec4(mixed, 1.0) * qt_Opacity;
}
