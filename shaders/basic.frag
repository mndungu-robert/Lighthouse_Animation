#version 330 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uFogColor;
uniform int uIsWater;
uniform int uLightingEnabled;
uniform float uTime;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vFragPos);
    vec3 L = normalize(uSunDir);

    // Add fine micro-ripples to water normal for shimmering sunset reflection
    if (uIsWater == 1) {
        float ripple1 = sin(vFragPos.x * 3.5 + uTime * 2.5) * cos(vFragPos.z * 3.5 - uTime * 2.0);
        float ripple2 = sin((vFragPos.x + vFragPos.z) * 6.0 - uTime * 3.2);
        N = normalize(N + vec3(ripple1 * 0.06, 0.0, ripple2 * 0.06));
    }

    vec3 baseColor = vColor;

    // Fresnel Sky Reflection on Water (like your reference photo)
    if (uIsWater == 1) {
        vec3 R = reflect(-V, N);
        float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        vec3 reflectedSky = mix(vec3(0.95, 0.48, 0.16), vec3(0.18, 0.25, 0.42), clamp(R.y * 1.5, 0.0, 1.0));
        baseColor = mix(vec3(0.04, 0.14, 0.24), reflectedSky, clamp(fresnel * 0.85 + 0.2, 0.0, 1.0));
    }

    vec3 litColor = baseColor;

    if (uLightingEnabled == 1) {
        // 1. Ambient Light (warm dusk fill)
        vec3 ambient = vec3(0.28, 0.22, 0.24) * baseColor;

        // 2. Diffuse Light (Lambertian)
        float diff = max(dot(N, L), 0.0);
        vec3 diffuse = diff * uSunColor * baseColor;

        // 3. Specular Light (Blinn-Phong)
        vec3 H = normalize(L + V);
        float shininess = (uIsWater == 1) ? 128.0 : 24.0;
        float specStrength = (uIsWater == 1) ? 1.4 : 0.25;
        float spec = pow(max(dot(N, H), 0.0), shininess);
        vec3 specular = specStrength * spec * uSunColor;

        litColor = ambient + diffuse + specular;
    }

    // Distance Horizon Fog so the ocean blends seamlessly into the sunset sky
    float dist = length(uViewPos - vFragPos);
    float fogFactor = clamp((dist - 18.0) / 38.0, 0.0, 1.0);
    vec3 finalColor = mix(litColor, uFogColor, fogFactor);

    FragColor = vec4(finalColor, 1.0);
}
