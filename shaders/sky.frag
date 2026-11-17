#version 330 core
in vec3 vDir;
out vec4 FragColor;

uniform vec3 uSunDir;

void main()
{
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, 0.0, 1.0);

    // Golden-hour sky gradient (warm orange horizon -> deep dusk blue overhead)
    vec3 horizonColor = vec3(0.96, 0.48, 0.14);
    vec3 midSkyColor  = vec3(0.68, 0.34, 0.28);
    vec3 zenithColor  = vec3(0.12, 0.20, 0.38);

    vec3 sky = mix(horizonColor, midSkyColor, smoothstep(0.0, 0.35, h));
    sky = mix(sky, zenithColor, smoothstep(0.25, 0.85, h));

    // Glowing sun disc near the horizon
    float sunAmount = max(dot(dir, normalize(uSunDir)), 0.0);
    sky += vec3(1.0, 0.65, 0.25) * pow(sunAmount, 12.0) * 0.45;
    sky += vec3(1.0, 0.90, 0.65) * smoothstep(0.9985, 0.9995, sunAmount) * 1.5;

    FragColor = vec4(sky, 1.0);
}
