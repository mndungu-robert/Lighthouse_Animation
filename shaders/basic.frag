#version 330 core

in vec3 vColor;
in vec3 vNormal;
in vec3 vFragPos;

out vec4 FragColor;

uniform vec3 uViewPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uFogColor;
uniform vec3 uSkyHorizonColor;
uniform vec3 uSkyZenithColor;

// Rotating Lighthouse Spotlight Uniforms
uniform vec3 uLanternPos;
uniform vec3 uBeamDir;
uniform float uCosInner;
uniform float uCosOuter;
uniform int uBeamEnabled;

uniform int uIsWater;
uniform int uIsLantern;
uniform int uLightingEnabled;
uniform float uTime;

void main()
{
    // If rendering the lantern glass itself, give it a bright warm self-glow
    if (uIsLantern == 1) {
        vec3 glow = (uBeamEnabled == 1) ? vec3(1.0, 0.92, 0.55) * 1.35 : vec3(0.4, 0.35, 0.25);
        FragColor = vec4(glow, 1.0);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vFragPos);
    vec3 L = normalize(uSunDir);

    // Fine water ripples
    if (uIsWater == 1) {
        float ripple1 = sin(vFragPos.x * 3.5 + uTime * 2.5) * cos(vFragPos.z * 3.5 - uTime * 2.0);
        float ripple2 = sin((vFragPos.x + vFragPos.z) * 6.0 - uTime * 3.2);
        N = normalize(N + vec3(ripple1 * 0.06, 0.0, ripple2 * 0.06));
    }

    vec3 baseColor = vColor;

    // Fresnel Sky Reflection on Water
    if (uIsWater == 1) {
        vec3 R = reflect(-V, N);
        float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        vec3 reflectedSky = mix(uSkyHorizonColor, uSkyZenithColor, clamp(R.y * 1.5, 0.0, 1.0));
        baseColor = mix(vec3(0.02, 0.08, 0.16), reflectedSky, clamp(fresnel * 0.85 + 0.18, 0.0, 1.0));
    }

    vec3 litColor = baseColor;

    if (uLightingEnabled == 1) {
        // 1. Directional Sun / Moon Light (Blinn-Phong)
        vec3 ambient = (0.14 + 0.18 * length(uSunColor) * 0.5) * baseColor;

        float diff = max(dot(N, L), 0.0);
        vec3 diffuse = diff * uSunColor * baseColor;

        vec3 H = normalize(L + V);
        float shininess = (uIsWater == 1) ? 128.0 : 24.0;
        float specStrength = (uIsWater == 1) ? 1.4 : 0.25;
        float spec = pow(max(dot(N, H), 0.0), shininess);
        vec3 specular = specStrength * spec * uSunColor;

        litColor = ambient + diffuse + specular;

        // 2. Rotating Lighthouse Spotlight Beam
        if (uBeamEnabled == 1) {
            vec3 toFrag = vFragPos - uLanternPos;
            float distToLantern = length(toFrag);
            vec3 fragDir = normalize(toFrag);

            // Spotlight cone falloff using smoothstep
            float theta = dot(fragDir, normalize(uBeamDir));
            float spotIntensity = smoothstep(uCosOuter, uCosInner, theta);

            // Distance attenuation so light fades naturally over far ocean
            float attenuation = 1.0 / (1.0 + 0.015 * distToLantern + 0.0018 * distToLantern * distToLantern);

            vec3 beamLightColor = vec3(1.0, 0.92, 0.65) * 3.2;
            vec3 lightToLantern = -fragDir;

            float spotDiff = max(dot(N, lightToLantern), 0.0);
            vec3 spotHalf = normalize(lightToLantern + V);
            float spotSpec = pow(max(dot(N, spotHalf), 0.0), (uIsWater == 1) ? 96.0 : 32.0);

            litColor += (spotDiff * baseColor + spotSpec * 0.85) * beamLightColor * spotIntensity * attenuation;
        }
    }

    // Distance Horizon Fog
    float dist = length(uViewPos - vFragPos);
    float fogFactor = clamp((dist - 18.0) / 38.0, 0.0, 1.0);
    vec3 finalColor = mix(litColor, uFogColor, fogFactor);

    FragColor = vec4(finalColor, 1.0);
}
