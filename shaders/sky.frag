#version 330 core
in vec3 vDir;
out vec4 FragColor;

uniform vec3 uSunDir;
uniform vec3 uMoonDir;
uniform float uDayFactor; // 1.0 = full day, 0.0 = deep night

// Simple pseudo-random hash for procedural stars
float hash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

void main()
{
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, 0.0, 1.0);

    // 1. Day Sky Colors
    vec3 dayHorizon = vec3(0.68, 0.84, 0.96);
    vec3 dayZenith  = vec3(0.18, 0.45, 0.82);
    vec3 daySky     = mix(dayHorizon, dayZenith, smoothstep(0.0, 0.85, h));

    // 2. Sunset / Golden-Hour Colors (when sun is near horizon)
    float sunsetFactor = clamp(1.0 - abs(uSunDir.y) * 3.2, 0.0, 1.0);
    vec3 sunsetHorizon = vec3(0.98, 0.46, 0.12);
    vec3 sunsetZenith  = vec3(0.14, 0.18, 0.36);
    vec3 sunsetSky     = mix(sunsetHorizon, sunsetZenith, smoothstep(0.0, 0.85, h));

    // 3. Night Sky Colors
    vec3 nightHorizon = vec3(0.03, 0.05, 0.10);
    vec3 nightZenith  = vec3(0.01, 0.01, 0.04);
    vec3 nightSky     = mix(nightHorizon, nightZenith, smoothstep(0.0, 0.85, h));

    // Blend between Night -> Day -> Sunset
    vec3 sky = mix(nightSky, daySky, uDayFactor);
    sky = mix(sky, sunsetSky, sunsetFactor * 0.85);

    // 4. Sun Disc & Glow
    float sunAmt = max(dot(dir, normalize(uSunDir)), 0.0);
    if (uSunDir.y > -0.15) {
        sky += vec3(1.0, 0.65, 0.25) * pow(sunAmt, 12.0) * 0.45;
        sky += vec3(1.0, 0.95, 0.80) * smoothstep(0.9985, 0.9995, sunAmt) * 1.6;
    }

    // 5. Moon Disc & Cool Glow
    float moonAmt = max(dot(dir, normalize(uMoonDir)), 0.0);
    if (uMoonDir.y > -0.1) {
        sky += vec3(0.45, 0.60, 0.85) * pow(moonAmt, 16.0) * 0.25;
        sky += vec3(0.90, 0.95, 1.00) * smoothstep(0.9988, 0.9996, moonAmt) * 1.1;
    }

    // 6. Procedural Stars (fade in at night above horizon)
    float nightVisibility = (1.0 - uDayFactor) * smoothstep(0.05, 0.35, h);
    if (nightVisibility > 0.01) {
        vec3 starGrid = floor(dir * 260.0);
        float starVal = hash(starGrid);
        if (starVal > 0.9965) {
            float brightness = (starVal - 0.9965) / 0.0035;
            sky += vec3(0.85, 0.90, 1.0) * brightness * nightVisibility * 1.2;
        }
    }

    FragColor = vec4(sky, 1.0);
}
